// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { buildRoom, lightLevel, ROOM, TABLE, WINDOW } from './room';

describe('the room around the bench', () => {
  it('the table\'s top is the bench\'s surface, 750 mm above the floor', () => {
    const { group } = buildRoom();
    const b = new THREE.Box3().setFromObject(group);
    expect(b.min.y).toBeCloseTo(-TABLE.height);
    expect(b.max.y).toBeCloseTo(ROOM.height - TABLE.height);
    let top: THREE.Box3 | undefined;
    group.traverse((o) => {
      const m = o as THREE.Mesh;
      if (m.isMesh && m.geometry instanceof THREE.BoxGeometry && m.geometry.parameters.width === TABLE.width) top = new THREE.Box3().setFromObject(m);
    });
    expect(top!.max.y).toBeLessThan(0);
    expect(top!.max.y).toBeGreaterThan(-1);
  });

  it('the window is a hole through the left wall: a ray from the table to the pane meets no wall', () => {
    const { group } = buildRoom();
    group.updateMatrixWorld(true);
    const from = new THREE.Vector3(0, 300, WINDOW.z);
    const ray = new THREE.Raycaster(from, new THREE.Vector3(-1, 0, 0));
    const walls = group.children.filter((o) => (o as THREE.Mesh).geometry instanceof THREE.ShapeGeometry);
    expect(walls).toHaveLength(1);
    expect(ray.intersectObjects(walls, false)).toHaveLength(0);
    // and beside the window, the wall is there
    const beside = new THREE.Raycaster(new THREE.Vector3(0, 300, WINDOW.z + WINDOW.width), new THREE.Vector3(-1, 0, 0));
    beside.params.Mesh = {};
    const hit = beside.intersectObjects(walls, false);
    expect(hit.length).toBeGreaterThan(0);
  });

  it('lights the room by the world\'s illuminance: 300 lx as always, darker and brighter around it', () => {
    expect(lightLevel(300)).toBeCloseTo(1);
    expect(lightLevel(undefined)).toBeCloseTo(1);
    expect(lightLevel(0)).toBeCloseTo(0.06);
    expect(lightLevel(10)).toBeLessThan(0.5);
    expect(lightLevel(100000)).toBe(1.5);
    expect(lightLevel(1000)).toBeGreaterThan(1);
  });
});
