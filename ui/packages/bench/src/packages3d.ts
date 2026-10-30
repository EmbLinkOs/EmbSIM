// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Packages drawn at their standard dimensions (docs/engineering/bench-3d.md,
// stage 2), as KiCad's packages3D generator draws them from the JEDEC and
// IPC tables. Millimetres; the origin is the package's centre on the
// surface it stands on, +y up, x along its length (pin 1 at -x), z across.
//
//   - DIP (JEDEC MS-001, 300 and 600 mil): a moulded body with its draft,
//     the pin-1 notch and dimple, and each lead as it is stamped: the wide
//     shoulder out of the body's side, bent down, narrowing below the seating
//     plane into the 0.46 mm tip that goes into the hole;
//   - pin header sockets and pin headers (2.54 mm): a strip per row, each
//     position's square opening down to its contact, or its square post with
//     a pointed tip over a moulded spacer;
//   - USB-B receptacle (USB 2.0, its chamfered mouth and tongue), a 2.1 mm
//     barrel jack (its bore and centre pin), a 6 mm tactile switch (its metal
//     frame and plunger), an HC-49/US crystal (its stadium can and rim),
//     QFP and QFN bodies with their leads or end pads and the pin-1 mark.

import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/examples/jsm/geometries/RoundedBoxGeometry.js';

export const TIN = new THREE.MeshStandardMaterial({ color: 0xc9ccd1, metalness: 0.9, roughness: 0.32 });
export const NICKEL = new THREE.MeshStandardMaterial({ color: 0xd8dade, metalness: 0.95, roughness: 0.22 });
export const GOLD_PLATE = new THREE.MeshStandardMaterial({ color: 0xd4af37, metalness: 0.95, roughness: 0.28 });
export const MOULD = new THREE.MeshStandardMaterial({ color: 0x161616, roughness: 0.62 });
export const PLASTIC_BLACK = new THREE.MeshStandardMaterial({ color: 0x141414, roughness: 0.55 });
const HOLE = new THREE.MeshStandardMaterial({ color: 0x050505, roughness: 1 });
const INSULATOR_WHITE = new THREE.MeshStandardMaterial({ color: 0xf0f0ec, roughness: 0.55 });
const INK = new THREE.MeshStandardMaterial({ color: 0x0b0b0b, roughness: 0.9 });

function part(geometry: THREE.BufferGeometry, material: THREE.Material): THREE.Mesh {
  const m = new THREE.Mesh(geometry, material);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

/** a rounded rectangle centred on the origin, for extrusion */
function roundedRect(w: number, h: number, r: number): THREE.Shape {
  const s = new THREE.Shape();
  const x = -w / 2;
  const y = -h / 2;
  s.moveTo(x + r, y);
  s.lineTo(x + w - r, y);
  s.quadraticCurveTo(x + w, y, x + w, y + r);
  s.lineTo(x + w, y + h - r);
  s.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  s.lineTo(x + r, y + h);
  s.quadraticCurveTo(x, y + h, x, y + h - r);
  s.lineTo(x, y + r);
  s.quadraticCurveTo(x, y, x + r, y);
  return s;
}

function rectPath(cx: number, cy: number, w: number, h: number): THREE.Path {
  const p = new THREE.Path();
  p.moveTo(cx - w / 2, cy - h / 2);
  p.lineTo(cx - w / 2, cy + h / 2);
  p.lineTo(cx + w / 2, cy + h / 2);
  p.lineTo(cx + w / 2, cy - h / 2);
  p.lineTo(cx - w / 2, cy - h / 2);
  return p;
}

// ---- DIP ---------------------------------------------------------------------------------

export interface DipDimensions {
  pins: number;
  /** body length D, width E1, thickness (A - A1), standoff A1 */
  length: number;
  width: number;
  thickness: number;
  standoff: number;
  /** row spacing e1 (lead centre to centre across) */
  rows: number;
  /** lead widths: the shoulder b1 and the tip b; lead thickness c; tip length below the seating plane L */
  shoulder: number;
  tip: number;
  lead: number;
  below: number;
}

/** JEDEC MS-001 (300 mil) and MS-011 (600 mil) body lengths, nominal, by pin count */
const DIP_LENGTH: Record<number, number> = { 4: 4.6, 6: 7.62, 8: 9.27, 14: 19.05, 16: 19.18, 18: 22.86, 20: 26.16, 22: 27.94, 24: 31.75, 28: 34.67, 32: 41.91, 40: 52.07 };

/**
 * A DIP's dimensions: the standard's nominal values. The 28-pin is
 * Microchip's 28P3 (the ATmega328P-PU): D 34.67, E1 7.3.
 */
export function dipDimensions(pins: number, rows = pins >= 32 ? 15.24 : 7.62): DipDimensions {
  const wide = rows > 10;
  return {
    pins,
    length: DIP_LENGTH[pins] ?? (pins / 2) * 2.54 - 0.89,
    width: wide ? 13.97 : pins === 28 ? 7.3 : 6.35,
    thickness: 3.3,
    standoff: 0.51,
    rows,
    shoulder: 1.27,
    tip: 0.46,
    lead: 0.25,
    below: 3.3,
  };
}

/** where each lead's tip is, pin 1 first, counter-clockwise from above: pin 1 at -x, +z (bottom left seen from above, the notch to the left) */
export function dipPinPositions(pins: number, rows = pins >= 32 ? 15.24 : 7.62): [number, number][] {
  const half = pins / 2;
  const x0 = -((half - 1) * 2.54) / 2;
  const out: [number, number][] = [];
  for (let i = 0; i < half; i++) out.push([x0 + i * 2.54, rows / 2]);
  for (let i = half - 1; i >= 0; i--) out.push([x0 + i * 2.54, -rows / 2]);
  return out;
}

/** a body tapered by the mould's draft: its top face smaller than its bottom */
function draftedBody(length: number, width: number, height: number, draft: number, material: THREE.Material): THREE.Mesh {
  const g = new THREE.BoxGeometry(length, height, width, 1, 1, 1);
  const pos = g.attributes.position as THREE.BufferAttribute;
  for (let i = 0; i < pos.count; i++) {
    if (pos.getY(i) > 0) {
      pos.setX(i, pos.getX(i) - Math.sign(pos.getX(i)) * draft);
      pos.setZ(i, pos.getZ(i) - Math.sign(pos.getZ(i)) * draft);
    }
  }
  g.computeVertexNormals();
  return part(g, material);
}

/** a stamped DIP lead: out of the body's side, bent down, narrowing below the seating plane */
function dipLead(x: number, side: 1 | -1, d: DipDimensions, exit: number): THREE.Group {
  const g = new THREE.Group();
  const outer = d.rows / 2;
  // out of the body, level
  const arm = part(new THREE.BoxGeometry(d.shoulder, d.lead, outer - exit + d.lead / 2), TIN);
  arm.position.set(x, exit === 0 ? 0 : d.standoff + d.thickness * 0.38, side * ((outer + exit) / 2 + d.lead / 4));
  // down to the seating plane, at the shoulder's width
  const knee = arm.position.y;
  const drop = part(new THREE.BoxGeometry(d.shoulder, knee, d.lead), TIN);
  drop.position.set(x, knee / 2, side * outer);
  // the tip below the seating plane, tapered from the shoulder at its top
  const tip = new THREE.CylinderGeometry(d.tip * 0.6, d.tip * 0.6, d.below, 4, 1);
  tip.rotateY(Math.PI / 4);
  tip.scale(1, 1, d.lead / d.tip / 0.85);
  const t = part(tip, TIN);
  t.position.set(x, -d.below / 2, side * outer);
  const taper = part(new THREE.CylinderGeometry(d.shoulder / 2, d.tip / 2, 0.8, 4, 1), TIN);
  taper.geometry.rotateY(Math.PI / 4);
  taper.geometry.scale(1, 1, (d.lead * 1.4) / d.shoulder);
  taper.position.set(x, -0.4, side * outer);
  g.add(arm, drop, taper, t);
  return g;
}

/** a DIP standing on its seating plane, its leads' tips below it as through a board */
export function dipBody(pins: number, opts: { rows?: number; label?: string; labelTexture?: THREE.Texture } = {}): THREE.Group {
  const d = dipDimensions(pins, opts.rows);
  const g = new THREE.Group();
  g.name = `DIP-${pins}`;
  const body = draftedBody(d.length, d.width, d.thickness, 0.12, MOULD);
  body.position.y = d.standoff + d.thickness / 2;
  g.add(body);
  const top = d.standoff + d.thickness;
  // the notch at the pin-1 end, and the ejector dimple by pin 1
  const notch = part(new THREE.CylinderGeometry(0.85, 0.85, 0.5, 24, 1, false, 0, Math.PI), INK);
  // a half disc from the end into the body (the cylinder's half at +x)
  notch.position.set(-d.length / 2 + 0.12, top - 0.24, 0);
  g.add(notch);
  const dimple = part(new THREE.CylinderGeometry(0.5, 0.5, 0.08, 20), new THREE.MeshStandardMaterial({ color: 0x222222, roughness: 0.35 }));
  dimple.position.set(-d.length / 2 + 2.2, top - 0.02, d.width / 2 - 1.4);
  g.add(dimple);
  if (opts.labelTexture) {
    const w = Math.min(d.length * 0.75, 30);
    const h = Math.min(d.width * 0.35, 2.4);
    const plate = new THREE.Mesh(new THREE.PlaneGeometry(w, h), new THREE.MeshBasicMaterial({ map: opts.labelTexture, transparent: true }));
    plate.rotation.x = -Math.PI / 2;
    plate.position.y = top + 0.01;
    g.add(plate);
  }
  for (const [x, z] of dipPinPositions(pins, d.rows)) g.add(dipLead(x, z > 0 ? 1 : -1, d, d.width / 2 - 0.12));
  return g;
}

/** a DIP socket (a stamped-contact socket's open frame) with the chip in it */
export function dipSocket(pins: number, rows = pins >= 32 ? 15.24 : 7.62): THREE.Group {
  const d = dipDimensions(pins, rows);
  const g = new THREE.Group();
  const len = d.length + 2.2;
  const width = rows + 2.6;
  const shape = roundedRect(len, width, 0.4);
  shape.holes.push(rectPath(0, 0, len - 5, rows - 3.2));
  const frame = new THREE.ExtrudeGeometry(shape, { depth: 3, bevelEnabled: false });
  frame.rotateX(-Math.PI / 2);
  g.add(part(frame, PLASTIC_BLACK));
  for (const [x, z] of dipPinPositions(pins, rows)) {
    const hole = part(new THREE.BoxGeometry(0.9, 0.05, 0.6), HOLE);
    hole.position.set(x, 3.01, z);
    g.add(hole);
  }
  return g;
}

// ---- headers -----------------------------------------------------------------------------

/** pins grouped into straight 2.54 mm strips (a header is moulded as one piece per row) */
export function headerStrips(pins: [number, number][], pitch = 2.54): [number, number][][] {
  const rows = new Map<string, [number, number][]>();
  for (const p of pins) {
    const key = Math.round(p[1] * 20).toString();
    const list = rows.get(key) ?? [];
    list.push(p);
    rows.set(key, list);
  }
  const strips: [number, number][][] = [];
  for (const list of rows.values()) {
    list.sort((a, b) => a[0] - b[0]);
    let cur: [number, number][] = [];
    for (const p of list) {
      const last = cur[cur.length - 1];
      if (last && Math.abs(p[0] - last[0] - pitch) > 0.05) {
        strips.push(cur);
        cur = [];
      }
      cur.push(p);
    }
    if (cur.length) strips.push(cur);
  }
  return strips;
}

/** a pin header socket's strip: 8.5 mm high, each position's square opening down to its contact */
export function socketStrip(n: number, height = 8.5, pitch = 2.54): THREE.Group {
  const g = new THREE.Group();
  const len = n * pitch;
  const shape = roundedRect(len, pitch, 0.15);
  for (let i = 0; i < n; i++) shape.holes.push(rectPath(-len / 2 + pitch / 2 + i * pitch, 0, 1.0, 1.0));
  // the top 2 mm of the body has the openings; below them the contacts sit in their cavities
  const top = new THREE.ExtrudeGeometry(shape, { depth: 2, bevelEnabled: false });
  top.rotateX(-Math.PI / 2);
  top.translate(0, height - 2, 0);
  g.add(part(top, PLASTIC_BLACK));
  const base = part(new THREE.BoxGeometry(len, height - 2, pitch), PLASTIC_BLACK);
  base.position.y = (height - 2) / 2;
  g.add(base);
  for (let i = 0; i < n; i++) {
    const x = -len / 2 + pitch / 2 + i * pitch;
    const floor = part(new THREE.BoxGeometry(1.0, 0.05, 1.0), HOLE);
    floor.position.set(x, height - 2.02, 0);
    g.add(floor);
    // the contact's two gold leaves, seen down the opening
    for (const side of [-1, 1]) {
      const leaf = part(new THREE.BoxGeometry(0.7, 1.6, 0.12), GOLD_PLATE);
      leaf.position.set(x, height - 1.1, side * 0.38);
      g.add(leaf);
    }
  }
  return g;
}

/** a pin header's strip: the moulded spacer and each square post with its pointed tip */
export function pinStrip(n: number, opts: { above?: number; below?: number; spacer?: number; pitch?: number } = {}): THREE.Group {
  const { above = 6, below = 3, spacer = 2.5, pitch = 2.54 } = opts;
  const g = new THREE.Group();
  const len = n * pitch;
  const body = part(new RoundedBoxGeometry(len, spacer, pitch, 2, 0.25), PLASTIC_BLACK);
  body.position.y = spacer / 2;
  g.add(body);
  for (let i = 0; i < n; i++) {
    const x = -len / 2 + pitch / 2 + i * pitch;
    // the necks between positions, where a strip is snapped apart
    if (i > 0) {
      const neck = part(new THREE.BoxGeometry(0.3, spacer * 0.7, pitch * 1.02), INK);
      neck.position.set(x - pitch / 2, spacer / 2, 0);
      g.add(neck);
    }
    const shaft = spacer + above - 0.5;
    const post = part(new THREE.BoxGeometry(0.64, shaft + below - 0.5, 0.64), GOLD_PLATE);
    post.position.set(x, (shaft - (below - 0.5)) / 2, 0);
    g.add(post);
    for (const [y, dir] of [[shaft, 1], [-(below - 0.5), -1]] as const) {
      const point = part(new THREE.ConeGeometry(0.45, 0.5, 4), GOLD_PLATE);
      point.rotation.y = Math.PI / 4;
      if (dir < 0) point.rotation.x = Math.PI;
      point.position.set(x, y + dir * 0.25, 0);
      g.add(point);
    }
  }
  return g;
}

// ---- connectors, switches, crystals --------------------------------------------------------

/**
 * A USB-B receptacle (USB 2.0 spec): a 12.0 x 10.9 mm shell, its mouth
 * 8.45 x 7.78 mm with the top corners chamfered, the white tongue with its
 * four contacts. The mouth faces -x; `length` is the shell's depth.
 */
export function usbB(length = 16.5): THREE.Group {
  const g = new THREE.Group();
  const W = 12.0;
  const H = 10.9;
  // one drawn shell, so it reads as one piece; the mouth, the tongue and its contacts on its face
  const shell = new THREE.ExtrudeGeometry(roundedRect(W, H, 0.6), { depth: length, bevelEnabled: false });
  shell.rotateY(Math.PI / 2);
  shell.translate(-length / 2, H / 2, 0);
  g.add(part(shell, NICKEL));
  const mw = 8.45;
  const mh = 7.78;
  const ch = 1.6;
  const mouth = new THREE.Shape();
  mouth.moveTo(-mw / 2, -mh / 2);
  mouth.lineTo(-mw / 2, mh / 2 - ch);
  mouth.lineTo(-mw / 2 + ch, mh / 2);
  mouth.lineTo(mw / 2 - ch, mh / 2);
  mouth.lineTo(mw / 2, mh / 2 - ch);
  mouth.lineTo(mw / 2, -mh / 2);
  mouth.lineTo(-mw / 2, -mh / 2);
  const face = new THREE.ShapeGeometry(mouth);
  face.rotateY(-Math.PI / 2);
  face.translate(-length / 2 - 0.01, H / 2, 0);
  g.add(part(face, HOLE));
  // the tongue (8.2 x 2.7 mm seen end-on) and its four contacts, standing out of the mouth's shadow
  const tongue = part(new THREE.BoxGeometry(0.02, 2.7, 4.9), INSULATOR_WHITE);
  tongue.position.set(-length / 2 - 0.02, H / 2, 0);
  g.add(tongue);
  for (const zz of [-1.25, 1.25]) for (const yy of [-1.4, 1.4]) {
    const c = part(new THREE.BoxGeometry(0.02, 0.1, 0.9), GOLD_PLATE);
    c.position.set(-length / 2 - 0.03, H / 2 + yy, zz);
    g.add(c);
  }
  return g;
}

/** a 2.1 mm barrel jack (PJ-102A type): its body, the 6.3 mm bore facing -x and the centre pin */
export function barrelJack(w = 14, d = 9, h = 11): THREE.Group {
  const g = new THREE.Group();
  const shape = roundedRect(d, h, 0.6);
  const bore = new THREE.Path();
  bore.absarc(0, 0.6, 3.15, 0, Math.PI * 2, true);
  shape.holes.push(bore);
  const front = new THREE.ExtrudeGeometry(shape, { depth: 3, bevelEnabled: false, curveSegments: 32 });
  front.rotateY(Math.PI / 2);
  front.translate(-w / 2, h / 2, 0);
  g.add(part(front, PLASTIC_BLACK));
  const rest = new THREE.ExtrudeGeometry(roundedRect(d, h, 0.6), { depth: w - 3, bevelEnabled: false });
  rest.rotateY(Math.PI / 2);
  rest.translate(-w / 2 + 3, h / 2, 0);
  g.add(part(rest, PLASTIC_BLACK));
  const back = part(new THREE.CylinderGeometry(3.15, 3.15, 0.1, 32), HOLE);
  back.rotation.z = Math.PI / 2;
  back.position.set(-w / 2 + 2.95, h / 2 + 0.6, 0);
  g.add(back);
  const pin = part(new THREE.CylinderGeometry(1.0, 1.0, 7, 20), NICKEL);
  pin.rotation.z = Math.PI / 2;
  pin.position.set(-w / 2 + 3.5 + 0.5, h / 2 + 0.6, 0);
  g.add(pin);
  return g;
}

/**
 * A 6 x 6 mm tactile switch: the black base, the metal frame folded over it
 * with its four tabs, and the round plunger (`cap`, which a press moves).
 */
export function tactileSwitch(size = 6, height = 4.3, capColor = '#1a1a1a'): { group: THREE.Group; cap: THREE.Mesh } {
  const g = new THREE.Group();
  const base = part(new RoundedBoxGeometry(size, 2.6, size, 2, 0.25), PLASTIC_BLACK);
  base.position.y = 1.3;
  g.add(base);
  const frame = new THREE.Shape();
  const half = size / 2;
  frame.moveTo(-half, -half);
  frame.lineTo(half, -half);
  frame.lineTo(half, half);
  frame.lineTo(-half, half);
  frame.lineTo(-half, -half);
  const hole = new THREE.Path();
  hole.absarc(0, 0, 1.9, 0, Math.PI * 2, true);
  frame.holes.push(hole);
  const plate = new THREE.ExtrudeGeometry(frame, { depth: 0.25, bevelEnabled: false, curveSegments: 32 });
  plate.rotateX(-Math.PI / 2);
  plate.translate(0, 2.6, 0);
  g.add(part(plate, TIN));
  for (const sx of [-1, 1]) {
    const tab = part(new THREE.BoxGeometry(0.25, 2.2, 1.6), TIN);
    tab.position.set(sx * (half + 0.12), 1.6, 0);
    g.add(tab);
  }
  const cap = part(new THREE.CylinderGeometry(1.75, 1.75, height - 2.85, 32), new THREE.MeshStandardMaterial({ color: capColor, roughness: 0.45 }));
  cap.position.y = 2.85 + (height - 2.85) / 2;
  cap.name = 'cap';
  g.add(cap);
  return { group: g, cap };
}

/** an HC-49/US crystal: the stadium-shaped can on its rim (11.05 x 4.65 x 3.5 mm low profile) */
export function crystalHC49(length = 11.05, width = 4.65, height = 3.5): THREE.Group {
  const g = new THREE.Group();
  const stadium = (l: number, w: number) => {
    const s = new THREE.Shape();
    const r = w / 2;
    s.moveTo(-l / 2 + r, -r);
    s.lineTo(l / 2 - r, -r);
    s.absarc(l / 2 - r, 0, r, -Math.PI / 2, Math.PI / 2, false);
    s.lineTo(-l / 2 + r, r);
    s.absarc(-l / 2 + r, 0, r, Math.PI / 2, Math.PI * 1.5, false);
    return s;
  };
  const rim = new THREE.ExtrudeGeometry(stadium(length, width), { depth: 0.45, bevelEnabled: false, curveSegments: 24 });
  rim.rotateX(-Math.PI / 2);
  g.add(part(rim, NICKEL));
  const can = new THREE.ExtrudeGeometry(stadium(length - 0.5, width - 0.5), { depth: height - 0.45 - 0.3, bevelEnabled: true, bevelThickness: 0.3, bevelSize: 0.25, bevelSegments: 4, curveSegments: 24 });
  can.rotateX(-Math.PI / 2);
  can.translate(0, 0.45, 0);
  g.add(part(can, NICKEL));
  return g;
}

/**
 * A quad flat package: the body and its gull-wing leads on four sides
 * (QFP), or end pads under its edges (QFN), and the pin-1 mark.
 */
export function quadPackage(kind: 'qfp' | 'qfn', w: number, d: number, h: number, pins: number): THREE.Group {
  const g = new THREE.Group();
  const perSide = Math.max(1, Math.round(pins / 4));
  const pitch = kind === 'qfp' ? Math.min(0.8, (Math.min(w, d) - 1) / perSide) : Math.min(0.65, (Math.min(w, d) - 0.8) / perSide);
  const standoff = kind === 'qfp' ? 0.1 : 0;
  const body = kind === 'qfp' ? draftedBody(w, d, h - standoff, 0.08, MOULD) : part(new THREE.BoxGeometry(w, h, d), MOULD);
  body.position.y = standoff + (h - standoff) / 2;
  g.add(body);
  const mark = part(new THREE.CylinderGeometry(Math.min(w, d) * 0.06, Math.min(w, d) * 0.06, 0.02, 16), new THREE.MeshStandardMaterial({ color: 0x2a2a2a, roughness: 0.3 }));
  mark.position.set(-w / 2 + Math.min(w, d) * 0.15, h + 0.005, -d / 2 + Math.min(w, d) * 0.15);
  g.add(mark);
  for (let side = 0; side < 4; side++) {
    const alongX = side % 2 === 0;
    const len = alongX ? w : d;
    const edge = alongX ? d / 2 : w / 2;
    for (let i = 0; i < perSide; i++) {
      const t = -((perSide - 1) * pitch) / 2 + i * pitch;
      if (Math.abs(t) > len / 2 - 0.2) continue;
      const sign = side < 2 ? 1 : -1;
      let lead: THREE.Mesh;
      if (kind === 'qfp') {
        lead = part(new THREE.BoxGeometry(pitch * 0.45, 0.15, 1.0), TIN);
        lead.position.set(0, 0.08, edge + 0.5);
      } else {
        lead = part(new THREE.BoxGeometry(pitch * 0.5, 0.2, 0.4), TIN);
        lead.position.set(0, 0.1, edge - 0.19);
      }
      const holder = new THREE.Group();
      holder.add(lead);
      holder.position.x = t;
      holder.rotation.y = alongX ? (sign > 0 ? 0 : Math.PI) : (sign > 0 ? Math.PI / 2 : -Math.PI / 2);
      if (!alongX) holder.position.set(0, 0, t);
      g.add(holder);
    }
  }
  return g;
}

/** the label plate on a DIP's top: three quarters of its length, a third of its width */
export function dipLabelSize(pins: number, rows?: number): [number, number] {
  const d = dipDimensions(pins, rows);
  return [Math.min(d.length * 0.75, 30), Math.min(d.width * 0.35, 2.4)];
}

/**
 * A radial aluminium electrolytic capacitor as made: the printed sleeve over
 * the can with its rolled rim at the top and the crimp groove above the
 * rubber seal, the aluminium top scored with its K vent, and the sleeve's
 * stripe of minus signs over the negative lead (towards `minusDir`, an angle
 * about +y from +x, radians).
 */
export function electrolytic(dia: number, height: number, sleeve: string, minusDir: number): THREE.Group {
  const g = new THREE.Group();
  const r = dia / 2;
  // the sleeve's profile from the seal to the rim, turned about the axis
  const pts = [
    new THREE.Vector2(r * 0.8, 0),
    new THREE.Vector2(r, 0.3),
    new THREE.Vector2(r, 0.8),
    new THREE.Vector2(r - 0.25, 1.0),
    new THREE.Vector2(r, 1.2),
    new THREE.Vector2(r, height - 0.4),
    new THREE.Vector2(r - 0.25, height - 0.1),
    new THREE.Vector2(r - 0.45, height),
  ];
  const body = part(new THREE.LatheGeometry(pts, 48), new THREE.MeshStandardMaterial({ color: sleeve, roughness: 0.35 }));
  g.add(body);
  const top = part(new THREE.CylinderGeometry(r - 0.45, r - 0.45, 0.05, 48), new THREE.MeshStandardMaterial({ color: 0xc6cbd1, metalness: 0.85, roughness: 0.38 }));
  top.position.y = height - 0.06;
  g.add(top);
  // the vent: three scored lines meeting at the centre
  for (const a of [0, (2 * Math.PI) / 3, (4 * Math.PI) / 3]) {
    const line = part(new THREE.BoxGeometry((r - 0.6) * 0.95, 0.03, 0.12), new THREE.MeshStandardMaterial({ color: 0x8b9197, metalness: 0.7, roughness: 0.5 }));
    line.geometry.translate((r - 0.6) * 0.475, 0, 0);
    line.rotation.y = a;
    line.position.y = height - 0.02;
    g.add(line);
  }
  // the stripe, a band of the sleeve over the negative lead
  const half = Math.PI * 0.16;
  // CylinderGeometry measures theta from +z towards +x; minusDir from +x towards -z (a rotation about +y)
  const thetaCentre = Math.PI / 2 + minusDir;
  const stripe = part(new THREE.CylinderGeometry(r + 0.01, r + 0.01, height - 1.7, 24, 1, true, thetaCentre - half, 2 * half), new THREE.MeshStandardMaterial({ color: 0xd4dbe4, roughness: 0.4 }));
  stripe.position.y = 1.2 + (height - 1.7) / 2;
  g.add(stripe);
  for (let i = 0; i < Math.floor((height - 2) / 2.2); i++) {
    const minus = part(new THREE.BoxGeometry(0.1, 0.25, dia * 0.12), new THREE.MeshStandardMaterial({ color: sleeve, roughness: 0.4 }));
    minus.position.set(Math.cos(minusDir) * (r + 0.03), 1.9 + i * 2.2, -Math.sin(minusDir) * (r + 0.03));
    minus.rotation.y = minusDir;
    g.add(minus);
  }
  return g;
}

/** a quad package's body by pin count when a board does not give it: LQFP (JEDEC MS-026) and QFN (MO-220) */
export function quadSize(kind: 'qfp' | 'qfn', pins: number): [number, number, number] {
  if (kind === 'qfp') {
    const side = pins <= 32 ? 7 : pins <= 48 ? 7 : pins <= 64 ? 10 : pins <= 100 ? 14 : pins <= 144 ? 20 : 24;
    return [side, side, 1.6];
  }
  const side = pins <= 16 ? 3 : pins <= 24 ? 4 : pins <= 32 ? 5 : pins <= 48 ? 7 : 8;
  return [side, side, 0.85];
}

/**
 * A micro-USB B receptacle (7.5 x 5.6 x 2.8 mm drawn): its drawn shell
 * with the trapezoid mouth (6.9 mm wide at the top, 5.4 at the bottom,
 * 1.85 high) and the tongue in it. The mouth faces +z, the board's edge;
 * the connector's width runs along x.
 */
export function usbMicro(w = 7.5, d = 5.6, h = 2.8): THREE.Group {
  const g = new THREE.Group();
  const shell = part(new RoundedBoxGeometry(w, h, d, 2, 0.35), NICKEL);
  shell.position.y = h / 2;
  g.add(shell);
  const mouth = new THREE.Shape();
  const top = Math.min(6.9, w - 0.5);
  const bottom = top * 0.78;
  const mh = Math.min(1.85, h - 0.6);
  mouth.moveTo(-top / 2, mh / 2);
  mouth.lineTo(top / 2, mh / 2);
  mouth.lineTo(bottom / 2, -mh / 2);
  mouth.lineTo(-bottom / 2, -mh / 2);
  mouth.lineTo(-top / 2, mh / 2);
  const face = new THREE.ShapeGeometry(mouth);
  face.translate(0, h / 2, d / 2 + 0.01);
  g.add(part(face, HOLE));
  const tongue = part(new THREE.BoxGeometry(top * 0.55, mh * 0.3, 0.02), INSULATOR_WHITE);
  tongue.position.set(0, h / 2 + mh * 0.12, d / 2 + 0.02);
  g.add(tongue);
  return g;
}

/** a USB-C receptacle: the stadium shell and mouth (8.34 x 2.56 mm) with its tongue; the mouth faces +z */
export function usbC(w = 8.94, d = 7.35, h = 3.26): THREE.Group {
  const g = new THREE.Group();
  const shell = part(new RoundedBoxGeometry(w, h, d, 3, h / 2 - 0.01), NICKEL);
  shell.position.y = h / 2;
  g.add(shell);
  const mw = Math.min(8.34, w - 0.6);
  const mh = Math.min(2.56, h - 0.7);
  const face = new THREE.ShapeGeometry(roundedRect(mw, mh, mh / 2 - 0.01));
  face.translate(0, h / 2, d / 2 + 0.01);
  g.add(part(face, HOLE));
  const tongue = part(new THREE.BoxGeometry(mw * 0.8, 0.7, 0.02), PLASTIC_BLACK);
  tongue.position.set(0, h / 2, d / 2 + 0.02);
  g.add(tongue);
  return g;
}

/**
 * An RF module (an ESP32-C3-MINI-1, 13.2 x 16.6 x 2.4 mm): its PCB, the
 * shield can over the chip with the module's name on it, and at the -z end
 * the bare antenna area with the meandered antenna in the soldermask.
 */
export function rfModule(w: number, d: number, h: number, nameplate?: THREE.Texture, substrate = '#1d2a36'): THREE.Group {
  const g = new THREE.Group();
  const pcbT = 0.8;
  const pcb = part(new THREE.BoxGeometry(w, pcbT, d), new THREE.MeshStandardMaterial({ color: substrate, roughness: 0.45 }));
  pcb.position.y = pcbT / 2;
  g.add(pcb);
  const antenna = Math.min(d * 0.3, 5.2);
  const canD = d - antenna - 0.6;
  const can = part(new RoundedBoxGeometry(w - 0.6, h - pcbT, canD, 2, 0.2), NICKEL);
  can.position.set(0, pcbT + (h - pcbT) / 2, -d / 2 + antenna + 0.3 + canD / 2);
  g.add(can);
  if (nameplate) {
    const plate = new THREE.Mesh(new THREE.PlaneGeometry((w - 0.6) * 0.8, canD * 0.35), new THREE.MeshBasicMaterial({ map: nameplate, transparent: true }));
    plate.rotation.x = -Math.PI / 2;
    plate.position.set(0, h + 0.005, can.position.z);
    g.add(plate);
  }
  // the antenna: a meander of copper under the mask, along the free end
  const copper = new THREE.MeshStandardMaterial({ color: 0x3d5a6b, roughness: 0.35, metalness: 0.3 });
  const z0 = -d / 2 + 0.7;
  const turns = 6;
  const span = w - 2.4;
  for (let i = 0; i <= turns; i++) {
    const x = -span / 2 + (i * span) / turns;
    const leg = part(new THREE.BoxGeometry(0.35, 0.02, antenna - 1.6), copper);
    leg.position.set(x, pcbT + 0.01, z0 + (antenna - 1.6) / 2);
    g.add(leg);
    if (i < turns) {
      const bridge = part(new THREE.BoxGeometry(span / turns + 0.35, 0.02, 0.35), copper);
      bridge.position.set(x + span / turns / 2, pcbT + 0.01, i % 2 ? z0 : z0 + antenna - 1.6);
      g.add(bridge);
    }
  }
  return g;
}

/** the glass-epoxy core seen at a board's cut edge */
const FR4_EDGE = new THREE.MeshStandardMaterial({ color: 0xcfc6a0, roughness: 0.85 });

export interface PcbBody {
  /** width and depth, mm; the outline is in mm from the top-left corner */
  w: number;
  d: number;
  thickness: number;
  outline?: [number, number][];
  holes?: { at: [number, number]; r: number }[];
  /** the soldermask's colour, for the underside and the edges' film */
  mask?: string;
  /** the top: the board's artwork */
  top?: THREE.Texture;
}

/**
 * A PCB as made: its outline cut from glass-epoxy (the pale core at the
 * edges), the drilled mounting holes with their plated barrels and rings,
 * the top's soldermask over the artwork with its gloss, and the underside
 * in the same mask. y = 0 is its underside; the origin is its centre.
 */
export function pcbBody(p: PcbBody): THREE.Group {
  const g = new THREE.Group();
  g.name = 'pcb';
  const { w, d, thickness: t } = p;
  // the outline in the shape's plane: x right, y away from the person (scene -z)
  const toShape = ([x, y]: [number, number]) => new THREE.Vector2(x - w / 2, d / 2 - y);
  const outline = p.outline?.length ? p.outline : ([[0, 0], [w, 0], [w, d], [0, d]] as [number, number][]);
  const makeShape = () => {
    const s = new THREE.Shape(outline.map(toShape));
    for (const h of p.holes ?? []) {
      const c = toShape(h.at);
      const hole = new THREE.Path();
      hole.absarc(c.x, c.y, h.r * 0.8, 0, Math.PI * 2, true);
      s.holes.push(hole);
    }
    return s;
  };
  const core = new THREE.ExtrudeGeometry(makeShape(), { depth: t, bevelEnabled: false, curveSegments: 24 });
  core.rotateX(-Math.PI / 2);
  g.add(part(core, FR4_EDGE));
  // the top's artwork, mapped by position (the shape's u right, v away from the person)
  const skin = (y: number, up: boolean, material: THREE.Material) => {
    const geo = new THREE.ShapeGeometry(makeShape(), 24);
    const pos = geo.attributes.position as THREE.BufferAttribute;
    const uv = geo.attributes.uv as THREE.BufferAttribute;
    for (let i = 0; i < pos.count; i++) uv.setXY(i, (pos.getX(i) + w / 2) / w, (pos.getY(i) + d / 2) / d);
    geo.rotateX(up ? -Math.PI / 2 : Math.PI / 2);
    if (!up) geo.scale(1, 1, -1);
    const m = part(geo, material);
    m.position.y = y;
    return m;
  };
  const maskColour = new THREE.Color(p.mask ?? '#1f6f43');
  const top = p.top
    ? new THREE.MeshPhysicalMaterial({ map: p.top, roughness: 0.55, clearcoat: 0.55, clearcoatRoughness: 0.28 })
    : new THREE.MeshPhysicalMaterial({ color: maskColour, roughness: 0.55, clearcoat: 0.55, clearcoatRoughness: 0.28 });
  g.add(skin(t + 0.01, true, top));
  // mirrored to keep the outline's handedness, so drawn from both sides
  g.add(skin(-0.01, false, new THREE.MeshPhysicalMaterial({ color: maskColour, roughness: 0.55, clearcoat: 0.4, clearcoatRoughness: 0.3, side: THREE.DoubleSide })));
  // the plated holes: the barrel through the board and the ring around it on both faces
  for (const h of p.holes ?? []) {
    const c = toShape(h.at);
    const barrel = part(new THREE.CylinderGeometry(h.r * 0.8, h.r * 0.8, t + 0.04, 32, 1, true), TIN);
    (barrel.material as THREE.Material).side = THREE.DoubleSide;
    barrel.position.set(c.x, t / 2, -c.y);
    g.add(barrel);
    for (const [y, up] of [[t + 0.02, true], [-0.02, false]] as const) {
      const ring = part(new THREE.RingGeometry(h.r * 0.8, h.r, 40), TIN);
      ring.rotation.x = up ? -Math.PI / 2 : Math.PI / 2;
      ring.position.set(c.x, y, -c.y);
      g.add(ring);
    }
  }
  return g;
}
