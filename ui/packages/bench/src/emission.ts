// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { OutputValue } from '@embsim/protocol';
import type { SpriteMaterial } from 'three';
import type { Built } from './parts3d';

/** Display only: engine luminance is authoritative; no current is invented here. */
export function updateEmission(built: Built, outputs?: Record<string, OutputValue>): void {
  const lum = outputs?.luminance;
  const level = typeof lum === 'number' ? (Number.isFinite(lum) ? Math.max(0, Math.min(1, lum)) : 0) : outputs?.lit === true ? 1 : 0;
  const bright = Math.cbrt(level);
  const color = outputs?.color;
  if (typeof color === 'string' && /^#[\da-f]{6}$/i.test(color)) {
    built.glow?.emissive.set(color);
    built.light?.color.set(color);
    (built.halo?.material as SpriteMaterial | undefined)?.color.set(color);
  }
  if (built.glow) built.glow.emissiveIntensity = bright * 3.5;
  if (built.light) built.light.intensity = bright * 450;
  if (built.halo) {
    built.halo.visible = bright > 0.02;
    (built.halo.material as SpriteMaterial).opacity = Math.min(0.9, bright);
    built.halo.scale.setScalar(10 + bright * 14);
  }
}
