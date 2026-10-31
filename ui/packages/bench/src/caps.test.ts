// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { CAP_COLOR, capSection } from './caps';

describe('capped sections', () => {
  it('draws each closed body twice into the stencil (back faces up, front faces down) and caps the plane', () => {
    const scene = new THREE.Scene();
    const part = new THREE.Group();
    const body = new THREE.Mesh(new THREE.BoxGeometry(10, 10, 10), new THREE.MeshStandardMaterial());
    const label = new THREE.Mesh(new THREE.PlaneGeometry(5, 2), new THREE.MeshBasicMaterial());
    const glass = new THREE.Mesh(new THREE.SphereGeometry(3), new THREE.MeshStandardMaterial({ transparent: true, opacity: 0.5 }));
    part.add(body, label, glass);
    scene.add(part);
    const plane = new THREE.Plane(new THREE.Vector3(0, 0, -1), 2); // keeps z <= 2
    const caps = capSection([part], plane, scene, 500);
    const helpers = body.children as THREE.Mesh[];
    expect(helpers).toHaveLength(2);
    const [back, front] = helpers.map((h) => h.material as THREE.MeshBasicMaterial);
    expect(back!.side).toBe(THREE.BackSide);
    expect(back!.stencilZPass).toBe(THREE.IncrementWrapStencilOp);
    expect(front!.side).toBe(THREE.FrontSide);
    expect(front!.stencilZPass).toBe(THREE.DecrementWrapStencilOp);
    expect(back!.colorWrite).toBe(false);
    expect(back!.clippingPlanes).toEqual([plane]);
    // flat plates and see-through bodies are not capped
    expect(label.children).toHaveLength(0);
    expect(glass.children).toHaveLength(0);
    // the cap lies on the plane and draws only where the stencil is not zero
    expect(plane.distanceToPoint(caps.plane.position)).toBeCloseTo(0);
    const capMat = caps.plane.material as THREE.MeshStandardMaterial;
    expect(capMat.stencilFunc).toBe(THREE.NotEqualStencilFunc);
    expect(capMat.color.getHex()).toBe(new THREE.Color(CAP_COLOR).getHex());
    // never under the pointer
    const hits: THREE.Intersection[] = [];
    caps.plane.raycast(new THREE.Raycaster(new THREE.Vector3(0, 0, 10), new THREE.Vector3(0, 0, -1)), hits);
    expect(hits).toHaveLength(0);
    caps.dispose();
    expect(body.children).toHaveLength(0);
    expect(scene.children).toEqual([part]);
  });
});
