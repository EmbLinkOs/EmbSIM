// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Static batching (docs/engineering/bench-3d.md, stage 7): a board's headers,
// leads and pins are hundreds of small meshes in a few shared materials that
// never change (tin, gold plate, black plastic, the mould). Each would be a
// draw call, twice with shadows. Once a part is built, the meshes in one such
// material are merged into one mesh, their transforms baked in. What the
// bench refers to stays separate: a control (a button's cap, a knob), anything
// carrying a gesture or a hotspot, named meshes, and every material that is
// set from the circuit (LEDs, displays, labels).

import * as THREE from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

/** merges `group`'s meshes in the `statics` materials, per material; returns how many meshes it removed */
export function batchStatic(group: THREE.Object3D, statics: ReadonlySet<THREE.Material>, keep?: THREE.Object3D): number {
  group.updateMatrixWorld(true);
  const inverse = group.matrixWorld.clone().invert();
  const kept = new Set<THREE.Object3D>();
  keep?.traverse((o) => kept.add(o));
  const buckets = new Map<string, { material: THREE.Material; meshes: THREE.Mesh[] }>();
  group.traverse((o) => {
    const m = o as THREE.Mesh;
    if (!m.isMesh || m === group || kept.has(m) || m.name || Array.isArray(m.material)) return;
    if (!statics.has(m.material as THREE.Material)) return;
    if (Object.keys(m.userData).length > 0 || (m as THREE.InstancedMesh).isInstancedMesh) return;
    const geo = m.geometry;
    // geometries merge only with the same attributes, all indexed or none
    const key = `${(m.material as THREE.Material).uuid}|${geo.index ? 'i' : 'n'}|${Object.keys(geo.attributes).sort().join(',')}|${m.castShadow ? 1 : 0}${m.receiveShadow ? 1 : 0}`;
    const b = buckets.get(key) ?? { material: m.material as THREE.Material, meshes: [] };
    b.meshes.push(m);
    buckets.set(key, b);
  });
  let removed = 0;
  for (const { material, meshes } of buckets.values()) {
    if (meshes.length < 2) continue;
    const parts = meshes.map((m) => m.geometry.clone().applyMatrix4(new THREE.Matrix4().multiplyMatrices(inverse, m.matrixWorld)));
    const merged = mergeGeometries(parts, false);
    for (const p of parts) p.dispose();
    if (!merged) continue;
    const one = new THREE.Mesh(merged, material);
    one.castShadow = meshes[0]!.castShadow;
    one.receiveShadow = meshes[0]!.receiveShadow;
    one.userData.batched = meshes.length;
    for (const m of meshes) {
      m.removeFromParent();
      m.geometry.dispose();
    }
    group.add(one);
    removed += meshes.length - 1;
  }
  return removed;
}
