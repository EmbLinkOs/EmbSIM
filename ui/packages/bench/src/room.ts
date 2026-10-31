// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The room around the bench (docs/engineering/bench-3d.md, stage 6): the
// bench's surface is the top of a lab table (750 mm high, 1800 x 900 mm, as
// EN 14056 lab benches and office desks are), standing on a floor in a room
// with a window on the left, where the key light comes from. The walls face
// inwards: one between the camera and the table is seen from behind and not
// drawn, so it never hides the bench. How lit the room is follows the world's
// illuminance (ADR-026), the value the light sensors read: an office at
// 300 lx looks as the bench always has, a dark room leaves the LEDs glowing,
// daylight is brighter. Presentation only; the room is not part of the
// assembly (exports, framing and the height check leave it out).

import * as THREE from 'three';

/** mm: the table's top is the bench's y = 0 */
export const TABLE = { width: 1800, depth: 900, thickness: 30, height: 750 };
export const ROOM = { width: 5200, depth: 4400, height: 2700 };
/** the window in the left wall: its centre along the wall (z), its sill above the floor, its size */
export const WINDOW = { z: 300, sill: 900, width: 1600, height: 1300 };

export interface RoomSize {
  width: number;
  depth: number;
  height: number;
}

/** the window of a room this size: as WINDOW, narrowed and lowered to fit a small room's wall */
export function windowOf(room: RoomSize): typeof WINDOW {
  const width = Math.min(WINDOW.width, room.depth - 1000);
  const height = Math.min(WINDOW.height, room.height - WINDOW.sill - 400);
  return { z: Math.max(-room.depth / 2 + 500 + width / 2, Math.min(room.depth / 2 - 500 - width / 2, WINDOW.z)), sill: WINDOW.sill, width, height };
}

/** the window as the sun sees it: the wall's x, and the opening relative to the bench's surface */
export function windowOpening(room: RoomSize): { wallX: number; z: number; width: number; bottom: number; top: number } {
  const w = windowOf(room);
  return { wallX: -room.width / 2, z: w.z, width: w.width, bottom: w.sill - TABLE.height, top: w.sill + w.height - TABLE.height };
}

/** the office lighting the bench was tuned for (EN 12464-1's 300 to 500 lx for desk work) */
export const REFERENCE_LUX = 300;

/**
 * The scene's light, relative to the bench's usual lighting, for an
 * illuminance in lux. Brightness is perceived roughly logarithmically, so
 * the scale is too: 300 lx is 1, a dark room keeps a little so the bench
 * stays visible beside its LEDs, daylight up to half again as bright (tone
 * mapping compresses the rest, as an eye adapts).
 */
export function lightLevel(lux: number | undefined): number {
  const lx = Math.max(0, Number.isFinite(lux) ? (lux as number) : REFERENCE_LUX);
  return Math.min(1.5, Math.max(0.06, Math.log10(lx + 1) / Math.log10(REFERENCE_LUX + 1)));
}

function plane(w: number, h: number, material: THREE.Material): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.PlaneGeometry(w, h), material);
  m.receiveShadow = true;
  // walls and ceiling stop the sun, so only what comes through the window reaches the table
  m.castShadow = true;
  return m;
}

export interface Room {
  group: THREE.Group;
  /** the window's pane: it glows with the daylight outside */
  pane: THREE.MeshBasicMaterial;
  dispose(): void;
}

export function buildRoom(size: RoomSize = ROOM): Room {
  const ROOM = size;
  const WINDOW = windowOf(size);
  const g = new THREE.Group();
  g.name = 'room';
  const floorY = -TABLE.height;
  const wallMat = new THREE.MeshStandardMaterial({ color: 0xd9d6cf, roughness: 0.95 });
  const floorMat = new THREE.MeshStandardMaterial({ color: 0x8a8580, roughness: 0.85 });
  const ceilingMat = new THREE.MeshStandardMaterial({ color: 0xeeece8, roughness: 1 });
  const laminate = new THREE.MeshStandardMaterial({ color: 0xc9c4ba, roughness: 0.6 });
  const steel = new THREE.MeshStandardMaterial({ color: 0x4b5157, roughness: 0.45, metalness: 0.6 });
  const pane = new THREE.MeshBasicMaterial({ color: 0xdfeaf5 });
  const frame = new THREE.MeshStandardMaterial({ color: 0xf2f2f0, roughness: 0.5 });

  // the table: a laminate top on four steel legs, its top face just under the bench's surface
  const top = new THREE.Mesh(new THREE.BoxGeometry(TABLE.width, TABLE.thickness, TABLE.depth), laminate);
  top.position.y = -0.6 - TABLE.thickness / 2;
  top.receiveShadow = true;
  g.add(top);
  const legH = TABLE.height - TABLE.thickness - 0.6;
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    const leg = new THREE.Mesh(new THREE.BoxGeometry(50, legH, 50), steel);
    leg.position.set(sx * (TABLE.width / 2 - 60), floorY + legH / 2, sz * (TABLE.depth / 2 - 60));
    g.add(leg);
  }
  // a rail between the back legs, as lab benches have
  const rail = new THREE.Mesh(new THREE.BoxGeometry(TABLE.width - 120, 60, 30), steel);
  rail.position.set(0, floorY + 250, -(TABLE.depth / 2 - 60));
  g.add(rail);

  // the floor and the ceiling
  const floor = plane(ROOM.width, ROOM.depth, floorMat);
  floor.rotation.x = -Math.PI / 2;
  floor.position.set(0, floorY, 0);
  g.add(floor);
  const ceiling = plane(ROOM.width, ROOM.depth, ceilingMat);
  ceiling.rotation.x = Math.PI / 2;
  ceiling.position.set(0, floorY + ROOM.height, 0);
  g.add(ceiling);

  // walls, each facing into the room
  const midY = floorY + ROOM.height / 2;
  const back = plane(ROOM.width, ROOM.height, wallMat);
  back.position.set(0, midY, -ROOM.depth / 2);
  g.add(back);
  const front = plane(ROOM.width, ROOM.height, wallMat);
  front.rotation.y = Math.PI;
  front.position.set(0, midY, ROOM.depth / 2);
  g.add(front);
  const right = plane(ROOM.depth, ROOM.height, wallMat);
  right.rotation.y = -Math.PI / 2;
  right.position.set(ROOM.width / 2, midY, 0);
  g.add(right);
  // the left wall, with the window cut through it
  const shape = new THREE.Shape();
  shape.moveTo(-ROOM.depth / 2, -ROOM.height / 2);
  shape.lineTo(ROOM.depth / 2, -ROOM.height / 2);
  shape.lineTo(ROOM.depth / 2, ROOM.height / 2);
  shape.lineTo(-ROOM.depth / 2, ROOM.height / 2);
  shape.closePath();
  // in the wall's own frame x runs along -z of the room (it faces +x)
  const wx = -WINDOW.z;
  const wy = WINDOW.sill + WINDOW.height / 2 - ROOM.height / 2;
  const hole = new THREE.Path();
  hole.moveTo(wx - WINDOW.width / 2, wy - WINDOW.height / 2);
  hole.lineTo(wx - WINDOW.width / 2, wy + WINDOW.height / 2);
  hole.lineTo(wx + WINDOW.width / 2, wy + WINDOW.height / 2);
  hole.lineTo(wx + WINDOW.width / 2, wy - WINDOW.height / 2);
  hole.closePath();
  shape.holes.push(hole);
  const left = new THREE.Mesh(new THREE.ShapeGeometry(shape), wallMat);
  left.receiveShadow = true;
  left.castShadow = true;
  left.rotation.y = Math.PI / 2;
  left.position.set(-ROOM.width / 2, midY, 0);
  g.add(left);
  // the pane: the daylight outside, a little behind the wall; and the frame's bars
  const glass = plane(WINDOW.width, WINDOW.height, pane);
  glass.castShadow = false; // the pane lets the light through
  glass.rotation.y = Math.PI / 2;
  glass.position.set(-ROOM.width / 2 - 40, floorY + WINDOW.sill + WINDOW.height / 2, WINDOW.z);
  g.add(glass);
  const bars: [number, number, number, number][] = [
    [WINDOW.width, 50, 0, WINDOW.height / 2], [WINDOW.width, 50, 0, -WINDOW.height / 2],
    [50, WINDOW.height, WINDOW.width / 2, 0], [50, WINDOW.height, -WINDOW.width / 2, 0], [40, WINDOW.height, 0, 0],
  ];
  for (const [w, h, dz, dy] of bars) {
    const bar = new THREE.Mesh(new THREE.BoxGeometry(60, h, w), frame);
    bar.castShadow = true; // the frame's bars cross the sunlit patch
    bar.position.set(-ROOM.width / 2 - 20, floorY + WINDOW.sill + WINDOW.height / 2 + dy, WINDOW.z + dz);
    g.add(bar);
  }

  return {
    group: g,
    pane,
    dispose() {
      g.traverse((o) => { if ((o as THREE.Mesh).isMesh) (o as THREE.Mesh).geometry.dispose(); });
      for (const m of [wallMat, floorMat, ceilingMat, laminate, steel, pane, frame]) m.dispose();
    },
  };
}
