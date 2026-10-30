// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A part's body on the bench (SPEC-005 §8.1): its package, its size and
// where each leg or pin is, in millimetres from its top-left corner seen
// from above. A description's own `bench` wins; otherwise the package
// follows the part as it is sold (a resistor's 0.4 in lead span, a TO-92's
// 0.1 in legs, a DIP's 0.3 in rows), so that the legs land on a
// breadboard's 2.54 mm holes as the real part's do.

import type { LedBody, BenchPackage, ComponentDescription } from '@embsim/protocol';

export const PITCH = 2.54;

export type ShapePackage = BenchPackage | 'virtual';

/** a physical component on a board's PCB (SPEC-006 §4): a socket, a chip, a button, an LED */
export interface BoardFeature {
  kind: 'usb_b' | 'usb_micro' | 'usb_c' | 'barrel_jack' | 'dip' | 'qfp' | 'qfn' | 'button' | 'crystal' | 'led' | 'rf_module' | 'box';
  at: [number, number];
  size?: [number, number, number];
  rotation?: number;
  pins?: number;
  label?: string;
  color?: string;
  input?: string;
  output?: string;
}

export interface BoardBench {
  thickness_mm?: number;
  standoff_mm?: number;
  headers?: 'female' | 'male' | 'none';
  features?: BoardFeature[];
  /** from the artwork: the PCB's outline (mm from its top-left corner), its soldermask colour, its plated holes */
  outline?: [number, number][];
  mask?: string;
  holes?: { at: [number, number]; r: number }[];
}

export interface BenchShape {
  package: ShapePackage;
  /** Geometry provenance, not electrical or mechanical qualification. */
  geometrySource?: 'catalog' | 'artwork' | 'approximate';
  /** a board's own components and build */
  board?: BoardBench;
  /** width (x), depth (y), height above the bench (z), mm */
  size: [number, number, number];
  pins: Record<string, [number, number]>;
  color?: string;
  label?: string;
  wiringAid?: boolean;
  led?: LedBody;
  /** the artwork is the body's top face (modules, boards) */
  textured?: boolean;
  /** a distance sensor's beam: from `at` along `dir`, mm, x right, y towards the viewer, z up */
  sense?: { at?: [number, number, number]; dir?: [number, number, number] };
}

export function isSurfaceMount(shape: BenchShape): boolean {
  return shape.package === 'sot23' || shape.package === 'sot223';
}

/** Representative lead thickness, not the PCB land pattern. */
export function surfaceLeadThickness(shape: BenchShape): number {
  return shape.package === 'sot223' ? 0.29 : 0.14;
}

/** Shared contact/body heights so visible sockets and wiring endpoints
 * cannot drift when catalog board thickness or mounting height changes. */
export function boardStack(shape: BenchShape): { standoff: number; thickness: number; surface: number; contact: number; headers: 'female' | 'male' | 'none' } {
  const standoff = shape.board?.standoff_mm ?? 3;
  const thickness = shape.board?.thickness_mm ?? 1.6;
  const headers = shape.board?.headers ?? 'female';
  const surface = standoff + thickness;
  return { standoff, thickness, surface, headers, contact: surface + (headers === 'female' ? 8.5 : headers === 'male' ? 8 : 0) };
}

type Desc = Pick<ComponentDescription, 'id' | 'name' | 'category' | 'pins' | 'bench' | 'artwork'> & { kind?: string; ports?: Record<string, unknown> };

/** parts with no body of their own: supply symbols and abstract stand-ins */
const VIRTUAL = new Set(['vcc', 'gnd', 'pull_up', 'pull_down', 'logic_level', 'test_point']);
/** instruments and sources the bench shows as boxes on the desk */
const BOXES: Record<string, { size: [number, number, number]; color: string; label?: string }> = {
  vsource: { size: [60, 40, 30], color: '#2d3436', label: 'DC SUPPLY' },
  isource: { size: [60, 40, 30], color: '#2d3436', label: 'CURRENT SOURCE' },
  battery: { size: [26, 17, 48], color: '#1e272e', label: '9V' },
  clock_source: { size: [50, 34, 24], color: '#34495e', label: 'CLOCK' },
  pulse_source: { size: [50, 34, 24], color: '#34495e', label: 'PULSE' },
  pattern_generator: { size: [70, 40, 26], color: '#34495e', label: 'PATTERN GEN' },
  logic_probe: { size: [12, 60, 12], color: '#c0392b', label: 'PROBE' },
  virtual_terminal: { size: [30, 18, 8], color: '#1e3799', label: 'USB-SERIAL' },
};

const AXIAL: Record<string, { span: number; body: [number, number]; color: string }> = {
  resistor: { span: 10.16, body: [6.3, 2.4], color: '#d9c6a0' },
  diode: { span: 10.16, body: [5.2, 2.7], color: '#1d1d1d' },
  diode_1n4007: { span: 10.16, body: [5.2, 2.7], color: '#1d1d1d' },
  diode_schottky: { span: 10.16, body: [5.2, 2.7], color: '#1d1d1d' },
  diode_1n4148: { span: 7.62, body: [3.8, 1.8], color: '#d35400' },
  zener: { span: 7.62, body: [3.8, 1.8], color: '#d35400' },
  bzx55c5v1: { span: 7.62, body: [3.8, 1.8], color: '#d35400' },
  inductor: { span: 10.16, body: [7, 3.2], color: '#2e86de' },
  ferrite_bead: { span: 10.16, body: [3.5, 3.5], color: '#4b4b4b' },
};

const RADIAL: Record<string, { span: number; dia: number; h: number; color: string }> = {
  capacitor: { span: 2.54, dia: 4.5, h: 6, color: '#e1a33c' },
  electrolytic_capacitor: { span: 2.54, dia: 5, h: 11, color: '#1f3a93' },
  crystal: { span: 5.08, dia: 4.6, h: 13, color: '#c8ccd0' },
  buzzer: { span: 7.62, dia: 12, h: 9.5, color: '#141414' },
  gl5528: { span: 2.54, dia: 5, h: 2.5, color: '#c0582a' },
  bpw34: { span: 5.08, dia: 5.4, h: 3.2, color: '#1b1b1b' },
  bpw34s: { span: 5.08, dia: 5.4, h: 3.2, color: '#1b1b1b' },
  ntcle100e3103jb0: { span: 2.54, dia: 3.3, h: 5, color: '#1b1b1b' },
  ntcle100e3104jb0: { span: 2.54, dia: 3.3, h: 5, color: '#1b1b1b' },
  ntcle100e3472jb0: { span: 2.54, dia: 3.3, h: 5, color: '#1b1b1b' },
};

const TO92 = new Set(['bc547b', 'bc557b', 'bjt_2n3904', 'bjt_2n3906', 'bjt_npn', 'bjt_pnp', 'mosfet_2n7000', 'nmos', 'pmos', 'ds18b20', 'lm35dz', 'tmp36gt9z']);
const TO220 = new Set(['l7805cv', 'l7809cv', 'l7812cv', 'regulator', 'regulator_78xx', 'irlz44n']);
const POTS: Record<string, { span: number; size: [number, number, number]; color: string }> = {
  potentiometer: { span: 5.08, size: [12.7, 12, 20], color: '#2b6cb0' },
  trimmer_potentiometer: { span: 2.54, size: [9.5, 4.8, 10], color: '#2457a6' },
  bourns_3296w_103: { span: 2.54, size: [9.5, 4.8, 10], color: '#2457a6' },
  bourns_3296w_104: { span: 2.54, size: [9.5, 4.8, 10], color: '#2457a6' },
};
/** modules: a PCB with its header along the bottom edge; the size of the common board */
const MODULES: Record<string, [number, number]> = {
  hc_sr04: [45, 20],
  dht22: [15.1, 25],
  ssd1306: [27.3, 27.8],
  lcd1602: [80, 36],
  lcd2004: [98, 60],
  mpu6050: [21, 16],
  bme280: [13, 10.5],
  bmp280: [15, 11.5],
  mfrc522: [60, 40],
  neo6m: [36, 25],
  hx711_load_cell: [34, 21],
  tm1637_display: [42, 24],
  max7219_display: [32, 32],
  keypad_4x4: [69, 77],
  pir_sensor: [32, 24],
  rotary_encoder: [26, 19],
  ds3231: [38, 22],
  ds1307: [27, 28],
  uln2003_module: [35, 32],
  servo: [23, 12.2],
  stepper_28byj48: [42, 28],
  ws2812b_strip: [60, 10],
  ws2812b: [9, 9],
  g5le_1_dc5: [22.5, 16.5],
  g5le_1_dc12: [22.5, 16.5],
  relay_contacts: [22.5, 16.5],
  a4988: [20.3, 15.2],
  tsop38238: [6, 5],
};
/** the soldermask the common breakout is sold in */
const MODULE_COLORS: Record<string, string> = {
  hc_sr04: '#1b4f9c',
  bme280: '#5b2a86',
  bmp280: '#5b2a86',
  mpu6050: '#1b4f9c',
  mfrc522: '#1b4f9c',
  neo6m: '#1b4f9c',
  hx711_load_cell: '#1e7a3a',
  ds3231: '#1b4f9c',
  ds1307: '#1b4f9c',
  ssd1306: '#1b4f9c',
  tm1637_display: '#1b4f9c',
  max7219_display: '#1e7a3a',
  pir_sensor: '#1e7a3a',
  rotary_encoder: '#1e7a3a',
  dht22: '#f2f2f2',
  keypad_4x4: '#1b1b1b',
  a4988: '#1e7a3a',
};

/** chips sold in DIP that the catalog describes as the chip itself */
const DIP_CHIPS = /^(ic_74hc|sn74hc|cd40|lm358|lm393|ne555|timer_555|mcp3008|mcp4921|eeprom_|pcf8574|l293d|uln2003$|vo617a|ds1307$)/;

function round(v: number): number {
  return Math.round(v * 1000) / 1000;
}

/** pins in their numbering order (`number`, else declaration order) */
function ordered(desc: Desc): string[] {
  return (desc.pins ?? [])
    .map((p, i) => ({ name: p.name, n: typeof p.number === 'number' ? p.number : i + 1 }))
    .sort((a, b) => a.n - b.n)
    .map((p) => p.name);
}

/** legs in a row, `span` apart (or one pitch), centred in a body `size` wide */
function inRow(names: string[], size: [number, number, number], span: number, y: number): Record<string, [number, number]> {
  const pins: Record<string, [number, number]> = {};
  const x0 = (size[0] - span * (names.length - 1)) / 2;
  names.forEach((n, i) => (pins[n] = [round(x0 + i * span), round(y)]));
  return pins;
}

/** a DIP's pins: 1 at the bottom left, counter-clockwise seen from above (notch to the left) */
export function dipPins(names: string[], rowSpacing: number): { size: [number, number, number]; pins: Record<string, [number, number]> } {
  const half = Math.ceil(names.length / 2);
  const w = half * PITCH + 1.2;
  const d = rowSpacing + 1.2;
  const pins: Record<string, [number, number]> = {};
  const x0 = (w - (half - 1) * PITCH) / 2;
  names.forEach((n, i) => {
    if (i < half) pins[n] = [round(x0 + i * PITCH), round(0.6 + rowSpacing)];
    else pins[n] = [round(x0 + (names.length - 1 - i) * PITCH), 0.6];
  });
  return { size: [round(w), round(d), 3.3], pins };
}

/** the `pin-<name>` anchors of an artwork, in mm (SPEC-005 §8) */
export function artworkAnchors(svg: string, widthMm: number, heightMm: number): Record<string, [number, number]> {
  const vb = /viewBox\s*=\s*"([\d.\-\s]+)"/.exec(svg)?.[1]?.trim().split(/\s+/).map(Number);
  if (!vb || vb.length !== 4 || !vb[2] || !vb[3]) return {};
  const sx = widthMm / vb[2];
  const sy = heightMm / vb[3];
  const out: Record<string, [number, number]> = {};
  const re = /<(rect|circle|ellipse|use)\b([^>]*\bid="pin-([^"]+)"[^>]*)>/g;
  const num = (attrs: string, k: string) => {
    const m = new RegExp(`\\b${k}="(-?[\\d.]+)"`).exec(attrs);
    return m ? Number(m[1]) : undefined;
  };
  for (let m = re.exec(svg); m; m = re.exec(svg)) {
    const [, tag, attrs = '', name = ''] = m;
    let x: number | undefined;
    let y: number | undefined;
    if (tag === 'rect') {
      x = (num(attrs, 'x') ?? 0) + (num(attrs, 'width') ?? 0) / 2;
      y = (num(attrs, 'y') ?? 0) + (num(attrs, 'height') ?? 0) / 2;
    } else {
      x = num(attrs, 'cx') ?? num(attrs, 'x');
      y = num(attrs, 'cy') ?? num(attrs, 'y');
    }
    if (x === undefined || y === undefined) continue;
    out[name] = [round((x - vb[0]!) * sx), round((y - vb[1]!) * sy)];
  }
  return out;
}

/**
 * What a board's artwork says of its PCB: the outline (the `pcb` path, or
 * the `#pcb` rectangle with its rounded corners), the soldermask's colour
 * (the `.pcb` rule's fill) and the plated mounting holes (`.mount` circles,
 * whose radius is the pad's), in mm from the top-left corner.
 */
export function artworkBoard(svg: string, widthMm: number, heightMm: number): Pick<BoardBench, 'outline' | 'mask' | 'holes'> {
  const vb = /viewBox\s*=\s*"([\d.\-\s]+)"/.exec(svg)?.[1]?.trim().split(/\s+/).map(Number);
  if (!vb || vb.length !== 4 || !vb[2] || !vb[3]) return {};
  const sx = widthMm / vb[2];
  const sy = heightMm / vb[3];
  const mm = (x: number, y: number): [number, number] => [round((x - vb[0]!) * sx), round((y - vb[1]!) * sy)];
  const out: Pick<BoardBench, 'outline' | 'mask' | 'holes'> = {};
  const fill = /\.pcb\s*\{[^}]*fill:\s*(#[0-9a-fA-F]{6})/.exec(svg)?.[1];
  if (fill) out.mask = fill;
  const path = /<path\b[^>]*class="pcb"[^>]*\bd="([^"]+)"/.exec(svg)?.[1] ?? /<path\b[^>]*\bd="([^"]+)"[^>]*class="pcb"/.exec(svg)?.[1];
  if (path) {
    // absolute M, L, H, V and Z, as the artworks draw outlines
    const pts: [number, number][] = [];
    let x = 0;
    let y = 0;
    const tokens = path.match(/[MLHVZ]|-?[\d.]+/gi) ?? [];
    let cmd = 'M';
    for (let i = 0; i < tokens.length; i++) {
      const t = tokens[i]!;
      if (/^[MLHVZ]$/i.test(t)) { cmd = t.toUpperCase(); continue; }
      if (cmd === 'H') x = Number(t);
      else if (cmd === 'V') y = Number(t);
      else { x = Number(t); y = Number(tokens[++i]); }
      pts.push(mm(x, y));
    }
    if (pts.length >= 3) out.outline = pts;
  } else {
    const rect = /<rect\b[^>]*\bid="pcb"[^>]*>/.exec(svg)?.[0];
    const num = (k: string) => Number(new RegExp(`\\b${k}="(-?[\\d.]+)"`).exec(rect ?? '')?.[1] ?? NaN);
    if (rect && Number.isFinite(num('width'))) {
      const [x0, y0, w, h] = [num('x') || 0, num('y') || 0, num('width'), num('height')];
      const r = Number.isFinite(num('rx')) ? num('rx') : 0;
      const pts: [number, number][] = [];
      const corner = (cx: number, cy: number, a0: number) => {
        for (let k = 0; k <= 6; k++) {
          const a = a0 + (k / 6) * (Math.PI / 2);
          pts.push(mm(cx + r * Math.cos(a), cy + r * Math.sin(a)));
        }
      };
      corner(x0 + w - r, y0 + r, -Math.PI / 2);
      corner(x0 + w - r, y0 + h - r, 0);
      corner(x0 + r, y0 + h - r, Math.PI / 2);
      corner(x0 + r, y0 + r, Math.PI);
      out.outline = pts;
    }
  }
  const holes: { at: [number, number]; r: number }[] = [];
  const re = /<circle\b[^>]*class="mount"[^>]*>/g;
  for (let m = re.exec(svg); m; m = re.exec(svg)) {
    const a = m[0];
    const cx = Number(/\bcx="(-?[\d.]+)"/.exec(a)?.[1]);
    const cy = Number(/\bcy="(-?[\d.]+)"/.exec(a)?.[1]);
    const r = Number(/\br="([\d.]+)"/.exec(a)?.[1]);
    if ([cx, cy, r].every(Number.isFinite)) holes.push({ at: mm(cx, cy), r: round(r * sx) });
  }
  if (holes.length) out.holes = holes;
  return out;
}

/** a description's pins by the anchor names (lower case) the artwork uses */
function fromAnchors(names: string[], anchors: Record<string, [number, number]>): Record<string, [number, number]> | null {
  const pins: Record<string, [number, number]> = {};
  for (const n of names) {
    const a = anchors[n.toLowerCase()];
    if (a) pins[n] = a;
  }
  return Object.keys(pins).length ? pins : null;
}

function header(names: string[], w: number, d: number): Record<string, [number, number]> {
  return inRow(names, [w, d, 0], PITCH, d - 1.6);
}

/**
 * The bench shape of a component or board. `artwork` is the artwork's SVG
 * text when the catalog has it: its pin anchors place a module's or a
 * board's pins.
 */
export function benchShape(desc: Desc, params?: Record<string, unknown>, artwork?: string): BenchShape {
  const shape = deriveShape(desc, params, artwork);
  const names = desc.kind === 'board' ? Object.keys(desc.ports ?? {}) : ordered(desc);
  const art = desc.artwork;
  const anchors = artwork && art?.width_mm && art.height_mm ? artworkAnchors(artwork, art.width_mm, art.height_mm) : {};
  shape.geometrySource = desc.bench?.pins && desc.bench.size_mm ? 'catalog'
    : shape.textured && names.length > 0 && names.every((name) => anchors[name.toLowerCase()]) ? 'artwork' : 'approximate';
  const sense = (desc.bench as { sense?: BenchShape['sense'] } | undefined)?.sense;
  if (sense) shape.sense = sense;
  return shape;
}

function deriveShape(desc: Desc, params?: Record<string, unknown>, artwork?: string): BenchShape {

  const short = desc.id.split('/').pop() ?? desc.id;
  const own = desc.bench;
  const names = ordered(desc);
  const art = desc.artwork;
  const artMm = art?.width_mm && art.height_mm ? ([art.width_mm, art.height_mm] as [number, number]) : undefined;
  const anchors = artwork && artMm ? artworkAnchors(artwork, artMm[0], artMm[1]) : {};

  if (own?.pins && own.size_mm) {
    return { package: own.package ?? 'box', size: own.size_mm, pins: own.pins, color: own.color, label: own.label, wiringAid: own.wiring_aid, led: own.led, textured: own.package === 'module' || own.package === 'board' || own.package === 'breadboard' };
  }
  if (desc.kind === 'board') {
    const size: [number, number, number] = artMm ? [artMm[0], artMm[1], 1.6] : [68.6, 53.3, 1.6];
    const ports = Object.keys(desc.ports ?? {});
    const pins = fromAnchors(ports, anchors) ?? header(ports, size[0], size[1]);
    const own = (desc as { bench?: BoardBench }).bench;
    const board = artwork && artMm ? { ...own, ...artworkBoard(artwork, artMm[0], artMm[1]) } : own;
    return { package: 'board', size, pins, textured: true, label: desc.name, board };
  }
  if (VIRTUAL.has(short)) return { package: 'virtual', size: [1, 1, 1], pins: {} };
  const box = BOXES[short];
  if (box) {
    // binding posts along the front, 0.3 in apart when they fit
    const spacing = names.length > 1 ? Math.min(7.62, (box.size[0] - 4) / (names.length - 1)) : 0;
    return { package: 'box', size: box.size, pins: inRow(names, box.size, spacing, box.size[1] - 4), color: box.color, label: box.label };
  }
  const ax = AXIAL[short];
  if (ax && names.length === 2) {
    const size: [number, number, number] = [ax.span, ax.body[1], ax.body[1]];
    return { package: 'axial', size, pins: inRow(names, size, ax.span, ax.body[1] / 2), color: ax.color };
  }
  if (short === 'led') {
    const size: [number, number, number] = [5.8, 5.8, 8.6];
    // the anode (the longer leg) first, the cathode by the flat
    const order = ['A', 'K'].filter((n) => names.includes(n));
    return { package: 'led', size, pins: inRow(order.length === 2 ? order : names, size, PITCH, 2.9) };
  }
  if (short === 'rgb_led') {
    const size: [number, number, number] = [8, 5.8, 8.6];
    return { package: 'led', size, pins: inRow(names, size, PITCH, 2.9), color: '#f4f6ff' };
  }
  const rad = RADIAL[short];
  if (rad && names.length === 2) {
    const size: [number, number, number] = [Math.max(rad.dia, rad.span + 1), rad.dia, rad.h];
    return { package: 'radial', size, pins: inRow(names, size, rad.span, rad.dia / 2), color: rad.color };
  }
  if (TO92.has(short) && names.length === 3) {
    const size: [number, number, number] = [5.2, 4.2, 5.2];
    return { package: 'to92', size, pins: inRow(names, size, PITCH, 2.1), label: desc.name.split(' ')[0] };
  }
  if (TO220.has(short) && names.length === 3) {
    const size: [number, number, number] = [10.2, 4.6, 15.6];
    return { package: 'to220', size, pins: inRow(names, size, PITCH, 3.4), label: desc.name.split(' ')[0] };
  }
  const pot = POTS[short];
  if (pot) return { package: 'pot', size: pot.size, pins: inRow(names, pot.size, pot.span, pot.size[1] - 1.5), color: pot.color };
  if (short === 'd2f_01l') {
    // Omron's D2F subminiature switch with its lever: three PCB terminals 0.2 in apart
    const size: [number, number, number] = [12.8, 5.8, 6.5];
    return { package: 'switch', size, pins: inRow(names, size, 5.08, 4.3), color: '#1b1b1b', label: 'D2F' };
  }
  if (short === 'push_button' || short === 'b3f_1000') {
    // a 6 x 6 mm tactile switch; two legs straddle a strip pair as its pairs do
    const size: [number, number, number] = [7.62, 6, 5];
    const pins = names.length === 4
      ? { [names[0]!]: [0, 0.5], [names[1]!]: [7.62, 0.5], [names[2]!]: [0, 5.58], [names[3]!]: [7.62, 5.58] } as Record<string, [number, number]>
      : inRow(names, size, PITCH * 3, 3);
    return { package: 'tactile', size, pins };
  }
  if (short === 'toggle_switch') {
    const size: [number, number, number] = [8.6, 4.4, 10];
    return { package: 'switch', size, pins: inRow(names, size, PITCH, 2.2) };
  }
  if (short === 'header') {
    const n = Math.max(1, Math.min(names.length, Number(params?.pins ?? names.length) || names.length));
    const used = names.slice(0, n);
    const size: [number, number, number] = [n * PITCH, PITCH, 8.5];
    return { package: 'module', size, pins: inRow(used, size, PITCH, PITCH / 2), color: '#1b1b1b' };
  }
  if (short === 'a4988') {
    // Pololu's carrier: two rows of eight, 0.5 in apart, on a 0.6 x 0.8 in board
    const { size, pins } = dipPins(names, 12.7);
    return { package: 'module', size: [Math.max(size[0], 20.3), Math.max(size[1], 15.2), 1.6], pins: Object.fromEntries(Object.entries(pins).map(([n, [x, y]]) => [n, [x, y + (15.2 - size[1]) / 2]])), color: '#1e7a3a', label: 'A4988' };
  }
  if (DIP_CHIPS.test(short) || ((desc.category === 'logic' || desc.category === 'analog' || desc.category === 'timers' || desc.category === 'amplifiers' || desc.category === 'comparators') && names.length >= 8 && names.length % 2 === 0 && !MODULES[short])) {
    const wide = names.length >= 28 && !/328/.test(short);
    const { size, pins } = dipPins(names, wide ? 15.24 : 7.62);
    return { package: 'dip', size, pins, label: desc.name.split(' ')[0] };
  }
  if (short === 'seven_segment' || short === 'led_bargraph') {
    const { size, pins } = dipPins(names, 15.24);
    return { package: 'module', size: [Math.max(size[0], 12.7), size[1], 8], pins, color: '#1b1b1b', label: desc.name };
  }
  if (short === 'hc_sr04') {
    // stood upright in a breadboard, as it is used: the 45 x 20 mm board edge-on,
    // its header along the bottom, the transducers facing the back of the bench
    const size: [number, number, number] = [45, 5, 21];
    return { package: 'module', size, pins: inRow(names, size, PITCH, 2.5), color: '#1b4f9c', label: desc.name };
  }
  const mod = MODULES[short];
  if (!mod && (desc.category === 'logic' || desc.category === 'analog' || desc.category === 'comparators')) {
    // an abstract gate or block: a labelled box, its pins in a row
    const size: [number, number, number] = [Math.max(10, names.length * PITCH + 2), 7, 4];
    return { package: 'box', size, pins: inRow(names, size, PITCH, size[1] - 1.2), color: '#2f3640', label: desc.name };
  }
  if (mod || names.length > 3) {
    const [w0, d] = artMm ?? mod ?? [Math.max(20, names.length * PITCH + 4), 20];
    // a header longer than the board: the board is as long as its header
    const w = Math.max(w0, names.length * PITCH + 1);
    const pins = fromAnchors(names, anchors) ?? header(names, w, d);
    return { package: 'module', size: [w, d, 1.6], pins, textured: Boolean(artwork), color: MODULE_COLORS[short] ?? '#1f6f43', label: desc.name };
  }
  // anything else: a small box with its legs in a row
  const size: [number, number, number] = [Math.max(5, names.length * PITCH + 1), 5, 5];
  return { package: names.length <= 1 ? 'virtual' : 'box', size, pins: inRow(names, size, PITCH, 2.5), color: '#3d3d3d', label: desc.name };
}
