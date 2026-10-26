// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { render, screen } from '@testing-library/react';
import { beforeEach, expect, it, vi } from 'vitest';
import { WaveformRecord } from './waveform';
import { Oscilloscope } from './Oscilloscope';
import { LogicAnalyzer } from './LogicAnalyzer';

const { ctx } = vi.hoisted(() => ({ ctx: new Proxy({} as Record<string, unknown>, { get(target, key: string) { return target[key] ??= vi.fn(); } }) }));
vi.mock('./useCanvas', () => ({
  cssVar: (_el: unknown, _name: string, fallback: string) => fallback,
  useCanvas: (draw: (ctx: unknown, size: unknown) => void) => {
    const size = { width: 800, height: 300, dpr: 1 };
    draw(ctx, size);
    return { ref: { current: null }, size, redraw: () => undefined };
  },
}));
beforeEach(() => vi.clearAllMocks());
const props = { window: { start: 0n, span: 1000n, follow: false }, onWindowChange: () => undefined, cursors: { a: 700n, b: null }, onCursorsChange: () => undefined, now: 1000n };

it('clips analog drawing and cursor readouts to producer coverage when the clock is ahead', () => {
  const record = new WaveformRecord('voltage', 'analog', 'voltage', 'V');
  record.appendFrame({ stream: 1, seq: 0, probe: 'voltage', encoding: 'json', samples: [['0', 3.3]], through_time_ps: '400' });
  render(<Oscilloscope {...props} traces={[{ record, color: '#ff0000' }]} />);
  // Plot width 740: only 40% has arrived even though the engine clock is 1000.
  expect(ctx.rect).toHaveBeenCalledWith(52, 8, 296, 270);
  expect(screen.getByText('40.0 %')).toBeTruthy();
  expect(screen.getByText(/700.*—/)).toBeTruthy();
});

it('clips a sparse digital trace to its watermark and extends it after an empty frontier packet', () => {
  const record = new WaveformRecord('logic', 'digital');
  record.appendFrame({ stream: 1, seq: 0, probe: 'logic', encoding: 'json', samples: [['0', 1]], through_time_ps: '400' });
  const { rerender } = render(<LogicAnalyzer {...props} channels={[{ record }]} />);
  expect(ctx.rect).toHaveBeenCalledWith(72, 6, 288, 28);
  record.appendFrame({ stream: 1, seq: 0, probe: 'logic', encoding: 'json', samples: [], through_time_ps: '800' });
  rerender(<LogicAnalyzer {...props} channels={[{ record }]} revision={record.revision} />);
  expect(ctx.rect).toHaveBeenCalledWith(72, 6, 576, 28);
});
