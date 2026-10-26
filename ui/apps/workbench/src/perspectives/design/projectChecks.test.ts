// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { hierarchyOf, type ComponentDescription, type Project } from '@embsim/protocol';
import { checkProject, liveProjectChecks } from './projectChecks';
const desc = (pins: ComponentDescription['pins']) => ({
  description: { pins } as ComponentDescription,
});
const p = (): Project => ({
  schema_version: 1,
  kind: 'project',
  name: 'Checks',
  components: [
    { id: 'A', component: 'block/io' },
    { id: 'B', component: 'block/io' },
  ],
  nets: { JOIN: ['A.OUT', 'B.OUT'] },
  blocks: {
    io: {
      name: 'IO',
      components: [{ id: 'U', component: 'test/driver' }],
      nets: { Q: ['U.Y'], IN: ['U.A'] },
      ports: { OUT: 'U.Y', IN: 'U.A' },
    },
  },
});
const catalog = {
  'test/driver': desc([
    { name: 'Y', model: { kind: 'digital_out', drive: 'push_pull' } },
    { name: 'A', model: { kind: 'digital_in' } },
    { name: 'VCC', model: { kind: 'power' }, required: true },
  ]),
};
const electrical = (doc: Project, entries = catalog) =>
  checkProject(doc, hierarchyOf(doc), entries, {}).filter((i) => i.source === 'electrical');
describe('project electrical review', () => {
  it('resolves driver topology across nested ports without claiming current contention', () => {
    const issues = electrical(p()),
      conflict = issues.find((i) => i.code === 'multiple-push-pull');
    expect(conflict?.severity).toBe('warning');
    expect(conflict?.detail).toContain('A.U.Y, B.U.Y');
    expect(issues.filter((i) => i.code === 'required-terminal')).toHaveLength(2);
    expect(issues.filter((i) => i.code === 'unconnected-digital-input')).toHaveLength(2);
    expect(issues.some((i) => i.code === 'live-contention')).toBe(false);
  });
  it('does not combine disconnected repeated local names or open-drain outputs', () => {
    const doc = p();
    doc.nets = {};
    expect(electrical(doc).some((i) => i.code === 'multiple-push-pull')).toBe(false);
    const entries = structuredClone(catalog);
    entries['test/driver'].description.pins[0]!.model!.drive = 'open_drain';
    expect(electrical(p(), entries).some((i) => i.code === 'multiple-push-pull')).toBe(false);
  });
  it('does not call an input unconnected when another sheet drives it', () => {
    const doc = p();
    doc.nets = { LINK: ['A.OUT', 'B.IN'] };
    expect(
      electrical(doc)
        .filter((i) => i.code === 'unconnected-digital-input')
        .map((i) => i.title),
    ).toEqual(['A.U.A: unconnected digital input']);
  });
  it('uses aliases and declared pulls, while optional power rails remain advisory', () => {
    const doc = p(),
      entries = structuredClone(catalog);
    entries['test/driver'].description.pins[0]!.aliases = ['OUTPUT'];
    doc.blocks!.io!.nets.Q = ['U.OUTPUT'];
    doc.blocks!.io!.ports.OUT = 'U.OUTPUT';
    entries['test/driver'].description.pins[1]!.model!.pull = 'up';
    entries['test/driver'].description.pins[2]!.required = false;
    const issues = electrical(doc, entries);
    expect(issues.filter((i) => i.code === 'multiple-push-pull')).toHaveLength(1);
    expect(issues.some((i) => i.code === 'unconnected-digital-input')).toBe(false);
    expect(issues.filter((i) => i.code === 'power-connection').every((i) => i.severity === 'info')).toBe(
      true,
    );
  });
  it('reports input-only nets but allows a board boundary or declared pull', () => {
    const doc = p();
    doc.nets = { INPUT: ['A.IN', 'B.IN'] };
    expect(electrical(doc).some((i) => i.code === 'input-only-net')).toBe(true);
    doc.boards = [{ id: 'MCU', board: 'test/board' }];
    doc.nets.INPUT!.push('MCU.IO0');
    expect(electrical(doc).some((i) => i.code === 'input-only-net')).toBe(false);
    expect(
      checkProject(doc, hierarchyOf(doc), catalog, {}).some((i) => i.code === 'board-erc-boundary'),
    ).toBe(true);
  });
  it('reports missing metadata and invalid buses rather than passing unchecked designs', () => {
    const doc = p();
    doc.layout = {
      buses: [
        {
          name: 'BAD',
          nets: ['JOIN', 'missing'],
          points: [
            [0, 0],
            [0, 40],
          ],
        },
      ],
    };
    doc.nets.JOIN = ['A.OUT'];
    const issues = checkProject(doc, hierarchyOf(doc), {}, {});
    expect(issues.some((i) => i.code === 'erc-description-unavailable')).toBe(true);
    expect(issues.some((i) => i.code === 'small-net' && i.net === 'JOIN')).toBe(false);
    expect(issues.some((i) => i.code === 'invalid-bus')).toBe(true);
  });
  it('classifies only native evidence as live contention and ignores stale nets', () => {
    const nodes = hierarchyOf(p());
    expect(
      liveProjectChecks(nodes, {
        JOIN: { logic: 'X', receivers: ['A.U.A'] },
      })[0]!.code,
    ).toBe('live-unknown');
    expect(liveProjectChecks(nodes, { JOIN: { logic: 'X', contention: true } })[0]!.code).toBe(
      'live-contention',
    );
    expect(liveProjectChecks(nodes, { JOIN: { voltage: 0 } })).toEqual([]);
    expect(
      liveProjectChecks(nodes, {
        removed: { logic: 'Z', receivers: ['old.A'] },
      }),
    ).toEqual([]);
  });
});
