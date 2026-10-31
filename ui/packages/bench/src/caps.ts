// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Capped sections (docs/engineering/bench-3d.md, stage 1's later work): a
// cut body shows a solid face where the plane cuts it, as a CAD tool's
// section view does, instead of its hollow inside. The usual stencil method:
// every cut solid is drawn twice into the stencil buffer only, its back
// faces counting up and its front faces counting down (both clipped by the
// plane); where the count is not zero the plane is inside a solid, and the
// cap, a plane on the cut, is drawn there. Only closed bodies take part:
// flat label plates, lines and sprites are left out.

import * as THREE from 'three';

/** the cut faces' colour, as CAD tools hatch a section */
export const CAP_COLOR = 0xd9534f;

function stencilMaterial(side: THREE.Side, op: THREE.StencilOp, plane: THREE.Plane): THREE.MeshBasicMaterial {
  return new THREE.MeshBasicMaterial({
    side,
    depthWrite: false,
    depthTest: false,
    colorWrite: false,
    stencilWrite: true,
    stencilFunc: THREE.AlwaysStencilFunc,
    stencilFail: op,
    stencilZFail: op,
    stencilZPass: op,
    clippingPlanes: [plane],
  });
}

/** whether a mesh is a closed body (not a flat plate) that should be capped */
function solid(m: THREE.Mesh): boolean {
  const g = m.geometry;
  if (g instanceof THREE.PlaneGeometry || g instanceof THREE.ShapeGeometry || g instanceof THREE.CircleGeometry) return false;
  // a body with a material per face (a breadboard, a board) is still one closed body: the helper draws it whole
  const mat = (Array.isArray(m.material) ? m.material[0] : m.material) as THREE.Material | undefined;
  if (!mat || mat.visible === false || mat.transparent) return false;
  return Array.isArray(m.material) || !(mat as THREE.MeshBasicMaterial).map;
}

export interface Caps {
  /** the cap plane, added to the scene */
  plane: THREE.Mesh;
  /** removes every stencil mesh and the cap */
  dispose(): void;
}

/** caps the solids under `roots` where `plane` cuts them; `extent` is the cap's size (mm) */
export function capSection(roots: THREE.Object3D[], plane: THREE.Plane, scene: THREE.Scene, extent: number): Caps {
  const back = stencilMaterial(THREE.BackSide, THREE.IncrementWrapStencilOp, plane);
  const front = stencilMaterial(THREE.FrontSide, THREE.DecrementWrapStencilOp, plane);
  const added: THREE.Mesh[] = [];
  for (const root of roots) {
    const targets: THREE.Mesh[] = [];
    root.traverse((o) => { const m = o as THREE.Mesh; if (m.isMesh && !m.userData.stencil && solid(m)) targets.push(m); });
    for (const m of targets) {
      for (const mat of [back, front]) {
        const s = new THREE.Mesh(m.geometry, mat);
        s.userData.stencil = true;
        s.renderOrder = 1;
        s.raycast = () => undefined; // never under the pointer
        m.add(s);
        added.push(s);
      }
    }
  }
  const capMat = new THREE.MeshStandardMaterial({
    color: CAP_COLOR,
    roughness: 0.7,
    stencilWrite: true,
    stencilRef: 0,
    stencilFunc: THREE.NotEqualStencilFunc,
    stencilFail: THREE.ReplaceStencilOp,
    stencilZFail: THREE.ReplaceStencilOp,
    stencilZPass: THREE.ReplaceStencilOp,
  });
  capMat.side = THREE.DoubleSide;
  const cap = new THREE.Mesh(new THREE.PlaneGeometry(extent, extent), capMat);
  // on the plane, facing the kept side
  plane.coplanarPoint(cap.position);
  cap.lookAt(cap.position.clone().add(plane.normal));
  cap.renderOrder = 1.1;
  cap.userData.stencil = true;
  cap.raycast = () => undefined;
  cap.onAfterRender = (renderer) => renderer.clearStencil();
  scene.add(cap);
  return {
    plane: cap,
    dispose() {
      for (const s of added) s.removeFromParent();
      cap.removeFromParent();
      cap.geometry.dispose();
      capMat.dispose();
      back.dispose();
      front.dispose();
    },
  };
}
