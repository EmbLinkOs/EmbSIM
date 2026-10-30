// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as THREE from 'three';
import { describe, expect, it } from 'vitest';
import { barrelJack, crystalHC49, dipBody, dipDimensions, dipLabelSize, dipPinPositions, electrolytic, headerStrips, pinStrip, quadPackage, quadSize, rfModule, socketStrip, tactileSwitch, usbB, usbC, usbMicro } from './packages3d';

function size(o: THREE.Object3D): THREE.Vector3 {
  return new THREE.Box3().setFromObject(o).getSize(new THREE.Vector3());
}
function bounds(o: THREE.Object3D): THREE.Box3 {
  return new THREE.Box3().setFromObject(o);
}

describe('DIP (JEDEC MS-001, Microchip 28P3)', () => {
  it('takes the standard\'s nominal body', () => {
    const d = dipDimensions(28);
    expect(d.length).toBe(34.67);
    expect(d.width).toBe(7.3);
    expect(d.rows).toBe(7.62);
    expect(dipDimensions(8).length).toBe(9.27);
    expect(dipDimensions(40).rows).toBe(15.24);
  });

  it('numbers its pins counter-clockwise from pin 1 at the bottom left, 2.54 mm apart', () => {
    const p = dipPinPositions(8);
    expect(p[0]).toEqual([-3.81, 3.81]);
    expect(p[3]).toEqual([3.81, 3.81]);
    expect(p[4]).toEqual([3.81, -3.81]);
    expect(p[7]).toEqual([-3.81, -3.81]);
  });

  it('puts every lead\'s tip on its pin, below the seating plane', () => {
    const g = dipBody(28);
    g.updateMatrixWorld(true);
    const tips: [number, number][] = [];
    g.traverse((o) => {
      const m = o as THREE.Mesh;
      if (!m.isMesh) return;
      const b = new THREE.Box3().setFromObject(m);
      if (b.max.y <= 0.01 && b.min.y < -3) tips.push([Math.round(((b.min.x + b.max.x) / 2) * 100) / 100, Math.round(((b.min.z + b.max.z) / 2) * 100) / 100]);
    });
    const want = dipPinPositions(28).map(([x, z]) => [Math.round(x * 100) / 100, Math.round(z * 100) / 100]);
    expect(tips.sort()).toEqual(want.sort());
  });

  it('stands its body on the standoff, as long as the standard says', () => {
    const b = bounds(dipBody(28));
    expect(b.max.y).toBeCloseTo(0.51 + 3.3, 1);
    expect(b.min.y).toBeCloseTo(-3.3, 1);
    expect(b.max.x - b.min.x).toBeCloseTo(34.67, 1);
  });
});

describe('headers', () => {
  it('groups pins into straight strips per row', () => {
    const pins: [number, number][] = [[0, 0], [2.54, 0], [5.08, 0], [10.16, 0], [12.7, 0], [0, 50]];
    expect(headerStrips(pins).map((s) => s.length).sort()).toEqual([1, 2, 3]);
  });
  it('a socket strip is n positions long, 2.54 wide and 8.5 high', () => {
    const s = size(socketStrip(8));
    expect(s.x).toBeCloseTo(8 * 2.54, 2);
    expect(s.z).toBeCloseTo(2.54, 2);
    expect(s.y).toBeCloseTo(8.5, 2);
  });
  it('a pin strip\'s posts stand 6 mm above the spacer and 3 mm below', () => {
    const b = bounds(pinStrip(4));
    expect(b.max.y).toBeCloseTo(2.5 + 6, 1);
    expect(b.min.y).toBeCloseTo(-3, 1);
  });
});

describe('connectors and parts', () => {
  it('the USB-B shell is 12.0 x 10.9 mm and as deep as asked', () => {
    const s = size(usbB(16.5));
    expect(s.x).toBeCloseTo(16.5, 1);
    expect(s.y).toBeCloseTo(10.9, 1);
    expect(s.z).toBeCloseTo(12.0, 1);
  });
  it('the barrel jack, crystal and tactile switch keep their outlines', () => {
    const j = size(barrelJack(14, 9, 11));
    expect([j.x, j.y, j.z].map((v) => Math.round(v * 10) / 10)).toEqual([14, 11, 9]);
    const c = size(crystalHC49());
    expect(c.x).toBeCloseTo(11.05, 1);
    expect(c.z).toBeCloseTo(4.65, 1);
    expect(c.y).toBeCloseTo(3.5, 1);
    const t = tactileSwitch(6, 4.3);
    expect(size(t.group).y).toBeCloseTo(4.3, 1);
    expect(t.cap.name).toBe('cap');
  });
  it('a QFP has a lead per pin, on four sides', () => {
    let leads = 0;
    quadPackage('qfp', 7, 7, 1.2, 32).traverse((o) => { if ((o as THREE.Mesh).isMesh && (o as THREE.Mesh).geometry instanceof THREE.BoxGeometry && ((o as THREE.Mesh).geometry as THREE.BoxGeometry).parameters.height === 0.15) leads++; });
    expect(leads).toBe(32);
  });
});

describe('more packages', () => {
  it('a DIP\'s label fits its own top', () => {
    const [w8, h8] = dipLabelSize(8);
    expect(w8).toBeLessThan(dipDimensions(8).length);
    expect(h8).toBeLessThan(dipDimensions(8).width);
    expect(dipLabelSize(28)[0]).toBeCloseTo(26, 0);
  });
  it('quad packages take the standard body for their pin count', () => {
    expect(quadSize('qfp', 64)).toEqual([10, 10, 1.6]);
    expect(quadSize('qfp', 48)).toEqual([7, 7, 1.6]);
    expect(quadSize('qfn', 28)).toEqual([5, 5, 0.85]);
  });
  it('the micro-USB and USB-C mouths face +z, the board edge', () => {
    for (const g of [usbMicro(), usbC()]) {
      g.updateMatrixWorld(true);
      let mouthZ = -Infinity;
      g.traverse((o) => { const m = o as THREE.Mesh; if (m.isMesh && m.geometry instanceof THREE.ShapeGeometry) mouthZ = new THREE.Box3().setFromObject(m).max.z; });
      expect(mouthZ).toBeGreaterThan(size(g).z / 2 - 0.05);
    }
  });
  it('an RF module keeps its outline, the shield short of the antenna end', () => {
    const g = rfModule(13.2, 16.6, 2.4);
    const s = size(g);
    expect([s.x, s.y, s.z].map((v) => Math.round(v * 10) / 10)).toEqual([13.2, 2.4, 16.6]);
  });
  it('an electrolytic\'s stripe faces the negative lead', () => {
    for (const dir of [0, Math.PI / 2, Math.PI]) {
      const g = electrolytic(5, 11, '#1f3a93', dir);
      g.updateMatrixWorld(true);
      let stripe: THREE.Vector3 | undefined;
      g.traverse((o) => { const m = o as THREE.Mesh; if (m.isMesh && m.geometry instanceof THREE.CylinderGeometry && m.geometry.parameters.openEnded) stripe = new THREE.Box3().setFromObject(m).getCenter(new THREE.Vector3()); });
      expect(stripe).toBeDefined();
      const want = new THREE.Vector3(Math.cos(dir), 0, -Math.sin(dir));
      expect(stripe!.setY(0).normalize().dot(want)).toBeGreaterThan(0.9);
    }
  });
});

