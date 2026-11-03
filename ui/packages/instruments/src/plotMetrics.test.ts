// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';
import { plotMetrics, usePlotLook } from './plotMetrics';

describe('plotMetrics', () => {
  it('gives compact plots shorter rows and narrower margins', () => {
    const roomy = plotMetrics('logic', { density: 'comfortable', scale: 1 });
    const tight = plotMetrics('logic', { density: 'compact', scale: 1 });
    expect(roomy).toEqual({ font: 11, row: 30, ann: 18, left: 72, bottom: 22, top: 6 });
    expect(tight.row).toBeLessThan(roomy.row);
    expect(tight.left).toBeLessThan(roomy.left);
    expect(tight.bottom).toBeLessThan(roomy.bottom);
    expect(plotMetrics('analog', { density: 'compact', scale: 1 }).left).toBeLessThan(plotMetrics('analog', { density: 'comfortable', scale: 1 }).left);
  });

  it('scales with the text size, in whole pixels', () => {
    const large = plotMetrics('logic', { density: 'compact', scale: 1.12 });
    expect(large.row).toBe(Math.round(20 * 1.12));
    expect(Number.isInteger(large.font)).toBe(true);
    expect(plotMetrics('logic', { density: 'compact', scale: Number.NaN }).row).toBe(20);
  });
});

describe('usePlotLook', () => {
  afterEach(() => {
    delete document.documentElement.dataset['density'];
  });

  it('follows the density the settings put on <html>', async () => {
    const { result } = renderHook(() => usePlotLook());
    expect(result.current.density).toBe('comfortable');
    await act(async () => {
      document.documentElement.dataset['density'] = 'compact';
      await Promise.resolve();
    });
    expect(result.current.density).toBe('compact');
  });
});
