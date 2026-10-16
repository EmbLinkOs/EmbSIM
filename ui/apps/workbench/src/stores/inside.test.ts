// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { Project } from '@embsim/protocol';
import type { SceneCatalog } from '@embsim/scene';
import { describe, expect, it } from 'vitest';
import { insideSource, insideView } from './inside';

const resistor = { name: 'Resistor', svg: '<svg/>', symbol: { svg: 's', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' as const }, '2': { x: 60, y: 10, side: 'right' as const } } } };
const catalog: SceneCatalog = { 'emblink/resistor': resistor };

const board = {
  kind: 'board',
  name: 'Tiny board',
  mcu: { ref: 'st/stm32f103c8', instance: 'U1' },
  components: [{ id: 'R1', component: 'emblink/resistor' }],
  nets: { LED: ['U1.PC13', 'R1.1'], GND: ['R1.2', 'U1.VSS'] },
  ports: { C13: { net: 'LED' }, G: { net: 'GND' } },
};

const outer: Project = { schema_version: 1, kind: 'project', name: 'p', components: [], boards: [{ id: 'B1', board: 'x/tiny' }], nets: { OUT: ['B1.C13', 'D1.A'] } };

describe('inside view', () => {
  it('reads a board and a composite', () => {
    expect(insideSource(board)?.mcu).toEqual({ instance: 'U1', ref: 'st/stm32f103c8' });
    expect(insideSource({ model: { composite: { components: [], nets: {}, ports: { IN: 'I1.A' } } } })?.ports).toEqual({ IN: 'I1.A' });
    expect(insideSource({ kind: 'component', model: { primitive: 'resistor' } })).toBeNull();
  });

  it('draws the MCU from the pins used, labels every connected pin, and maps nets to the session', () => {
    const v = insideView('B1', insideSource(board)!, catalog, outer);
    expect(v.project.components.map((c) => c.id)).toEqual(['U1', 'R1']);
    expect(Object.keys(v.scene['mcu:st/stm32f103c8']!.symbol.pins).sort()).toEqual(['PC13', 'VSS']);
    expect(v.project.layout!.labels!.filter((l) => l.net === 'LED')).toHaveLength(2);
    // a port joined to a project net shows that net; the others are the board's own
    expect(v.alias.LED).toBe('OUT');
    expect(v.alias.GND).toBe('B1.GND');
  });
});
