// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { describeSun } from './WorldPanel';

describe('the sun in words', () => {
  it('says where the sun is and whether it shines on the bench', () => {
    // a winter morning at 48° N, the window looking towards it (150°): low sun, straight in
    expect(describeSun({ latitude_deg: 48, date: '12-21', solar_time_h: 10 }, { window_azimuth_deg: 150 })).toMatch(/^Sun \d+° up in the south-east: shining on the bench/);
    // the same sun with the window looking north
    expect(describeSun({ latitude_deg: 48, date: '12-21', solar_time_h: 10 }, { window_azimuth_deg: 0 })).toContain('no direct sun');
    expect(describeSun({ latitude_deg: 48, date: '06-21', solar_time_h: 0 }, undefined)).toMatch(/^Night/);
  });
});
