// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { clipOf, withClip } from './project';

const project = (): Project => ({
  schema_version: 1,
  kind: 'project',
  name: 't',
  components: [
    { id: 'R1', component: 'emblink/resistor', params: { resistance: '1k' }, layout: { x: 0, y: 0 } },
    { id: 'D1', component: 'emblink/led', layout: { x: 100, y: 0 } },
    { id: 'G1', component: 'emblink/gnd', layout: { x: 200, y: 0 } },
  ],
  nets: { A: ['R1.2', 'D1.A'], GND: ['D1.K', 'G1.GND'], IN: ['R1.1'] },
  layout: { wires: [{ net: 'A', points: [[60, 10], [100, 10]] }, { net: 'GND', points: [[160, 10], [210, 0]] }] },
});

describe('copy and paste', () => {
  it('copies the parts with the nets and wires between them', () => {
    const clip = clipOf(project(), { components: ['R1', 'D1'], wires: [] })!;
    expect(clip.components.map((c) => c.id)).toEqual(['R1', 'D1']);
    expect(clip.nets).toEqual({ A: ['R1.2', 'D1.A'] }); // GND reaches G1, IN has one pin
    expect(clip.wires).toHaveLength(1);
  });

  it('pastes under new ids and net names, offset', () => {
    const p = project();
    const clip = clipOf(p, { components: ['R1', 'D1'], wires: [] })!;
    const { project: q, ids } = withClip(p, clip, 40, 40, (c, used) => {
      const prefix = c.endsWith('resistor') ? 'R' : 'D';
      let n = 1;
      while (used.has(`${prefix}${n}`)) n++;
      return `${prefix}${n}`;
    });
    expect(ids).toEqual(['R2', 'D2']);
    expect(q.components.find((c) => c.id === 'R2')).toMatchObject({ params: { resistance: '1k' }, layout: { x: 40, y: 40 } });
    expect(q.nets.N1).toEqual(['R2.2', 'D2.A']);
    expect(q.nets.A).toEqual(['R1.2', 'D1.A']);
    expect(q.layout!.wires!.at(-1)).toEqual({ net: 'N1', points: [[100, 50], [140, 50]] });
  });
});

describe('editing guards', () => {
  it('rejects stale geometry and property drafts before any transaction', async () => {
    const { useProject } = await import('./project');
    const current = project();
    useProject.setState({ project: current, editable: true, busy: false });
    await expect(useProject.getState().reshapeWire(0, 0, 20, structuredClone(current))).rejects.toThrow('project changed');
    await expect(useProject.getState().setPartLayout('R1', { x: 10, y: 0 }, structuredClone(current))).rejects.toThrow('project changed');
    await expect(useProject.getState().setBatchParams(['R1'], { resistance: '2k' }, structuredClone(current))).rejects.toThrow('project changed');
    expect(useProject.getState().project).toBe(current);
    useProject.setState({ project: null });
  });
  it('blocks read-only and overlapping edits instead of diffing stale snapshots', async () => {
    const { useProject } = await import('./project');
    let invoked = false;
    const edit = (p: Project) => { invoked = true; return p; };
    useProject.setState({ project: project(), editable: false, busy: false });
    await expect(useProject.getState().mutate('test', edit)).rejects.toThrow('read-only');
    useProject.setState({ editable: true, busy: true });
    await expect(useProject.getState().mutate('test', edit)).rejects.toThrow('finishing');
    expect(invoked).toBe(false);
    useProject.setState({ project: null, busy: false });
  });
});

describe('authoritative net membership during geometry edits', () => {
  it('does not drag or remove a foreign wire merely touching a part pin', async () => {
    const { useProject } = await import('./project');
    const original = useProject.getState();
    const p: Project = { schema_version: 1, kind: 'project', name: 'coincident', components: [{ id: 'R1', component: 'resistor', layout: { x: 0, y: 0 } }, { id: 'R2', component: 'resistor', layout: { x: 200, y: 0 } }], nets: { REAL: ['R1.2'], FOREIGN: ['R2.1'] }, layout: { wires: [{ net: 'REAL', points: [[60, 10], [60, 50]] }, { net: 'FOREIGN', points: [[60, 10], [100, 10]] }] } };
    let after: Project | null = null;
    const symbol = { svg: '', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' as const }, '2': { x: 60, y: 10, side: 'right' as const } } };
    try {
      useProject.setState({ project: p, catalog: { resistor: { description: { symbol } as never, files: {} } }, mutate: async (_, fn) => { after = fn(structuredClone(p)); } });
      await useProject.getState().moveComponents(['R1'], 20, 0);
      expect((after! as Project).layout!.wires![0]!.points[0]).toEqual([80, 10]);
      expect((after! as Project).layout!.wires![1]).toEqual(p.layout!.wires![1]);
      await useProject.getState().deleteSelection({ components: ['R1'], wires: [] });
      expect((after! as Project).layout!.wires).toEqual([p.layout!.wires![1]]);
    } finally { useProject.setState(original); }
  });
});
