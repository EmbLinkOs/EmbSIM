// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A part's 3D model from a file (docs/engineering/bench-3d.md, stage 3):
// a VRML 2.0 model as KiCad's library ships them, drawn instead of the body
// the bench builds. KiCad's models are Z-up, in units of 0.1 in (2.54 mm),
// with their origin at pad 1; the bench is Y-up in mm with a part's origin
// at its centre. So the model is stood up, scaled, and its origin put on the
// part's first pin (plus an offset when the catalog gives one), then turned
// about the vertical as the catalog says. Each file is parsed once.

import * as THREE from 'three';
import { VRMLLoader } from 'three/examples/jsm/loaders/VRMLLoader.js';

export interface Model3d {
  /** the VRML text */
  text: string;
  /** mm per model unit; KiCad's are 2.54 */
  scale?: number;
  /** mm from the part's first pin: x right, y towards the viewer, z up */
  offset_mm?: [number, number, number];
  /** a turn about the vertical, degrees */
  rotation_deg?: number;
}

const parsed = new Map<string, THREE.Group>();

/** the model's meshes as parsed (shared by every part that uses it); null when it does not parse */
function parse(text: string): THREE.Group | null {
  const known = parsed.get(text);
  if (known) return known;
  try {
    const scene = new VRMLLoader().parse(text, '');
    const g = new THREE.Group();
    g.add(...scene.children);
    // the bench lights physically: Phong's specular becomes metal, its shininess smoothness
    g.traverse((o) => {
      const m = o as THREE.Mesh;
      if (!m.isMesh) return;
      const old = m.material as THREE.MeshPhongMaterial;
      if (old?.isMeshPhongMaterial) {
        m.material = new THREE.MeshStandardMaterial({ color: old.color, ...physical(old), transparent: old.transparent, opacity: old.opacity });
        old.dispose();
      }
      m.castShadow = true;
      m.receiveShadow = true;
    });
    parsed.set(text, g);
    return g;
  } catch {
    return null;
  }
}

/**
 * Phong's terms as metal and roughness. The file's colours are display
 * values (the loader holds them linear, so they are read back as written);
 * VRML's shininess runs from 0 to 1. A grey with a bright, tight specular
 * is a metal (KiCad's tin and nickel pins); everything else is a dielectric
 * (its dark plastics have a bright but broad specular).
 */
export function physical(m: THREE.MeshPhongMaterial): { roughness: number; metalness: number } {
  const spec = m.specular.clone().convertLinearToSRGB();
  const c = m.color.clone().convertLinearToSRGB();
  const level = (spec.r + spec.g + spec.b) / 3;
  const sat = Math.max(c.r, c.g, c.b) - Math.min(c.r, c.g, c.b);
  return { roughness: Math.min(0.9, Math.max(0.15, 1 - m.shininess)), metalness: level > 0.2 && m.shininess >= 0.5 && sat < 0.25 ? 0.85 : 0 };
}

/**
 * The model placed for a part whose first pin is at `pin1` (x, z in the
 * part's own frame, mm), or null when the file does not parse (the bench
 * then keeps the body it builds).
 */
export function modelBody(model: Model3d, pin1: [number, number]): THREE.Group | null {
  const source = parse(model.text);
  if (!source) return null;
  const s = model.scale ?? 2.54;
  // Z-up model units to Y-up mm: (x, y, z) -> (x, z, -y), scaled
  const standing = new THREE.Group();
  standing.rotation.x = -Math.PI / 2;
  standing.scale.setScalar(s);
  standing.add(source.clone(true));
  const turned = new THREE.Group();
  turned.rotation.y = -((model.rotation_deg ?? 0) * Math.PI) / 180;
  turned.add(standing);
  const [ox, oy, oz] = model.offset_mm ?? [0, 0, 0];
  turned.position.set(pin1[0] + ox, oz, pin1[1] + oy);
  const out = new THREE.Group();
  out.name = 'model3d';
  out.add(turned);
  return out;
}
