// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A person in the world (ADR-026), drawn as a person rather than as the box
// the sensors see (docs/engineering/bench-3d.md, stage 6). The figure fills
// the body's box with an adult's proportions (head about an eighth of the
// height, legs about half, shoulders about a quarter wide), faces the way
// they walk, and swings legs and arms with the distance walked: a stride of
// about 0.75 m, so the gait follows virtual time as the body does. The box
// stays what the distance and PIR sensors see; this is presentation only.

import * as THREE from 'three';
import type { WorldBody } from '@embsim/protocol';

/** a stride (one step of each foot), mm */
export const STRIDE = 1500;

/** whether a body is drawn as a person: warm, a box, of a person's size */
export function isPerson(b: WorldBody): boolean {
  const [w, d, h] = b.size;
  return b.warm === true && b.shape !== 'sphere' && h >= 1000 && h <= 2200 && w <= 900 && d <= 900;
}

/** how far through its stride (0 to 1) a walking body is, from where it has got to along its velocity */
export function gaitPhase(b: WorldBody): number {
  const v = b.velocity_mm_s;
  const speed = v ? Math.hypot(v[0], v[1], v[2]) : 0;
  if (!v || speed <= 0) return 0;
  const along = (b.at[0] * v[0] + b.at[1] * v[1] + b.at[2] * v[2]) / speed;
  return (((along / STRIDE) % 1) + 1) % 1;
}

function limb(radius: number, length: number, material: THREE.Material): THREE.Mesh {
  // a capsule hanging down from its top (the hip or the shoulder)
  const g = new THREE.CapsuleGeometry(radius, Math.max(1, length - 2 * radius), 4, 12);
  g.translate(0, -length / 2, 0);
  const m = new THREE.Mesh(g, material);
  m.castShadow = true;
  m.receiveShadow = true;
  return m;
}

/**
 * The person for a body: a group in scene units (mm, y up) centred on the
 * body's box, its feet at the box's bottom, facing its walk (or the bench, +z).
 */
export function personFigure(b: WorldBody, materials: { clothes: THREE.Material; legs: THREE.Material; skin: THREE.Material }): THREE.Group {
  const [w, d, h] = b.size;
  const g = new THREE.Group();
  const figure = new THREE.Group();
  g.add(figure);
  const head = h / 8;
  const legLen = h * 0.47;
  const torsoLen = h - legLen - head * 1.05;
  // shoulders about 0.23 of the height; with the arms hanging beside them the figure is about 1.5 times as wide, within the box
  const shoulders = Math.min(w * 0.66, h * 0.23);
  const depth = Math.min(d, h * 0.13);
  const base = -h / 2; // the feet, from the box's centre
  // legs, swinging from the hips
  const phase = gaitPhase(b) * Math.PI * 2;
  const swing = b.velocity_mm_s ? 0.45 : 0;
  const hip = base + legLen;
  for (const side of [-1, 1]) {
    const leg = limb(shoulders * 0.13, legLen, materials.legs);
    leg.position.set(side * shoulders * 0.2, hip, 0);
    leg.rotation.x = side * swing * Math.sin(phase);
    figure.add(leg);
  }
  // the torso: rounded, a little narrower at the waist
  const torso = new THREE.Mesh(new THREE.CapsuleGeometry(depth / 2, Math.max(1, torsoLen - depth), 4, 16), materials.clothes);
  torso.scale.set(shoulders / depth, 1, 1);
  torso.position.set(0, hip + torsoLen / 2, 0);
  torso.castShadow = true;
  figure.add(torso);
  // arms from the shoulders, swinging against the legs
  const shoulder = hip + torsoLen - depth * 0.3;
  for (const side of [-1, 1]) {
    const arm = limb(shoulders * 0.09, h * 0.36, materials.clothes);
    arm.position.set(side * (shoulders / 2 + shoulders * 0.06), shoulder, 0);
    arm.rotation.x = -side * swing * 0.8 * Math.sin(phase);
    arm.rotation.z = side * 0.03; // hanging a little clear of the body
    figure.add(arm);
  }
  // the head
  const skull = new THREE.Mesh(new THREE.SphereGeometry(head * 0.5, 20, 14), materials.skin);
  skull.scale.set(0.85, 1, 0.95);
  skull.position.set(0, h / 2 - head * 0.5, 0);
  skull.castShadow = true;
  figure.add(skull);
  // facing: along the walk, else towards the bench (the scene's +z is the bench's y towards the viewer)
  const v = b.velocity_mm_s;
  if (v && (v[0] !== 0 || v[1] !== 0)) figure.rotation.y = Math.atan2(v[0], v[1]);
  return g;
}
