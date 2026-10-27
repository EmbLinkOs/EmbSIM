// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { renderToStaticMarkup } from 'react-dom/server';
import { describe, expect, it } from 'vitest';
import type { TransientCapture } from '@embsim/protocol';
import { TransientPlot } from './TransientCapture';
const data = {samples: [{at_ps: '0'}, {at_ps: '1000'}, {at_ps: '2000'}]} as TransientCapture;
const colors = [{id:'OUT',color:'green'}];
describe('native transient plots', () => {
  it('keeps gaps instead of drawing through unknown samples', () => {
    const svg=renderToStaticMarkup(<TransientPlot data={data} traces={[{id:'OUT',label:'OUT',values:[0,null,2]}]} colors={colors} left={0} right={2} cursorA={0} cursorB={2} />);
    const d=svg.match(/<path[^>]* d="([^"]+)"/)?.[1] ?? '';
    expect(d.match(/M/g)).toHaveLength(2);expect(d).not.toContain('L');expect(svg.match(/<circle/g)).toHaveLength(2);
  });
  it('supports a single sample view and finite extreme voltages without invalid geometry', () => {
    const svg=renderToStaticMarkup(<TransientPlot data={data} traces={[{id:'OUT',label:'OUT',values:[-1e308,0,1e308]}]} colors={colors} left={1} right={1} cursorA={0} cursorB={1} />);
    expect(svg).not.toMatch(/NaN|Infinity/);expect(svg).toContain('M400,');expect(svg).toContain('<circle');
    expect(svg).not.toContain('>A</text>');expect(svg).toContain('>B</text>');
  });
});
