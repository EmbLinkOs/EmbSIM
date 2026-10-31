// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { pcbBody } from './packages3d';
import { artworkBoard } from './shape';

const boards = resolve(__dirname, '../../../../catalog/boards');
const art = (b: string) => readFileSync(resolve(boards, b, 'artwork.svg'), 'utf-8');

describe('a board\'s PCB from its artwork', () => {
  it('reads the Uno\'s outline, its teal mask and its four 3.2 mm mounting holes', () => {
    const b = artworkBoard(art('arduino_uno'), 68.58, 53.34);
    expect(b.mask).toBe('#00878f');
    const xs = b.outline!.map(([x]) => x);
    const ys = b.outline!.map(([, y]) => y);
    expect(Math.min(...xs)).toBe(0);
    expect(Math.max(...xs)).toBeCloseTo(68.58, 2);
    expect(Math.max(...ys)).toBeCloseTo(53.34, 2);
    // the chamfers and the step on the right edge: more than four corners
    expect(b.outline!.length).toBeGreaterThan(4);
    expect(b.holes).toHaveLength(4);
    expect(b.holes![0]).toEqual({ at: [15.24, 2.54], r: 2 });
  });

  it('reads a rounded rectangle when the PCB is drawn as one (the ESP32-C3 DevKit)', () => {
    const b = artworkBoard(art('esp32c3_devkitm1'), 25.4, 38.91);
    expect(b.mask).toBe('#1c1c1e');
    expect(b.outline!.length).toBe(28);
    expect(b.holes).toBeUndefined();
  });
});

describe('the PCB body', () => {
  it('is the outline\'s size and thickness, with a plated barrel in each hole', () => {
    const b = artworkBoard(art('arduino_uno'), 68.58, 53.34);
    const g = pcbBody({ w: 68.58, d: 53.34, thickness: 1.6, ...b });
    const size = new THREE.Box3().setFromObject(g).getSize(new THREE.Vector3());
    expect(size.x).toBeCloseTo(68.58, 1);
    expect(size.z).toBeCloseTo(53.34, 1);
    expect(size.y).toBeCloseTo(1.6 + 0.04, 1);
    let barrels = 0;
    g.traverse((o) => { const m = o as THREE.Mesh; if (m.isMesh && m.geometry instanceof THREE.CylinderGeometry) barrels++; });
    expect(barrels).toBe(4);
  });

  it('maps the artwork so a pointer finds its hotspots: u right, v up from the person\'s edge', () => {
    const g = pcbBody({ w: 60, d: 40, thickness: 1.6, top: new THREE.Texture() });
    g.updateMatrixWorld(true);
    // a point 6 mm from the left edge and 4 mm from the far edge, seen from above
    const ray = new THREE.Raycaster(new THREE.Vector3(-30 + 6, 10, -20 + 4), new THREE.Vector3(0, -1, 0));
    const hit = ray.intersectObject(g, true).find((h) => (h.object as THREE.Mesh).material instanceof THREE.MeshPhysicalMaterial && h.face!.normal.y > 0.9);
    expect(hit?.uv?.x).toBeCloseTo(6 / 60, 3);
    // the bench reads the artwork at (u, 1 - v): 4 mm down from its top edge
    expect(1 - hit!.uv!.y).toBeCloseTo(4 / 40, 3);
  });
});

