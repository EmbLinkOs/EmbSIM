// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it } from 'vitest';
import type { ParameterSweep } from '@embsim/protocol';
import { SweepPlot } from './ParameterSweeps';

function result(values: (number | null)[], xs = [1, 2, 3]): ParameterSweep {
  return {request: {probes: ['net:OUT'], parameter: 'resistance'}, points: values.map((v, i) => ({resolved: xs[i]!, ok: v !== null, measurements: [{voltage: v}]}))} as ParameterSweep;
}
describe('measured sweep plots', () => {
  it('does not connect across a failed/missing point', () => {
    const svg = renderToStaticMarkup(<SweepPlot result={result([1, null, 3])} trace={0} unit="Ω" />);
    const path = svg.match(/<path d="([^"]+)"/)?.[1] ?? '';
    expect(path.match(/M/g)).toHaveLength(2); expect(path).not.toContain('L');
    expect(svg.match(/<circle/g)).toHaveLength(2);
  });
  it('plots finite extreme endpoints without overflowing SVG coordinates', () => {
    const svg = renderToStaticMarkup(<SweepPlot result={result([1, 2, 3], [-1e308, 0, 1e308])} trace={0} unit="" />);
    expect(svg).not.toMatch(/NaN|Infinity/);
    expect(svg.match(/<circle/g)).toHaveLength(3);
  });
});
