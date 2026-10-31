// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { dayOfYear, solarPosition, sunColor, sunDirection, sunlit } from './sun';

describe('the sun', () => {
  it('reads a date as its day of the year', () => {
    expect(dayOfYear('01-01')).toBe(1);
    expect(dayOfYear('06-21')).toBe(172);
    expect(dayOfYear('12-21')).toBe(355);
    expect(dayOfYear('bad')).toBe(172);
  });

  it('stands at 90 - latitude + declination at solar noon (40° N: 73.4° in June, 26.6° in December)', () => {
    expect(solarPosition(40, 172, 12).elevation).toBeCloseTo(73.44, 0);
    expect(solarPosition(40, 355, 12).elevation).toBeCloseTo(26.56, 0);
    // due south at noon in the northern hemisphere, due north in the southern
    expect(solarPosition(40, 172, 12).azimuth).toBeCloseTo(180, 0);
    expect(solarPosition(-34, 172, 12).azimuth % 360).toBeCloseTo(0, 0);
  });

  it('rises in the east at 6 h and sets in the west at 18 h at an equinox, and is down at midnight', () => {
    const am = solarPosition(0, 80, 6);
    expect(Math.abs(am.elevation)).toBeLessThan(1.5);
    expect(am.azimuth).toBeCloseTo(90, -1);
    const pm = solarPosition(0, 80, 18);
    expect(pm.azimuth).toBeCloseTo(270, -1);
    expect(solarPosition(48, 172, 0).elevation).toBeLessThan(0);
  });

  it('points through a south-facing window from the south at noon', () => {
    const d = sunDirection({ elevation: 30, azimuth: 180 }, 180);
    expect(d[0]).toBeCloseTo(-Math.cos(Math.PI / 6));
    expect(d[1]).toBeCloseTo(0.5);
    expect(d[2]).toBeCloseTo(0);
  });

  it('lights the table only through the window, not through a wall or from below the horizon', () => {
    const w = { wallX: -2600, z: 300, width: 1600, bottom: 150, top: 1450 };
    const low = sunDirection({ elevation: 20, azimuth: 180 }, 180);
    expect(sunlit(low, [0, 0, 300], w)).toBe(true); // 2600 tan 20° = 946 mm: through the opening
    const high = sunDirection({ elevation: 60, azimuth: 180 }, 180);
    expect(sunlit(high, [0, 0, 300], w)).toBe(false); // over the top of the window: the wall above it
    const behind = sunDirection({ elevation: 20, azimuth: 0 }, 180);
    expect(sunlit(behind, [0, 0, 300], w)).toBe(false);
    expect(sunlit(sunDirection({ elevation: -5, azimuth: 180 }, 180), [0, 0, 300], w)).toBe(false);
  });

  it('is warmer near the horizon', () => {
    expect(sunColor(5)[2]).toBeLessThan(sunColor(45)[2]);
  });
});
