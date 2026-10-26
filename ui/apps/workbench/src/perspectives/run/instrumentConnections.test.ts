// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { hierarchyOf, type Project } from '@embsim/protocol';
import { configureInstrument, instrumentFingerprint, instrumentInstances } from './instrumentConnections';
const root = (): Project => ({
  schema_version: 1,
  kind: 'project',
  name: 'Physical instruments',
  components: [
    { id: 'S', component: 'emblink/scope_input' },
    { id: 'R', component: 'emblink/resistor' },
  ],
  nets: { A: ['S.+', 'R.1'], B: ['S.-'], C: ['R.2'] },
});
const draft = (p: Project) => {
  const i = instrumentInstances(p, hierarchyOf(p))[0]!;
  return { positive: i.positive, negative: i.negative, values: {}, expected: instrumentFingerprint(i) };
};
describe('physical instrument configuration', () => {
  it('changes actual lead connectivity while preserving unrelated net members', () => {
    const p = root(),
      d = { ...draft(p), negative: 'C', values: { input_resistance: '10meg', input_capacitance: '12p' } };
    configureInstrument(p, '', 'S', d, {});
    expect(p.nets).toEqual({ A: ['S.+', 'R.1'], B: [], C: ['R.2', 'S.-'] });
    expect(p.components[0]!.params).toEqual({ input_resistance: 1e7, input_capacitance: 12e-12 });
  });
  it('does not materialize equivalent defaults or restart an unchanged circuit', () => {
    const p = root(),
      before = structuredClone(p);
    configureInstrument(
      p,
      '',
      'S',
      { ...draft(p), values: { input_resistance: '1meg', input_capacitance: '15p' } },
      {},
    );
    expect(p).toEqual(before);
  });
  it('rejects stale drafts, invalid values, unsupported keys and disappeared nets', () => {
    const p = root(),
      old = draft(p);
    p.components[0]!.params = { input_resistance: 2e6 };
    expect(() => configureInstrument(structuredClone(p), '', 'S', old, {})).toThrow('changed');
    const invalid: Record<string, string>[] = [
      { input_resistance: '0' },
      { input_resistance: 'NaN' },
      { input_capacitance: '-1p' },
      { gain: '10' },
    ];
    for (const values of invalid)
      expect(() => configureInstrument(structuredClone(p), '', 'S', { ...draft(p), values }, {})).toThrow();
    expect(() => configureInstrument(p, '', 'S', { ...draft(p), negative: 'missing' }, {})).toThrow(
      'no longer exists',
    );
  });
  it('keeps numeric semantics and supports zero capacitance for a termination', () => {
    const p = root();
    configureInstrument(
      p,
      '',
      'S',
      { ...draft(p), values: { input_resistance: '50', input_capacitance: '0' } },
      {},
    );
    expect(p.components[0]!.params).toEqual({ input_resistance: 50, input_capacitance: 0 });
  });
  it('enumerates qualified nested instruments with native resolved values', () => {
    const p: Project = {
      schema_version: 1,
      kind: 'project',
      name: 'Nested',
      components: [
        { id: 'A', component: 'block/probe' },
        { id: 'B', component: 'block/probe' },
      ],
      nets: {},
      blocks: {
        probe: {
          name: 'Probe',
          params: { r: { type: 'number', default: '1meg' } },
          components: [{ id: 'S', component: 'emblink/scope_input', params: { input_resistance: '$r' } }],
          nets: { IN: ['S.+'], REF: ['S.-'] },
          ports: { IN: 'S.+', REF: 'S.-' },
        },
      },
    };
    const nodes = hierarchyOf(p);
    nodes[1]!.params = { r: 2e6 };
    nodes[2]!.params = { r: 3e6 };
    const instruments = instrumentInstances(p, nodes);
    expect(instruments.map((i) => [i.qualified, i.values.input_resistance, i.shared])).toEqual([
      ['A.S', 2e6, 2],
      ['B.S', 3e6, 2],
    ]);
    const i = instruments[0]!;
    expect(() =>
      configureInstrument(
        p,
        'A',
        'S',
        {
          positive: 'IN',
          negative: 'REF',
          expected: instrumentFingerprint(i),
          values: { input_resistance: '9meg' },
        },
        {},
      ),
    ).toThrow('bound');
    expect(() =>
      configureInstrument(
        p,
        'A',
        'S',
        { positive: '', negative: 'REF', expected: instrumentFingerprint(i), values: {} },
        {},
      ),
    ).toThrow('exported');
  });
});
