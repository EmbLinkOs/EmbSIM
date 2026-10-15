// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The demo project: the acceptance circuit of 01 §8 without its board. A
// push button through a pull-up drives an NPN transistor whose collector
// switches an LED through a series resistor. Without firmware in the loop the
// transistor follows the button: the LED is lit while the button is released
// and goes dark while it is held. Coordinates are on the 10-unit grid with
// rotation about the symbol centre (see packages/scene geometry).

import type { Project } from '../../types';

const DX = 100;
const DY = 60;
const P = (x: number, y: number): [number, number] => [x + DX, y + DY];

export function demoProject(): Project {
  return {
    schema_version: 1,
    kind: 'project',
    name: 'button-transistor-led',
    description: 'Push button through a pull-up switching an LED through an NPN transistor (demo session).',
    libraries: [],
    policy: 'electrical',
    boards: [],
    components: [
      { id: 'VCC1', component: 'emblink/vcc', params: { voltage: 3.3 }, layout: { x: 360 + DX, y: 0 + DY } },
      { id: 'R1', component: 'emblink/resistor', params: { resistance: '10k' }, layout: { x: 270 + DX, y: 60 + DY, rotation: 90 } },
      { id: 'SW1', component: 'emblink/push_button', params: { bounce: false }, layout: { x: 270 + DX, y: 240 + DY, rotation: 90 } },
      { id: 'GND1', component: 'emblink/gnd', layout: { x: 290 + DX, y: 300 + DY } },
      { id: 'R2', component: 'emblink/resistor', params: { resistance: '1k' }, layout: { x: 340 + DX, y: 200 + DY } },
      { id: 'Q1', component: 'emblink/bjt_npn', params: { part: '2N3904' }, layout: { x: 420 + DX, y: 180 + DY } },
      { id: 'GND2', component: 'emblink/gnd', layout: { x: 440 + DX, y: 250 + DY } },
      { id: 'R3', component: 'emblink/resistor', params: { resistance: 220 }, layout: { x: 420 + DX, y: 60 + DY, rotation: 90 } },
      { id: 'D1', component: 'emblink/led', params: { color: 'red' }, layout: { x: 420 + DX, y: 130 + DY, rotation: 90 } },
    ],
    nets: {
      '3V3': ['VCC1.+', 'R1.1', 'R3.1'],
      BTN: ['R1.2', 'SW1.1', 'R2.1'],
      GND: ['SW1.2', 'GND1.GND', 'Q1.E', 'GND2.GND'],
      Q1B: ['R2.2', 'Q1.B'],
      LED_K: ['R3.2', 'D1.A'],
      COLL: ['D1.K', 'Q1.C'],
    },
    layout: {
      wires: [
        { net: '3V3', points: [P(370, 20), P(370, 40)] },
        { net: '3V3', points: [P(300, 40), P(450, 40)] },
        { net: 'BTN', points: [P(300, 100), P(300, 220)] },
        { net: 'BTN', points: [P(300, 210), P(340, 210)] },
        { net: 'GND', points: [P(300, 280), P(300, 300)] },
        { net: 'GND', points: [P(450, 240), P(450, 250)] },
        { net: 'Q1B', points: [P(400, 210), P(420, 210)] },
        { net: 'LED_K', points: [P(450, 100), P(450, 110)] },
        { net: 'COLL', points: [P(450, 170), P(450, 180)] },
      ],
      labels: [
        { net: '3V3', x: 310 + DX, y: 32 + DY },
        { net: 'BTN', x: 308 + DX, y: 150 + DY },
        { net: 'COLL', x: 458 + DX, y: 178 + DY },
      ],
      sheet: { width: 1600, height: 1000 },
    },
    probes: [
      { id: 'coll', target: 'net:COLL', kind: 'analog', rate_hz: 200_000 },
      { id: 'btn', target: 'net:BTN', kind: 'digital', rate_hz: 200_000 },
      { id: 'ic', target: 'device:Q1.ic', kind: 'analog', rate_hz: 200_000 },
    ],
    scenarios: [],
    settings: { core_quantum_ps: '1000000000' },
  };
}
