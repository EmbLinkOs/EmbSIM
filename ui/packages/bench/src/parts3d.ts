// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bodies the 3D bench builds for each package (ADR-027), in
// millimetres, from the parts as they are made: a carbon-film resistor's
// beige body with its colour code read from its resistance, a 5 mm LED's
// epoxy dome with its flat by the cathode, a TO-92's moulded half-round, a
// DIP's body with its notch and its bent legs, a breadboard's white body
// with its holes, modules and boards as their PCBs with their headers. The
// group's origin is the part's centre on the surface it stands on; +y is up.

import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js';
import { boardStack, type BenchShape, type BoardFeature } from './shape';

export interface PartLook {
  /** the component's id ("emblink/resistor") */
  component: string;
  params?: Record<string, unknown>;
  /** the artwork's live picture for the top face, when it has one */
  texture?: THREE.Texture;
}

/** objects a pointer can act on carry what they are in userData */
export interface PickData {
  part?: string;
  /** a board's component the input belongs to ("SW_RESET") */
  target?: string;
  /** an input the object drives ("pressed" on a button's cap) */
  input?: string;
  gesture?: 'hold' | 'toggle' | 'turn';
}

const METAL = new THREE.MeshStandardMaterial({ color: 0xc9ccd1, metalness: 0.9, roughness: 0.32 });
const GOLD = new THREE.MeshStandardMaterial({ color: 0xd4af37, metalness: 0.95, roughness: 0.28 });
const BLACK_PLASTIC = new THREE.MeshStandardMaterial({ color: 0x151515, roughness: 0.55 });
const EPOXY = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.42 });

const COLOR_CODE = ['#111111', '#7b3f00', '#d63031', '#f39c12', '#f1c40f', '#27ae60', '#2e86de', '#8e44ad', '#808080', '#f5f5f5'];
const GOLD_BAND = '#c9a227';
const SILVER_BAND = '#c0c0c0';

const SI: Record<string, number> = { p: 1e-12, n: 1e-9, u: 1e-6, µ: 1e-6, m: 1e-3, '': 1, k: 1e3, K: 1e3, M: 1e6, G: 1e9 };

/** a value as the catalog writes it ("4.7k", "220", 1000, "1M") */
export function parseValue(v: unknown): number | undefined {
  if (typeof v === 'number') return v;
  if (typeof v !== 'string') return undefined;
  const m = /^\s*([\d.]+)\s*([pnuµmkKMG]?)/.exec(v);
  if (!m) return undefined;
  return Number(m[1]) * (SI[m[2] ?? ''] ?? 1);
}

/** a resistor's four bands (two digits, the multiplier, 5 % gold) */
export function resistorBands(ohms: number): string[] {
  if (!(ohms > 0)) return [COLOR_CODE[0]!, COLOR_CODE[0]!, COLOR_CODE[0]!, GOLD_BAND];
  let exp = Math.floor(Math.log10(ohms)) - 1;
  let digits = Math.round(ohms / 10 ** exp);
  if (digits >= 100) {
    digits = Math.round(digits / 10);
    exp += 1;
  }
  const mult = exp === -1 ? GOLD_BAND : exp === -2 ? SILVER_BAND : COLOR_CODE[Math.max(0, Math.min(9, exp))]!;
  return [COLOR_CODE[Math.floor(digits / 10)]!, COLOR_CODE[digits % 10]!, mult, GOLD_BAND];
}

const LED_COLORS: Record<string, string> = { red: '#ff2a2a', green: '#20e060', blue: '#3070ff', yellow: '#ffd400', white: '#f8f8ff', orange: '#ff8a00' };

export function ledColor(params?: Record<string, unknown>): string {
  const c = params?.color;
  return (typeof c === 'string' && LED_COLORS[c]) || LED_COLORS.red!;
}

function labelTexture(text: string, w: number, h: number, color = '#e8e8e8', background?: string): THREE.CanvasTexture | undefined {
  if (typeof document === 'undefined') return undefined;
  const canvas = document.createElement('canvas');
  const scale = 16;
  canvas.width = Math.max(16, Math.round(w * scale));
  canvas.height = Math.max(16, Math.round(h * scale));
  const g = canvas.getContext('2d');
  if (!g) return undefined;
  if (background) {
    g.fillStyle = background;
    g.fillRect(0, 0, canvas.width, canvas.height);
  }
  g.fillStyle = color;
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  let size = canvas.height * 0.55;
  g.font = `600 ${size}px "Inter", "Helvetica", sans-serif`;
  while (g.measureText(text).width > canvas.width * 0.9 && size > 6) {
    size -= 2;
    g.font = `600 ${size}px "Inter", "Helvetica", sans-serif`;
  }
  g.fillText(text, canvas.width / 2, canvas.height / 2);
  const t = new THREE.CanvasTexture(canvas);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}

function mesh(geometry: THREE.BufferGeometry, material: THREE.Material | THREE.Material[], shadows = true): THREE.Mesh {
  const m = new THREE.Mesh(geometry, material);
  m.castShadow = shadows;
  m.receiveShadow = true;
  return m;
}

function standard(color: string, roughness = 0.5, metalness = 0): THREE.MeshStandardMaterial {
  return new THREE.MeshStandardMaterial({ color: new THREE.Color(color), roughness, metalness });
}

/** a straight leg from `top` down to `bottom` (y), at (x, z) */
function leg(x: number, z: number, top: number, bottom: number, radius = 0.25, material = METAL): THREE.Mesh {
  const h = Math.max(0.1, top - bottom);
  const m = mesh(new THREE.CylinderGeometry(radius, radius, h, 8), material);
  m.position.set(x, bottom + h / 2, z);
  return m;
}

/** a bent lead: along a path of points */
function lead(points: THREE.Vector3[], radius = 0.25): THREE.Mesh {
  const curve = new THREE.CatmullRomCurve3(points, false, 'catmullrom', 0.05);
  return mesh(new THREE.TubeGeometry(curve, 24, radius, 6, false), METAL);
}

/** pins relative to the group's origin (the part's centre) */
function local(shape: BenchShape): Record<string, [number, number]> {
  const [w, d] = shape.size;
  const out: Record<string, [number, number]> = {};
  for (const [n, [x, y]] of Object.entries(shape.pins)) out[n] = [x - w / 2, y - d / 2];
  return out;
}

function breadboard(shape: BenchShape, look: PartLook): THREE.Group {
  const g = new THREE.Group();
  const [w, d, h] = shape.size;
  const top = look.texture
    ? new THREE.MeshStandardMaterial({ map: look.texture, roughness: 0.62 })
    : standard('#f4f1ea', 0.62);
  const side = standard('#ece8de', 0.66);
  const body = mesh(new RoundedBoxGeometry(w, h, d, 2, 0.8), [side, side, top, side, side, side]);
  body.position.y = h / 2;
  g.add(body);
  return g;
}

function axial(shape: BenchShape, look: PartLook): THREE.Group {
  const g = new THREE.Group();
  const pins = Object.values(local(shape));
  const short = look.component.split('/').pop() ?? '';
  const isResistor = short === 'resistor';
  const glass = short === 'diode_1n4148' || short === 'zener' || short === 'bzx55c5v1';
  const bodyLen = isResistor ? 6.3 : glass ? 3.8 : short === 'inductor' ? 7 : 5.2;
  const r = isResistor ? 1.2 : glass ? 0.9 : short === 'ferrite_bead' ? 1.75 : 1.35;
  const lift = 2.6;
  const y = lift + r;
  // the body: a resistor's dog-bone, a diode's cylinder
  let profile: THREE.Vector2[];
  if (isResistor) {
    const L = bodyLen / 2;
    profile = [
      new THREE.Vector2(0, -L), new THREE.Vector2(r * 0.9, -L + 0.05), new THREE.Vector2(r, -L + 0.5), new THREE.Vector2(r * 0.86, -L + 1.3),
      new THREE.Vector2(r * 0.86, L - 1.3), new THREE.Vector2(r, L - 0.5), new THREE.Vector2(r * 0.9, L - 0.05), new THREE.Vector2(0, L),
    ];
  } else {
    profile = [new THREE.Vector2(0, -bodyLen / 2), new THREE.Vector2(r, -bodyLen / 2 + 0.2), new THREE.Vector2(r, bodyLen / 2 - 0.2), new THREE.Vector2(0, bodyLen / 2)];
  }
  const bodyMat = glass
    ? new THREE.MeshStandardMaterial({ color: 0xff8a3d, roughness: 0.12, transparent: true, opacity: 0.8 })
    : standard(shape.color ?? '#d9c6a0', isResistor ? 0.55 : 0.4);
  const body = mesh(new THREE.LatheGeometry(profile, 24), bodyMat);
  body.rotation.z = Math.PI / 2;
  body.position.y = y;
  g.add(body);
  if (isResistor) {
    const bands = resistorBands(parseValue(look.params?.resistance) ?? 1000);
    const xs = [-1.9, -1.1, -0.3, 1.6];
    bands.forEach((c, i) => {
      const ring = mesh(new THREE.CylinderGeometry(r * 0.9 + 0.03, r * 0.9 + 0.03, 0.45, 20), standard(c, 0.45, c === GOLD_BAND ? 0.7 : 0));
      ring.rotation.z = Math.PI / 2;
      ring.position.set(xs[i]!, y, 0);
      g.add(ring);
    });
  } else if (short !== 'inductor' && short !== 'ferrite_bead') {
    // the cathode band, by the second pin (K)
    const band = mesh(new THREE.CylinderGeometry(r + 0.02, r + 0.02, 0.6, 20), standard(glass ? '#111111' : '#d0d0d0', 0.4));
    band.rotation.z = Math.PI / 2;
    band.position.set(bodyLen / 2 - 0.7, y, 0);
    g.add(band);
  }
  // the leads: out of the body's ends, bent down into the holes
  for (const [px, pz] of pins) {
    const end = Math.sign(px) * (bodyLen / 2);
    g.add(lead([new THREE.Vector3(end, y, pz), new THREE.Vector3(px - Math.sign(px) * 0.6, y, pz), new THREE.Vector3(px, y - 0.6, pz), new THREE.Vector3(px, -2.5, pz)]));
  }
  return g;
}

let haloTexture: THREE.Texture | undefined;

/** a soft round glow, white at the centre */
function halo(): THREE.Texture | undefined {
  if (haloTexture || typeof document === 'undefined') return haloTexture;
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  if (!g) return undefined;
  const grad = g.createRadialGradient(64, 64, 0, 64, 64, 64);
  grad.addColorStop(0, 'rgba(255,255,255,1)');
  grad.addColorStop(0.25, 'rgba(255,255,255,0.55)');
  grad.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = grad;
  g.fillRect(0, 0, 128, 128);
  haloTexture = new THREE.CanvasTexture(c);
  return haloTexture;
}

function led(shape: BenchShape, look: PartLook): { group: THREE.Group; glow: THREE.MeshStandardMaterial; light?: THREE.PointLight; halo?: THREE.Sprite } {
  const g = new THREE.Group();
  const color = new THREE.Color(shape.color ?? ledColor(look.params));
  const pins = local(shape);
  const r = 2.5;
  const base = 4.2;
  // the dome: a cylinder with a hemisphere on top, and the rim with its flat
  const profile: THREE.Vector2[] = [new THREE.Vector2(0, 0), new THREE.Vector2(r + 0.4, 0), new THREE.Vector2(r + 0.4, 1), new THREE.Vector2(r, 1)];
  for (let i = 0; i <= 12; i++) {
    const a = (i / 12) * (Math.PI / 2);
    profile.push(new THREE.Vector2(r * Math.cos(a), 1 + 4.6 + r * Math.sin(a) * 0.95));
  }
  // tinted, diffused epoxy: the colour of the die when off, glowing from it when lit
  const glow = new THREE.MeshStandardMaterial({
    color: color.clone().multiplyScalar(0.55),
    emissive: color,
    emissiveIntensity: 0,
    roughness: 0.22,
    metalness: 0,
    transparent: true,
    opacity: 0.88,
  });
  const dome = mesh(new THREE.LatheGeometry(profile, 32), glow);
  dome.position.y = base;
  g.add(dome);
  // the legs: the anode longer, straight down
  for (const [name, [px, pz]] of Object.entries(pins)) g.add(leg(px, pz, base + 0.5, name === 'A' ? -3.5 : -2.5, 0.25));
  let light: THREE.PointLight | undefined;
  if (look.component.endsWith('/led')) {
    light = new THREE.PointLight(color, 0, 60, 2);
    light.position.y = base + 6;
    g.add(light);
  }
  // the glow a camera sees round a lit LED
  const tex = halo();
  let sprite: THREE.Sprite | undefined;
  if (tex) {
    sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, color, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true, opacity: 0 }));
    sprite.scale.set(16, 16, 1);
    sprite.position.y = base + 5;
    sprite.visible = false;
    g.add(sprite);
  }
  return { group: g, glow, light, halo: sprite };
}

function radial(shape: BenchShape, look: PartLook): THREE.Group {
  const g = new THREE.Group();
  const short = look.component.split('/').pop() ?? '';
  const [, d, h] = shape.size;
  const pins = Object.values(local(shape));
  const lift = 1.5;
  if (short === 'capacitor') {
    // a ceramic disc, dipped
    const disc = mesh(new THREE.SphereGeometry(d / 2, 24, 12), standard(shape.color ?? '#e1a33c', 0.6));
    disc.scale.set(1, 1.1, 0.45);
    disc.position.y = lift + h / 2;
    g.add(disc);
  } else if (short === 'electrolytic_capacitor') {
    const can = mesh(new THREE.CylinderGeometry(d / 2, d / 2, h, 32), standard(shape.color ?? '#1f3a93', 0.35));
    can.position.y = lift + h / 2;
    g.add(can);
    const stripe = mesh(new THREE.CylinderGeometry(d / 2 + 0.02, d / 2 + 0.02, h * 0.98, 32, 1, true, Math.PI * 0.85, Math.PI * 0.3), standard('#c8d0dc', 0.4));
    stripe.position.y = lift + h / 2;
    g.add(stripe);
    const lid = mesh(new THREE.CylinderGeometry(d / 2 - 0.3, d / 2 - 0.3, 0.1, 32), METAL);
    lid.position.y = lift + h + 0.02;
    g.add(lid);
  } else if (short === 'crystal') {
    const can = mesh(new RoundedBoxGeometry(11, h, d, 2, d / 2 - 0.1), METAL);
    can.position.y = lift + h / 2;
    g.add(can);
  } else if (short === 'buzzer') {
    const can = mesh(new THREE.CylinderGeometry(d / 2, d / 2, h, 40), standard('#141414', 0.5));
    can.position.y = lift + h / 2;
    g.add(can);
    const hole = mesh(new THREE.CylinderGeometry(1.2, 1.2, 0.2, 16), standard('#000000', 0.9));
    hole.position.y = lift + h + 0.05;
    g.add(hole);
  } else {
    // an LDR, a thermistor, a photodiode: a small disc on two legs
    const disc = mesh(new THREE.CylinderGeometry(d / 2, d / 2, Math.min(2.5, h), 24), standard(shape.color ?? '#c0582a', 0.35));
    disc.rotation.x = Math.PI / 2;
    disc.position.y = lift + d / 2;
    g.add(disc);
  }
  for (const [px, pz] of pins) g.add(leg(px, pz, lift + 0.4, -2.5));
  return g;
}

function to92(shape: BenchShape, look: PartLook): THREE.Group {
  const g = new THREE.Group();
  const [w, d, h] = shape.size;
  const flat = new THREE.Shape();
  flat.moveTo(-w / 2, -d / 4);
  flat.lineTo(w / 2, -d / 4);
  flat.absarc(0, -d / 4, w / 2, 0, Math.PI, false);
  const geom = new THREE.ExtrudeGeometry(flat, { depth: h, bevelEnabled: true, bevelSize: 0.15, bevelThickness: 0.15, bevelSegments: 2 });
  geom.rotateX(-Math.PI / 2);
  const body = mesh(geom, EPOXY);
  body.position.y = 3;
  g.add(body);
  const tex = labelTexture(shape.label ?? '', w * 0.9, h * 0.35);
  if (tex) {
    const plate = new THREE.Mesh(new THREE.PlaneGeometry(w * 0.9, h * 0.35), new THREE.MeshBasicMaterial({ map: tex, transparent: true }));
    plate.position.set(0, 3 + h * 0.62, d / 4 + 0.17);
    g.add(plate);
  }
  for (const [px, pz] of Object.values(local(shape))) g.add(leg(px, pz, 3.1, -2.5, 0.22));
  void look;
  return g;
}

function to220(shape: BenchShape): THREE.Group {
  const g = new THREE.Group();
  const [w, , h] = shape.size;
  const bodyH = 9;
  const body = mesh(new THREE.BoxGeometry(w, bodyH, 4.4), EPOXY);
  body.position.set(0, 4 + bodyH / 2, 0.3);
  g.add(body);
  const tab = new THREE.Shape();
  tab.moveTo(-w / 2, 0);
  tab.lineTo(w / 2, 0);
  tab.lineTo(w / 2, h - bodyH);
  tab.lineTo(-w / 2, h - bodyH);
  tab.lineTo(-w / 2, 0);
  const hole = new THREE.Path();
  hole.absarc(0, (h - bodyH) / 2, 1.8, 0, Math.PI * 2, true);
  tab.holes.push(hole);
  const tabGeom = new THREE.ExtrudeGeometry(tab, { depth: 1.3, bevelEnabled: false });
  const tabMesh = mesh(tabGeom, METAL);
  tabMesh.position.set(0, 4 + bodyH, -1.9);
  g.add(tabMesh);
  const tex = labelTexture(shape.label ?? '', w * 0.9, 2.4);
  if (tex) {
    const plate = new THREE.Mesh(new THREE.PlaneGeometry(w * 0.9, 2.4), new THREE.MeshBasicMaterial({ map: tex, transparent: true }));
    plate.position.set(0, 4 + bodyH * 0.6, 2.52);
    g.add(plate);
  }
  for (const [px, pz] of Object.values(local(shape))) {
    const pin = mesh(new THREE.BoxGeometry(0.8, 6.5, 0.5), METAL);
    pin.position.set(px, 4 - 3.25 + 0.75, pz);
    g.add(pin);
  }
  return g;
}

function dip(shape: BenchShape): THREE.Group {
  const g = new THREE.Group();
  const [w, d] = shape.size;
  const pins = Object.values(local(shape));
  const rows = [...new Set(pins.map(([, z]) => z))];
  const span = rows.length > 1 ? Math.abs(rows[0]! - rows[1]!) : 7.62;
  const bodyD = span - 1.3;
  const bodyH = 3.3;
  const lift = 0.8;
  const body = mesh(new RoundedBoxGeometry(w - 0.6, bodyH, bodyD, 2, 0.35), EPOXY);
  body.position.y = lift + bodyH / 2;
  g.add(body);
  // the notch by pin 1, and the dimple
  const notch = mesh(new THREE.CylinderGeometry(0.9, 0.9, 0.2, 20, 1, false, 0, Math.PI), standard('#050505', 0.8));
  notch.rotation.y = Math.PI / 2;
  notch.position.set(-w / 2 + 0.3, lift + bodyH + 0.01, 0);
  g.add(notch);
  const tex = labelTexture(shape.label ?? '', Math.min(w * 0.8, 30), Math.min(bodyD * 0.4, 2.6), '#d0d0d0');
  if (tex) {
    const plate = new THREE.Mesh(new THREE.PlaneGeometry(Math.min(w * 0.8, 30), Math.min(bodyD * 0.4, 2.6)), new THREE.MeshBasicMaterial({ map: tex, transparent: true }));
    plate.rotation.x = -Math.PI / 2;
    plate.position.y = lift + bodyH + 0.02;
    g.add(plate);
  }
  // gull legs: out of the body's side, down into the holes
  for (const [px, pz] of pins) {
    const out = Math.sign(pz) * (bodyD / 2);
    const shoulder = mesh(new THREE.BoxGeometry(1.2, 0.25, Math.abs(pz - out) + 0.2), METAL);
    shoulder.position.set(px, lift + 1.4, (pz + out) / 2);
    g.add(shoulder);
    const down = mesh(new THREE.BoxGeometry(0.5, lift + 1.4 + 3, 0.25), METAL);
    down.position.set(px, (lift + 1.4 - 3) / 2, pz);
    g.add(down);
  }
  void d;
  return g;
}

function tactile(shape: BenchShape, look: PartLook, id: string): { group: THREE.Group; cap: THREE.Object3D } {
  const g = new THREE.Group();
  const base = mesh(new RoundedBoxGeometry(6, 3.4, 6, 2, 0.3), standard('#2d2d2d', 0.6));
  base.position.y = 1.7 + 0.3;
  g.add(base);
  const plate = mesh(new THREE.BoxGeometry(6.1, 0.3, 6.1), METAL);
  plate.position.y = 3.55;
  g.add(plate);
  const cap = mesh(new THREE.CylinderGeometry(1.75, 1.75, 1.5, 24), standard(look.params?.color === 'red' ? '#c0392b' : '#1a1a1a', 0.4));
  cap.position.y = 4.45;
  cap.userData = { part: id, input: 'pressed', gesture: 'hold' } satisfies PickData;
  g.add(cap);
  for (const [px, pz] of Object.values(local(shape))) g.add(leg(px, pz, 0.5, -2.5, 0.3));
  return { group: g, cap };
}

function toggle(shape: BenchShape, id: string): { group: THREE.Group; lever: THREE.Object3D } {
  const g = new THREE.Group();
  const [w, d, h] = shape.size;
  const body = mesh(new RoundedBoxGeometry(w, h * 0.45, d, 2, 0.4), standard(shape.color ?? '#1d4ed8', 0.45));
  body.position.y = 0.4 + (h * 0.45) / 2;
  g.add(body);
  const lever = mesh(new THREE.BoxGeometry(1.4, h * 0.6, 1.6), METAL);
  lever.geometry.translate(0, (h * 0.6) / 2, 0);
  lever.position.y = 0.4 + h * 0.45;
  lever.rotation.z = 0.35;
  lever.userData = { part: id, input: 'on', gesture: 'toggle' } satisfies PickData;
  g.add(lever);
  for (const [px, pz] of Object.values(local(shape))) g.add(leg(px, pz, 0.5, -2.5, 0.3));
  return { group: g, lever };
}

function pot(shape: BenchShape, id: string): { group: THREE.Group; knob: THREE.Object3D } {
  const g = new THREE.Group();
  const [w, d, h] = shape.size;
  const trimmer = h < 15;
  const body = mesh(trimmer ? new RoundedBoxGeometry(w, h * 0.6, d, 2, 0.4) : new THREE.CylinderGeometry(w / 2, w / 2, 6, 40), standard(shape.color ?? '#2b6cb0', 0.45));
  body.position.y = 0.5 + (trimmer ? (h * 0.6) / 2 : 3);
  g.add(body);
  const knob = new THREE.Group();
  const shaftR = trimmer ? 1.1 : 3;
  const shaft = mesh(new THREE.CylinderGeometry(shaftR, shaftR, trimmer ? 0.8 : h - 6, 24), trimmer ? GOLD : standard('#202020', 0.5));
  shaft.position.y = trimmer ? 0.4 : (h - 6) / 2;
  knob.add(shaft);
  const slot = mesh(new THREE.BoxGeometry(shaftR * 1.8, 0.3, 0.4), standard('#111111', 0.7));
  slot.position.y = trimmer ? 0.85 : h - 6 + 0.1;
  knob.add(slot);
  knob.position.y = 0.5 + (trimmer ? h * 0.6 : 6);
  if (trimmer) knob.position.x = w / 2 - 1.8;
  knob.userData = { part: id, input: 'position', gesture: 'turn' } satisfies PickData;
  shaft.userData = knob.userData;
  g.add(knob);
  for (const [px, pz] of Object.values(local(shape))) g.add(leg(px, pz, 0.6, -2.5, 0.3));
  return { group: g, knob };
}

/** a male header's black spacer and gold pins under a module, standing it on its pins */
function maleHeader(g: THREE.Group, pins: [number, number][], top: number): void {
  for (const [px, pz] of pins) {
    const spacer = mesh(new THREE.BoxGeometry(2.5, 2.5, 2.5), BLACK_PLASTIC);
    spacer.position.set(px, top - 1.25, pz);
    g.add(spacer);
    const pin = mesh(new THREE.BoxGeometry(0.64, top + 3, 0.64), GOLD);
    pin.position.set(px, (top - 3) / 2, pz);
    g.add(pin);
  }
}

/**
 * A breakout's face when the part has no artwork: its soldermask, its name
 * and each header pin's label in silkscreen beside a plated pad, and four
 * mounting holes, as such boards are printed.
 */
export function silkscreen(shape: BenchShape, color: string): THREE.CanvasTexture | undefined {
  if (typeof document === 'undefined') return undefined;
  const [w, d] = shape.size;
  const k = Math.min(16, 2048 / Math.max(w, d));
  const c = document.createElement('canvas');
  c.width = Math.max(16, Math.round(w * k));
  c.height = Math.max(16, Math.round(d * k));
  const g = c.getContext('2d');
  if (!g) return undefined;
  g.fillStyle = color;
  g.fillRect(0, 0, c.width, c.height);
  // a faint copper pour under the mask
  g.fillStyle = 'rgba(255,255,255,0.04)';
  g.fillRect(k * 1.2, k * 1.2, c.width - k * 2.4, c.height - k * 2.4);
  const hole = (x: number, y: number) => {
    g.fillStyle = '#d4af37';
    g.beginPath();
    g.arc(x * k, y * k, 1.5 * k, 0, Math.PI * 2);
    g.fill();
    g.fillStyle = '#111';
    g.beginPath();
    g.arc(x * k, y * k, 0.9 * k, 0, Math.PI * 2);
    g.fill();
  };
  if (w > 14 && d > 12) for (const [x, y] of [[2.5, 2.5], [w - 2.5, 2.5]]) hole(x!, y!);
  g.fillStyle = '#f2f2f2';
  g.textAlign = 'center';
  g.textBaseline = 'middle';
  const pins = Object.entries(shape.pins);
  const label = Math.min(1.4, 2.54 * 0.55) * k;
  g.font = `600 ${label}px "Helvetica", sans-serif`;
  for (const [name, [x, y]] of pins) {
    g.fillStyle = '#d4af37';
    g.fillRect((x - 0.9) * k, (y - 0.9) * k, 1.8 * k, 1.8 * k);
    g.fillStyle = '#f2f2f2';
    g.save();
    g.translate(x * k, (y - 2.6) * k);
    g.rotate(-Math.PI / 2);
    g.textAlign = 'left';
    g.fillText(name, 0, 0);
    g.restore();
  }
  if (shape.label) {
    let size = Math.min(3.2, d / 5) * k;
    g.font = `700 ${size}px "Helvetica", sans-serif`;
    while (g.measureText(shape.label).width > c.width * 0.85 && size > 6) {
      size -= 1;
      g.font = `700 ${size}px "Helvetica", sans-serif`;
    }
    g.fillText(shape.label, c.width / 2, c.height * 0.35);
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  return t;
}

function module(shape: BenchShape, look: PartLook): THREE.Group {
  const g = new THREE.Group();
  const [w, d] = shape.size;
  const short = look.component.split('/').pop() ?? '';
  const pins = Object.values(local(shape));
  const lift = 8.5; // the module stands on its header: 2.5 mm of spacer over 6 mm of pin
  const pcbColor = shape.color ?? '#1f6f43';
  const face = look.texture ?? silkscreen(shape, pcbColor);
  const top = face ? new THREE.MeshStandardMaterial({ map: face, roughness: 0.55 }) : standard(pcbColor, 0.55);
  const edge = standard(pcbColor, 0.6);
  const pcb = mesh(new THREE.BoxGeometry(w, 1.6, d), [edge, edge, top, edge, edge, edge]);
  pcb.position.y = lift + 0.8;
  g.add(pcb);
  maleHeader(g, pins, lift);
  const surface = lift + 1.6;
  if (short === 'hc_sr04') {
    for (const x of [-13, 13]) {
      const can = mesh(new THREE.CylinderGeometry(8, 8, 12, 40), METAL);
      can.position.set(x, surface + 6, -1);
      g.add(can);
      const mesh_ = mesh(new THREE.CylinderGeometry(7.2, 7.2, 0.3, 40), standard('#3a3a3a', 0.9));
      mesh_.position.set(x, surface + 12.1, -1);
      g.add(mesh_);
    }
  } else if (short === 'dht22') {
    const shell = mesh(new RoundedBoxGeometry(w, 7.7, d - 4, 2, 1), standard('#f2f2f2', 0.7));
    shell.position.set(0, surface + 3.85, -2);
    g.add(shell);
    for (let i = 0; i < 6; i++) {
      const slot = mesh(new THREE.BoxGeometry(w * 0.7, 0.5, 1.2), standard('#cfcfcf', 0.8));
      slot.position.set(0, surface + 7.8, -d / 2 + 4 + i * 2.8);
      g.add(slot);
    }
  } else if (short === 'servo' || short === 'stepper_28byj48') {
    const box = mesh(new RoundedBoxGeometry(w, 22, d, 2, 1), standard(short === 'servo' ? '#1e5aa8' : '#c0c8d0', 0.4, short === 'servo' ? 0 : 0.6));
    box.position.y = surface + 11;
    g.add(box);
  }
  return g;
}

/** an indicator a board's output drives (an SMD LED) */
export interface Indicator {
  output: string;
  material: THREE.MeshStandardMaterial;
  halo?: THREE.Sprite;
}

function dipPackage(pins: number, label: string | undefined, socket: boolean): THREE.Group {
  const g = new THREE.Group();
  const half = Math.ceil(pins / 2);
  const len = half * 2.54 + 0.6;
  const rows = pins >= 40 ? 15.24 : 7.62;
  const bodyD = rows - 1.3;
  let base = 0;
  if (socket) {
    const s = mesh(new THREE.BoxGeometry(len + 2, 3, rows + 2.5), BLACK_PLASTIC);
    s.position.y = 1.5;
    g.add(s);
    base = 3;
  }
  const body = mesh(new RoundedBoxGeometry(len, 3.3, bodyD, 2, 0.35), EPOXY);
  body.position.y = base + 0.4 + 1.65;
  g.add(body);
  const notch = mesh(new THREE.CylinderGeometry(0.9, 0.9, 0.2, 20, 1, false, 0, Math.PI), standard('#050505', 0.8));
  notch.rotation.y = Math.PI / 2;
  notch.position.set(-len / 2, base + 0.4 + 3.31, 0);
  g.add(notch);
  if (label) {
    const tex = labelTexture(label, Math.min(len * 0.8, 30), Math.min(bodyD * 0.38, 2.4), '#cfd3d8');
    if (tex) {
      const plate = new THREE.Mesh(new THREE.PlaneGeometry(Math.min(len * 0.8, 30), Math.min(bodyD * 0.38, 2.4)), new THREE.MeshBasicMaterial({ map: tex, transparent: true }));
      plate.rotation.x = -Math.PI / 2;
      plate.position.y = base + 0.4 + 3.32;
      g.add(plate);
    }
  }
  for (let i = 0; i < half; i++) {
    const x = -len / 2 + 0.3 + 1.27 + i * 2.54;
    for (const side of [-1, 1]) {
      const legM = mesh(new THREE.BoxGeometry(0.5, 1.8, 0.25), METAL);
      legM.position.set(x, base + 0.9, side * (bodyD / 2 + 0.35));
      g.add(legM);
    }
  }
  return g;
}

/** a board's physical components on its PCB's surface (y = 0 there) */
function feature(f: BoardFeature, id: string, indicators: Indicator[]): THREE.Object3D {
  const [w, d, h] = f.size ?? [5, 5, 2];
  const g = new THREE.Group();
  switch (f.kind) {
    case 'usb_b': {
      const shell = mesh(new RoundedBoxGeometry(w, h, d, 2, 0.4), METAL);
      shell.position.y = h / 2;
      g.add(shell);
      const mouth = mesh(new THREE.BoxGeometry(0.6, h * 0.62, d * 0.72), standard('#16181c', 0.8));
      mouth.position.set(-w / 2 - 0.05, h / 2, 0);
      g.add(mouth);
      const tongue = mesh(new THREE.BoxGeometry(1.2, h * 0.28, d * 0.42), standard('#f2f2f2', 0.6));
      tongue.position.set(-w / 2 + 0.3, h / 2, 0);
      g.add(tongue);
      break;
    }
    case 'usb_micro':
    case 'usb_c': {
      const shell = mesh(new RoundedBoxGeometry(w, h, d, 2, Math.min(h, d) / 3), METAL);
      shell.position.y = h / 2;
      g.add(shell);
      break;
    }
    case 'barrel_jack': {
      const body = mesh(new RoundedBoxGeometry(w, h, d, 2, 0.6), standard('#141414', 0.5));
      body.position.y = h / 2;
      g.add(body);
      const bore = mesh(new THREE.CylinderGeometry(3.2, 3.2, 0.6, 24), standard('#050505', 0.9));
      bore.rotation.z = Math.PI / 2;
      bore.position.set(-w / 2 - 0.05, h / 2 + 0.6, 0);
      g.add(bore);
      const pin = mesh(new THREE.CylinderGeometry(1, 1, 0.8, 16), METAL);
      pin.rotation.z = Math.PI / 2;
      pin.position.set(-w / 2 - 0.1, h / 2 + 0.6, 0);
      g.add(pin);
      break;
    }
    case 'dip':
      g.add(dipPackage(f.pins ?? 28, f.label, true));
      break;
    case 'qfp':
    case 'qfn': {
      const body = mesh(new THREE.BoxGeometry(w, h, d), EPOXY);
      body.position.y = h / 2;
      g.add(body);
      break;
    }
    case 'crystal': {
      const can = mesh(new RoundedBoxGeometry(w, h, d, 2, Math.min(d, h) / 2.2), METAL);
      can.position.y = h / 2;
      g.add(can);
      break;
    }
    case 'button': {
      const base = mesh(new RoundedBoxGeometry(w, h * 0.6, d, 2, 0.3), standard('#d0d3d8', 0.35, 0.6));
      base.position.y = (h * 0.6) / 2;
      g.add(base);
      const cap = mesh(new THREE.CylinderGeometry(w * 0.3, w * 0.3, h * 0.55, 24), standard(f.color ?? '#c0392b', 0.4));
      cap.position.y = h * 0.6 + h * 0.27;
      const [target, input] = (f.input ?? '').split('.');
      if (target && input) cap.userData = { part: id, target, input, gesture: 'hold' } satisfies PickData;
      cap.name = 'cap';
      g.add(cap);
      break;
    }
    case 'led': {
      const color = new THREE.Color(f.color ?? '#ffd84a');
      const m = new THREE.MeshStandardMaterial({ color: color.clone().multiplyScalar(0.35).addScalar(0.45), emissive: color, emissiveIntensity: 0, roughness: 0.3 });
      const chip = mesh(new THREE.BoxGeometry(w, h, d), m);
      chip.position.y = h / 2;
      g.add(chip);
      const tex = halo();
      let sprite: THREE.Sprite | undefined;
      if (tex) {
        sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, color, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true, opacity: 0 }));
        sprite.scale.set(7, 7, 1);
        sprite.position.y = h + 0.6;
        sprite.visible = false;
        g.add(sprite);
      }
      if (f.output) indicators.push({ output: f.output, material: m, halo: sprite });
      break;
    }
    default: {
      const b = mesh(new RoundedBoxGeometry(w, h, d, 2, Math.min(w, d, h) / 6), standard(f.color ?? '#2d2d2d', 0.5));
      b.position.y = h / 2;
      g.add(b);
    }
  }
  g.rotation.y = -((f.rotation ?? 0) * Math.PI) / 180;
  return g;
}

function board(shape: BenchShape, look: PartLook, id: string, indicators: Indicator[]): THREE.Group {
  const g = new THREE.Group();
  const [w, d] = shape.size;
  const { standoff, thickness, surface, headers, contact } = boardStack(shape);
  const top = look.texture ? new THREE.MeshStandardMaterial({ map: look.texture, roughness: 0.5 }) : standard('#00878f', 0.5);
  const edge = standard('#006a70', 0.6);
  const pcb = mesh(new THREE.BoxGeometry(w, thickness, d), [edge, edge, top, edge, edge, edge]);
  pcb.position.y = standoff + thickness / 2;
  g.add(pcb);
  for (const [sx, sz] of [[-1, -1], [1, -1], [-1, 1], [1, 1]] as const) {
    const foot = mesh(new THREE.CylinderGeometry(1.5, 1.5, standoff, 12), standard('#efefef', 0.7));
    foot.position.set(sx * (w / 2 - 4), standoff / 2, sz * (d / 2 - 4));
    g.add(foot);
  }
  for (const f of shape.board?.features ?? []) {
    const o = feature(f, id, indicators);
    o.position.set(f.at[0] - w / 2, surface, f.at[1] - d / 2);
    g.add(o);
  }
  // female header sockets on its pins
  for (const [px, pz] of Object.values(local(shape))) {
    if (headers !== 'female') {
      const pad = mesh(new THREE.CylinderGeometry(0.8, 0.8, 0.05, 12), GOLD);
      pad.position.set(px, surface + 0.025, pz);
      g.add(pad);
      if (headers === 'male') {
        const spacer = mesh(new THREE.BoxGeometry(2.5, 2.5, 2.5), BLACK_PLASTIC);
        spacer.position.set(px, surface + 1.25, pz);
        g.add(spacer);
        const pin = mesh(new THREE.BoxGeometry(0.64, contact - surface, 0.64), GOLD);
        pin.position.set(px, (contact + surface) / 2, pz);
        g.add(pin);
      }
      continue;
    }
    const socket = mesh(new THREE.BoxGeometry(2.54, 8.5, 2.54), BLACK_PLASTIC);
    socket.position.set(px, surface + 4.25, pz);
    g.add(socket);
    const hole = mesh(new THREE.BoxGeometry(1, 0.05, 1), standard('#000000', 1), false);
    hole.position.set(px, surface + 8.52, pz);
    g.add(hole);
  }
  return g;
}

function box(shape: BenchShape): THREE.Group {
  const g = new THREE.Group();
  const [w, d, h] = shape.size;
  const body = mesh(new RoundedBoxGeometry(w, h, d, 2, Math.min(2, h / 4)), standard(shape.color ?? '#2d3436', 0.5));
  body.position.y = h / 2;
  g.add(body);
  const tex = shape.label ? labelTexture(shape.label, w * 0.8, Math.min(6, h * 0.3), '#e0e6ea') : undefined;
  if (tex) {
    const plate = new THREE.Mesh(new THREE.PlaneGeometry(w * 0.8, Math.min(6, h * 0.3)), new THREE.MeshBasicMaterial({ map: tex, transparent: true }));
    plate.rotation.x = -Math.PI / 2;
    plate.position.set(0, h + 0.02, -d / 6);
    g.add(plate);
  }
  // binding posts, red for the first terminal and black after
  Object.values(local(shape)).forEach(([px, pz], i) => {
    const post = mesh(new THREE.CylinderGeometry(1.4, 1.4, 4, 16), standard(i === 0 ? '#c0392b' : '#151515', 0.4));
    post.position.set(px, h + 2, pz);
    g.add(post);
  });
  return g;
}

export interface Built {
  group: THREE.Group;
  /** an LED's emissive material, light and halo, set from its luminance */
  glow?: THREE.MeshStandardMaterial;
  light?: THREE.PointLight;
  halo?: THREE.Sprite;
  /** a button's cap, a switch's lever, a pot's knob */
  control?: THREE.Object3D;
  /** a board's LEDs, by the output each shows */
  indicators?: Indicator[];
  /** where a jumper meets each pin, above the part's origin (y) */
  pinTop: number;
}

/** the 3D body of a part placed with `shape` */
export function buildPart(id: string, shape: BenchShape, look: PartLook): Built {
  let built: Built;
  switch (shape.package) {
    case 'breadboard':
      built = { group: breadboard(shape, look), pinTop: shape.size[2] };
      break;
    case 'axial':
      built = { group: axial(shape, look), pinTop: 0 };
      break;
    case 'led': {
      const { group, glow, light, halo: sprite } = led(shape, look);
      built = { group, glow, light, halo: sprite, pinTop: 0 };
      break;
    }
    case 'radial':
      built = { group: radial(shape, look), pinTop: 0 };
      break;
    case 'to92':
      built = { group: to92(shape, look), pinTop: 0 };
      break;
    case 'to220':
      built = { group: to220(shape), pinTop: 0 };
      break;
    case 'dip':
      built = { group: dip(shape), pinTop: 0 };
      break;
    case 'tactile': {
      const { group, cap } = tactile(shape, look, id);
      built = { group, control: cap, pinTop: 0 };
      break;
    }
    case 'switch': {
      const { group, lever } = toggle(shape, id);
      built = { group, control: lever, pinTop: 0 };
      break;
    }
    case 'pot': {
      const { group, knob } = pot(shape, id);
      built = { group, control: knob, pinTop: 0 };
      break;
    }
    case 'module':
      built = { group: module(shape, look), pinTop: 8.5 - 2.5 };
      break;
    case 'board': {
      const indicators: Indicator[] = [];
      built = { group: board(shape, look, id, indicators), indicators, pinTop: boardStack(shape).contact };
      break;
    }
    default:
      built = { group: box(shape), pinTop: shape.size[2] + 4 };
  }
  built.group.traverse((o) => {
    if (!o.userData.part) o.userData = { ...o.userData, part: id } satisfies PickData;
  });
  built.group.name = id;
  return built;
}
