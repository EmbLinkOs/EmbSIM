// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Sprite, SpriteMaterial } from 'three';
import { describe, expect, it } from 'vitest';
import { updateEmission } from './emission';
import { buildPart } from './parts3d';

describe('engine-driven LED display', () => {
  it('changes the actual RGB dome, light and halo together and extinguishes all three', () => {
    const built = buildPart('D1', { package: 'led', size: [8, 6, 9], pins: {} }, { component: 'emblink/rgb_led' });
    built.halo = new Sprite(new SpriteMaterial());
    updateEmission(built, { luminance: 0.125, color: '#ff00ff', lit: true });
    expect(built.glow?.emissive.getHexString()).toBe('ff00ff');
    expect(built.light?.color.getHexString()).toBe('ff00ff');
    expect((built.halo.material as SpriteMaterial).color.getHexString()).toBe('ff00ff');
    expect(built.light?.intensity).toBe(225);
    expect(built.halo.visible).toBe(true);
    updateEmission(built, { luminance: 0, color: '#000000', lit: false });
    expect(built.glow?.emissiveIntensity).toBe(0);
    expect(built.light?.intensity).toBe(0);
    expect(built.halo.visible).toBe(false);
  });
  it('uses boolean indication only when numeric luminance is absent', () => {
    const built = buildPart('D1', { package: 'led', size: [5, 5, 9], pins: {} }, { component: 'emblink/led' });
    updateEmission(built, { lit: true });
    expect(built.light?.intensity).toBe(450);
    updateEmission(built, { lit: true, luminance: Number.NaN });
    expect(built.light?.intensity).toBe(0);
    updateEmission(built, { lit: true, luminance: 0 });
    expect(built.light?.intensity).toBe(0);
  });
});
