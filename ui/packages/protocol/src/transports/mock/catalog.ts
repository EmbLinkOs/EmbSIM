// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The mock catalog: a handful of `emblink` components with inline symbol and
// artwork SVG following SPEC-005 §8 (10-unit grid, pins on grid points,
// colours through --symbol-stroke / --symbol-fill, hotspots and states).

import type { CatalogItem, ComponentDescription } from '../../types';

const STROKE = 'stroke="var(--symbol-stroke)" fill="none" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"';

const symbols: Record<string, string> = {
  resistor: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 20" width="60" height="20">
  <g ${STROKE}>
    <path d="M0 10 H12"/><path d="M48 10 H60"/>
    <rect x="12" y="4" width="36" height="12" fill="var(--symbol-fill)"/>
  </g>
</svg>`,
  capacitor: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 20" width="40" height="20">
  <g ${STROKE}>
    <path d="M0 10 H17"/><path d="M23 10 H40"/>
    <path d="M17 2 V18"/><path d="M23 2 V18"/>
  </g>
</svg>`,
  push_button: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 20" width="60" height="20">
  <g ${STROKE}>
    <path d="M0 10 H16"/><path d="M44 10 H60"/>
    <circle cx="18" cy="10" r="2" fill="var(--symbol-fill)"/><circle cx="42" cy="10" r="2" fill="var(--symbol-fill)"/>
    <path d="M16 4 H44"/>
    <path d="M30 4 V0"/><path d="M24 0 H36"/>
  </g>
</svg>`,
  led: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 20" width="60" height="20">
  <g ${STROKE}>
    <path d="M0 10 H20"/><path d="M40 10 H60"/>
    <path d="M20 2 L40 10 L20 18 Z" fill="var(--symbol-fill)"/>
    <path d="M40 2 V18"/>
    <path d="M30 0 L36 -6"/><path d="M34 -6 H36 V-4"/>
    <path d="M36 2 L42 -4"/><path d="M40 -4 H42 V-2"/>
  </g>
</svg>`,
  bjt_npn: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 60" width="60" height="60">
  <g ${STROKE}>
    <circle cx="32" cy="30" r="16"/>
    <path d="M0 30 H24"/><path d="M24 18 V42" stroke-width="2.5"/>
    <path d="M24 26 L40 16 V0"/>
    <path d="M24 34 L40 44 V60"/>
    <path d="M36 42 L40 44 L38 39 Z" fill="var(--symbol-stroke)"/>
    <path d="M30 0 H40"/><path d="M30 60 H40"/>
  </g>
</svg>`,
  vcc: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" width="20" height="20">
  <g ${STROKE}>
    <path d="M10 20 V8"/><path d="M2 8 H18"/>
    <path d="M6 2 L10 8 L14 2" fill="var(--symbol-fill)"/>
  </g>
</svg>`,
  gnd: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" width="20" height="20">
  <g ${STROKE}>
    <path d="M10 0 V8"/><path d="M1 8 H19"/><path d="M4 12 H16"/><path d="M7 16 H13"/>
  </g>
</svg>`,
};

const artworks: Record<string, string> = {
  push_button: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 80 80" width="80" height="80">
  <rect x="8" y="8" width="64" height="64" rx="6" fill="#1b1f27" stroke="#3a4150"/>
  <circle cx="18" cy="18" r="3" fill="#8a929f"/><circle cx="62" cy="18" r="3" fill="#8a929f"/>
  <circle cx="18" cy="62" r="3" fill="#8a929f"/><circle cx="62" cy="62" r="3" fill="#8a929f"/>
  <circle cx="40" cy="40" r="20" fill="#0d1017"/>
  <g id="cap"><circle cx="40" cy="40" r="17" fill="#2b2f3a" stroke="#4a5160"/><circle cx="40" cy="40" r="13" fill="#343947"/></g>
</svg>`,
  led: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 80" width="40" height="80">
  <path d="M14 56 V80" stroke="#8a929f" stroke-width="2"/><path d="M26 60 V80" stroke="#8a929f" stroke-width="2"/>
  <rect x="8" y="50" width="24" height="8" fill="#4a1414"/>
  <path d="M8 50 V28 A12 12 0 0 1 32 28 V50 Z" fill="#6a1a1a" stroke="#8a2a2a"/>
  <g id="glow" opacity="0"><path d="M8 50 V28 A12 12 0 0 1 32 28 V50 Z" fill="#ff5a5a"/><circle cx="20" cy="36" r="22" fill="#ff5a5a" opacity="0.35"/></g>
</svg>`,
  resistor: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 90 30" width="90" height="30">
  <path d="M0 15 H18"/><path d="M72 15 H90"/>
  <g stroke="#8a929f" stroke-width="2"><path d="M0 15 H18"/><path d="M72 15 H90"/></g>
  <rect x="18" y="6" width="54" height="18" rx="6" fill="#c9b28a"/>
  <rect x="28" y="6" width="5" height="18" fill="#7a4a1a"/><rect x="38" y="6" width="5" height="18" fill="#000"/>
  <rect x="48" y="6" width="5" height="18" fill="#d45a1a"/><rect x="62" y="6" width="5" height="18" fill="#c8a02a"/>
</svg>`,
  bjt_npn: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 50 70" width="50" height="70">
  <g stroke="#8a929f" stroke-width="2"><path d="M13 50 V70"/><path d="M25 50 V70"/><path d="M37 50 V70"/></g>
  <path d="M5 50 V22 A20 20 0 0 1 45 22 V50 Z" fill="#1b1f27" stroke="#3a4150"/>
  <rect x="5" y="38" width="40" height="12" fill="#11141a"/>
  <text x="25" y="32" font-family="monospace" font-size="8" fill="#c0c6d0" text-anchor="middle">2N3904</text>
</svg>`,
};

type Entry = { description: ComponentDescription; files: { symbol?: string; artwork?: string } };

function fid(functional: string, timed: string, electrical: string, omissions: string[] = []) {
  return {
    functional: { claims: functional, omissions },
    timed: { claims: timed, omissions },
    electrical: { claims: electrical, omissions, validation: [] as string[] },
  };
}

const provenance = { author: 'EmbSIM', license: 'Apache-2.0', sources: [] as { title: string; url?: string }[] };

export const MOCK_CATALOG: Record<string, Entry> = {
  'emblink/resistor': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/resistor',
      version: '1.0.0',
      name: 'Resistor',
      category: 'passives',
      description: 'Ideal resistor.',
      keywords: ['resistor', 'ohm', 'passive'],
      pins: [
        { name: '1', number: 1, model: { kind: 'passive' } },
        { name: '2', number: 2, model: { kind: 'passive' } },
      ],
      params: {
        resistance: { type: 'number', unit: 'ohm', default: 1000, min: 0.001, doc: 'Resistance' },
        tolerance: { type: 'number', unit: '%', default: 5, min: 0, max: 20, doc: 'Tolerance (informational)', advanced: true },
        power: { type: 'number', unit: 'W', default: 0.25, min: 0, doc: 'Rated power', advanced: true },
      },
      model: { primitive: 'resistor', bind: { r: 'resistance' } },
      functional: { primitive: 'wire' },
      symbol: { svg: 'symbol.svg', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' }, '2': { x: 60, y: 10, side: 'right' } } },
      artwork: { svg: 'artwork.svg', width: 90, height: 30 },
      fidelity: fid('A wire.', 'A wire; no timing.', 'Ohmic, temperature independent.', ['temperature coefficient', 'noise']),
      provenance,
    },
    files: { symbol: symbols['resistor']!, artwork: artworks['resistor']! },
  },
  'emblink/capacitor': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/capacitor',
      version: '1.0.0',
      name: 'Capacitor',
      category: 'passives',
      description: 'Ideal capacitor.',
      keywords: ['capacitor', 'farad'],
      pins: [
        { name: '1', number: 1, model: { kind: 'passive' } },
        { name: '2', number: 2, model: { kind: 'passive' } },
      ],
      params: { capacitance: { type: 'number', unit: 'F', default: 1e-7, min: 1e-15, doc: 'Capacitance' } },
      model: { primitive: 'capacitor', bind: { c: 'capacitance' } },
      symbol: { svg: 'symbol.svg', width: 40, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' }, '2': { x: 40, y: 10, side: 'right' } } },
      fidelity: { functional: null, timed: null, electrical: { claims: 'Linear capacitance.', omissions: ['ESR', 'leakage'] } },
      provenance,
    },
    files: { symbol: symbols['capacitor']! },
  },
  'emblink/push_button': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/push_button',
      version: '1.0.0',
      name: 'Push button',
      category: 'switches',
      description: 'Momentary tactile switch, normally open.',
      keywords: ['button', 'tactile', 'switch', 'momentary'],
      pins: [
        { name: '1', number: 1, model: { kind: 'passive' }, aliases: ['A'] },
        { name: '2', number: 2, model: { kind: 'passive' }, aliases: ['B'] },
      ],
      params: {
        ron: { type: 'number', unit: 'ohm', default: 0.05, min: 0, doc: 'Contact resistance when closed' },
        bounce: { type: 'boolean', default: false, doc: 'Model contact bounce on press and release' },
        bounce_ms: { type: 'number', unit: 'ms', default: 2, min: 0, max: 50, doc: 'Bounce burst length' },
      },
      inputs: [{ name: 'pressed', type: 'boolean', default: false, doc: 'Held while the pointer is down; toggled by keyboard' }],
      outputs: [{ name: 'closed', type: 'boolean' }],
      model: { primitive: 'switch', bind: { state: 'pressed', ron: 'ron' } },
      functional: { primitive: 'switch_digital' },
      symbol: { svg: 'symbol.svg', width: 60, height: 20, pins: { '1': { x: 0, y: 10, side: 'left' }, '2': { x: 60, y: 10, side: 'right' } } },
      artwork: {
        svg: 'artwork.svg',
        width: 80,
        height: 80,
        hotspots: [{ input: 'pressed', selector: '#cap', gesture: 'hold', label: 'Button cap' }],
        states: [{ output: 'closed', selector: '#cap', attr: 'transform', true: 'translate(0 2) scale(0.96)', false: '' }],
      },
      fidelity: fid(
        'Open or closed; no timing.',
        'Bounce as a burst of toggles at bounce_ms/8 spacing when enabled.',
        'Contact resistance ron when closed, 1 GΩ when open.',
        ['contact wear', 'capacitance'],
      ),
      provenance,
    },
    files: { symbol: symbols['push_button']!, artwork: artworks['push_button']! },
  },
  'emblink/led': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/led',
      version: '1.0.0',
      name: 'LED',
      category: 'optoelectronics',
      description: 'Light-emitting diode with luminance output.',
      keywords: ['led', 'diode', 'light'],
      pins: [
        { name: 'A', number: 1, model: { kind: 'passive' }, doc: 'Anode' },
        { name: 'K', number: 2, model: { kind: 'passive' }, doc: 'Cathode' },
      ],
      params: {
        color: { type: 'enum', enum: ['red', 'green', 'yellow', 'blue', 'white'], default: 'red', doc: 'Emission colour' },
        vf: { type: 'number', unit: 'V', default: 1.9, min: 1, max: 4, doc: 'Forward voltage at i_nom' },
        i_nom: { type: 'number', unit: 'A', default: 0.01, min: 0.0001, doc: 'Nominal current for full luminance' },
      },
      outputs: [
        { name: 'lit', type: 'boolean' },
        { name: 'luminance', type: 'number', doc: '0..1 relative to i_nom' },
      ],
      model: { primitive: 'diode_led', bind: { vf: 'vf', i_nom: 'i_nom' } },
      functional: { primitive: 'led_digital' },
      symbol: { svg: 'symbol.svg', width: 60, height: 20, pins: { A: { x: 0, y: 10, side: 'left' }, K: { x: 60, y: 10, side: 'right' } } },
      artwork: {
        svg: 'artwork.svg',
        width: 40,
        height: 80,
        states: [{ output: 'luminance', selector: '#glow', attr: 'opacity', scale: [0, 1] }],
      },
      fidelity: fid('On when forward biased.', 'On with a 1 µs rise.', 'Shockley diode with series resistance; luminance ∝ current.', ['thermal drift', 'spectrum']),
      provenance,
    },
    files: { symbol: symbols['led']!, artwork: artworks['led']! },
  },
  'emblink/bjt_npn': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/bjt_npn',
      version: '1.0.0',
      name: 'NPN transistor',
      category: 'semiconductors',
      description: 'Bipolar NPN transistor (Ebers-Moll).',
      keywords: ['transistor', 'npn', 'bjt', '2N3904'],
      pins: [
        { name: 'B', number: 1, model: { kind: 'passive' }, doc: 'Base' },
        { name: 'C', number: 2, model: { kind: 'passive' }, doc: 'Collector' },
        { name: 'E', number: 3, model: { kind: 'passive' }, doc: 'Emitter' },
      ],
      params: {
        part: { type: 'enum', enum: ['2N3904', 'BC547', '2N2222'], default: '2N3904', doc: 'Parameter set' },
        beta: { type: 'number', default: 150, min: 1, max: 1000, doc: 'Forward current gain' },
        is: { type: 'number', unit: 'A', default: 6.7e-15, advanced: true, doc: 'Saturation current' },
      },
      outputs: [{ name: 'ic', type: 'number', doc: 'Collector current, A' }],
      model: { primitive: 'bjt_npn', bind: { beta: 'beta', is: 'is' } },
      symbol: {
        svg: 'symbol.svg',
        width: 60,
        height: 60,
        pins: { B: { x: 0, y: 30, side: 'left' }, C: { x: 40, y: 0, side: 'top' }, E: { x: 40, y: 60, side: 'bottom' } },
      },
      artwork: { svg: 'artwork.svg', width: 50, height: 70 },
      fidelity: {
        functional: null,
        timed: null,
        electrical: { claims: 'Ebers-Moll DC model with beta and Is.', omissions: ['Early effect', 'junction capacitances'], range: { ic_a: [0, 0.2] } },
      },
      provenance: { ...provenance, sources: [{ title: '2N3904 datasheet, ON Semiconductor', url: 'https://www.onsemi.com/' }] },
    },
    files: { symbol: symbols['bjt_npn']!, artwork: artworks['bjt_npn']! },
  },
  'emblink/vcc': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/vcc',
      version: '1.0.0',
      name: 'Supply',
      category: 'power',
      description: 'Ideal DC supply referenced to ground.',
      keywords: ['vcc', 'supply', 'power', '3v3', '5v'],
      pins: [{ name: '+', number: 1, model: { kind: 'supply' }, doc: 'Positive terminal' }],
      params: { voltage: { type: 'number', unit: 'V', default: 3.3, min: 0, max: 48, doc: 'Nominal voltage' } },
      model: { primitive: 'vsource', bind: { v: 'voltage' } },
      functional: { primitive: 'driver_high' },
      symbol: { svg: 'symbol.svg', width: 20, height: 20, pins: { '+': { x: 10, y: 20, side: 'bottom' } } },
      fidelity: fid('Drives high.', 'Drives high.', 'Ideal voltage source.', ['source impedance']),
      provenance,
    },
    files: { symbol: symbols['vcc']! },
  },
  'emblink/gnd': {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/gnd',
      version: '1.0.0',
      name: 'Ground',
      category: 'power',
      description: 'The reference node.',
      keywords: ['gnd', 'ground', 'reference'],
      pins: [{ name: 'GND', number: 1, model: { kind: 'supply' } }],
      model: { primitive: 'ground' },
      functional: { primitive: 'driver_low' },
      symbol: { svg: 'symbol.svg', width: 20, height: 20, pins: { GND: { x: 10, y: 0, side: 'top' } } },
      fidelity: fid('Drives low.', 'Drives low.', 'The 0 V reference.'),
      provenance,
    },
    files: { symbol: symbols['gnd']! },
  },
};

/**
 * The 830-point breadboard (BB830), built from its geometry as
 * tools/catalog/gen_breadboard.py builds the catalog's: 0.1 in pitch,
 * strips of five across a 0.3 in channel, four rails of 50 in groups of five.
 */
function breadboard830(): Entry {
  const P = 2.54;
  const [w, d, h] = [165.1, 54.6, 8.5];
  const x0 = (w - 62 * P) / 2;
  const y0 = (d - 17 * P) / 2;
  const pins: Record<string, [number, number]> = {};
  const ties: string[][] = [];
  const r = (v: number) => Math.round(v * 1000) / 1000;
  for (let c = 1; c <= 63; c++) {
    const x = x0 + (c - 1) * P;
    [...'abcde'].forEach((row, k) => (pins[`${row}${c}`] = [r(x), r(y0 + (3 + k) * P)]));
    [...'fghij'].forEach((row, k) => (pins[`${row}${c}`] = [r(x), r(y0 + (10 + k) * P)]));
    ties.push([...'abcde'].map((row) => `${row}${c}`), [...'fghij'].map((row) => `${row}${c}`));
  }
  const rx0 = x0 + (62 * P - 58 * P) / 2;
  for (const [name, row] of [['TP', 0], ['TN', 1], ['BP', 16], ['BN', 17]] as const) {
    const names: string[] = [];
    for (let i = 0; i < 50; i++) {
      pins[`${name}${i + 1}`] = [r(rx0 + (Math.floor(i / 5) * 6 + (i % 5)) * P), r(y0 + row * P)];
      names.push(`${name}${i + 1}`);
    }
    ties.push(names);
  }
  const holes = Object.entries(pins).map(([n, [x, y]]) => `<rect id="pin-${n.toLowerCase()}" x="${r(x - 0.5)}" y="${r(y - 0.5)}" width="1" height="1" fill="#3a3a3a"/>`).join('');
  return {
    description: {
      schema_version: 1,
      kind: 'component',
      id: 'emblink/breadboard_830',
      version: '1.0.0',
      name: 'Breadboard, 830 points',
      category: 'connectors',
      description: 'Full-size solderless breadboard: 0.1 in pitch, strips of five holes either side of a 0.3 in channel, power rails along both edges.',
      keywords: ['breadboard', 'solderless'],
      pins: Object.keys(pins).map((name) => ({ name, model: { kind: 'passive' }, required: false })),
      model: { composite: { components: [], nets: {}, ports: {} } },
      symbol: { svg: 'symbol.svg', width: 60, height: 30, pins: {} },
      artwork: { svg: 'artwork.svg', width_mm: w, height_mm: d },
      ties,
      bench: { package: 'breadboard', size_mm: [w, d, h], wiring_aid: true, pins },
      fidelity: fid('Each strip and rail is one conductor.', 'Each strip and rail is one conductor.', 'Ideal contacts.', ['contact resistance']),
      provenance,
    },
    files: {
      symbol: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 30" width="60" height="30"><rect x="1" y="1" width="58" height="28" rx="2" fill="var(--symbol-fill)" stroke="var(--symbol-stroke)"/></svg>`,
      artwork: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${d}" width="${w}" height="${d}"><rect width="${w}" height="${d}" rx="1.6" fill="#f4f1ea"/>${holes}</svg>`,
    },
  };
}

MOCK_CATALOG['emblink/breadboard_830'] = breadboard830();

export function catalogItems(): CatalogItem[] {
  return Object.values(MOCK_CATALOG).map(({ description }) => ({
    id: description.id,
    version: description.version,
    name: description.name,
    category: description.category,
    fidelity_tiers: (['functional', 'timed', 'electrical'] as const).filter((t) => description.fidelity[t]),
  }));
}
