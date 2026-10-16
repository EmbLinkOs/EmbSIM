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
