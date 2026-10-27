// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import type { Project } from '@embsim/protocol';
import { describe, expect, it, vi } from 'vitest';
import { liveElectricalIssues } from './electricalFeedback';
import { SchematicCanvas, type SceneCatalog } from './SchematicCanvas';
import { checkSchematic } from './schematicChecks';

const catalog: SceneCatalog = { gate: { name: 'Gate', svg: '', symbol: { svg: '', width: 80, height: 40, pins: { A: { x: 0, y: 20, side: 'left' }, Y: { x: 80, y: 20, side: 'right' } } }, terminals: ['A', 'Y'], pins: [{ name: 'A', model: { kind: 'digital_in' } }, { name: 'Y', model: { kind: 'digital_out' } }] } };
const circuit = (): Project => ({ schema_version: 1, kind: 'project', name: 'feedback', components: [{ id: 'U1', component: 'gate', layout: { x: 100, y: 100 } }], nets: { INPUT: ['U1.A'], OUTPUT: ['U1.Y'] }, layout: { wires: [{ net: 'INPUT', points: [[0, 120], [100, 120]] }, { net: 'OUTPUT', points: [[180, 120], [240, 120]] }] } });

describe('observed electrical feedback', () => {
  it('picks observation probes from wires and pins by keyboard in read-only mode', () => {
    const p=circuit(), before=structuredClone(p), pick=vi.fn();
    render(<SchematicCanvas project={p} catalog={catalog} mode="probe" readOnly selection={{components:[],wires:[]}} onSelectionChange={() => undefined} onNetClick={pick}/>);
    fireEvent.keyDown(screen.getByRole('button',{name:'Track 1, net INPUT'}),{key:'Enter'});
    expect(pick).toHaveBeenLastCalledWith('INPUT',{x:0,y:120});
    fireEvent.keyDown(screen.getByRole('button',{name:/Pin U1.A/}),{key:'Enter'});
    expect(pick).toHaveBeenLastCalledWith('INPUT',{x:100,y:120});
    expect(p).toEqual(before);
  });
  it('cancels deferred viewport work when a sheet is closed', () => {
    vi.useFakeTimers();
    try {
      const {unmount}=render(<SchematicCanvas project={circuit()} catalog={catalog} mode="select" selection={{components:[],wires:[]}} onSelectionChange={() => undefined}/>);
      expect(vi.getTimerCount()).toBeGreaterThan(0);
      unmount();expect(vi.getTimerCount()).toBe(0);
    } finally { vi.useRealTimers(); }
  });
  it('distinguishes contention, unknown inputs and high impedance without diagnosing analog voltages', () => {
    const p = circuit();
    expect(liveElectricalIssues(p, catalog, { INPUT: { logic: 'X' } })[0]?.code).toBe('unknown-input');
    expect(liveElectricalIssues(p, catalog, { INPUT: { logic: 'Z' } })[0]?.code).toBe('floating-input');
    expect(liveElectricalIssues(p, catalog, { OUTPUT: { logic: '1', contention: true } })[0]?.code).toBe('driver-conflict');
    expect(liveElectricalIssues(p, catalog, { INPUT: { voltage: 0 }, OUTPUT: { logic: 'Z' } })).toEqual([]);
    expect(liveElectricalIssues(p, catalog, {})).toEqual([]);
    expect(liveElectricalIssues(p, catalog, { INPUT: { logic: '0' }, OUTPUT: { logic: '1' } })).toEqual([]);
  });
  it('uses active receiver metadata so output GPIOs are not called floating inputs', () => {
    expect(liveElectricalIssues(circuit(), catalog, { INPUT: { logic: 'Z', receivers: [] } })).toEqual([]);
    const issues = liveElectricalIssues(circuit(), {}, { INPUT: { kind: 'mixed', logic: 'X', receivers: ['B1.U4.GPIO4'] } });
    expect(issues[0]?.detail).toContain('B1.U4.GPIO4');
  });
  it('warns about unwired digital inputs but respects an explicitly declared pull', () => {
    const p = circuit(); p.nets = {};
    expect(checkSchematic(p, catalog).some((i) => i.code === 'unconnected-input')).toBe(true);
    const pulled: SceneCatalog = { gate: { ...catalog.gate!, pins: [{ name: 'A', model: { kind: 'digital_in', pull: 'up' } }] } };
    expect(checkSchematic(p, pulled).some((i) => i.code === 'unconnected-input')).toBe(false);
    pulled.gate!.pins![0]!.model!.pull = 'none';
    expect(checkSchematic(p, pulled).some((i) => i.code === 'unconnected-input')).toBe(true);
  });
  it('highlights every terminal of a selected wire net and keeps live labels after pausing', () => {
    const { container } = render(<SchematicCanvas project={circuit()} catalog={catalog} mode="select" selection={{ components: [], wires: [0] }} onSelectionChange={() => undefined} live netStates={{ INPUT: { logic: '1' } }} />);
    expect(container.querySelector('[data-pin="U1.A"]')?.classList.contains('es-pin--spotlight')).toBe(true);
    expect(container.querySelector('[data-pin="U1.A"] .es-pin__label')?.textContent).toBe('A · 1');
    expect(container.querySelector('[data-pin="U1.Y"]')?.classList.contains('es-pin--spotlight')).toBe(false);
    expect(screen.getByRole('status').textContent).toContain('INPUT');
  });
  it('locates an electrical warning without adding a probe or changing topology', () => {
    const p = circuit(), before = structuredClone(p), select = vi.fn();
    render(<SchematicCanvas project={p} catalog={catalog} mode="select" selection={{ components: [], wires: [] }} onSelectionChange={select} live netStates={{ INPUT: { logic: 'Z' } }} />);
    fireEvent.click(screen.getByRole('button', { name: /1 electrical warning/ }));
    fireEvent.click(screen.getByRole('button', { name: /INPUT: floating input/ }));
    expect(select).toHaveBeenCalledWith({ components: [], wires: [0] });
    expect(p).toEqual(before);
  });
  it('keeps terminal names horizontal outside a rotated symbol', () => {
    const p = circuit(); p.components[0]!.layout!.rotation = 90;
    const { container } = render(<SchematicCanvas project={p} catalog={catalog} mode="select" selection={{ components: ['U1'], wires: [] }} onSelectionChange={() => undefined} />);
    const text = container.querySelector('[data-pin="U1.A"] .es-pin__label')!;
    expect(text.getAttribute('x')).toBe('0');
    expect(text.getAttribute('y')).toBe('-10');
    expect(text.getAttribute('transform')).toBeNull();
  });
});

describe('unfitted footprints', () => {
  it('keeps pad sockets and live voltages in read-only designs without inventing input receivers', () => {
    const project = circuit(); project.components[0]!.populated = false;
    const states = {INPUT: {voltage: 5, logic: 'Z' as const}, OUTPUT: {voltage: 0}};
    render(<SchematicCanvas project={project} catalog={catalog} readOnly live netStates={states} mode="select" selection={{components:[],wires:[]}} onSelectionChange={() => undefined} />);
    expect(screen.getByRole('button', {name: /Pin U1.A, net INPUT.*5.000 V/})).toBeTruthy();
    expect(screen.getByRole('button', {name: /Pin U1.Y, net OUTPUT.*0.000 V/})).toBeTruthy();
    expect(liveElectricalIssues(project, catalog, states)).toEqual([]);
  });
});
