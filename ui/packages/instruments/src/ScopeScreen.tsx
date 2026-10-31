// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// An oscilloscope's screen, as the DS1054Z draws it: 12 divisions across and
// 8 up on a dark graticule, each channel's trace in its colour about its
// offset at its volts a division, each channel's ground marker on the left,
// the trigger's level on the right and its position at the top, the status
// (WAIT, T'D, AUTO, STOP) in the corner.

import type { ScopeCapture } from '@embsim/protocol';
import { useEffect, useRef } from 'react';

/** the DS1054Z's channel colours: yellow, cyan, magenta, blue */
export const SCOPE_COLORS = ['#f5e02c', '#2bd4e8', '#e34fd8', '#3b7bff'];

export interface ScopeChannelView {
  on: boolean;
  scale: number;
  offset: number;
}

export interface ScopeScreenProps {
  capture: ScopeCapture | null;
  channels: ScopeChannelView[];
  trigger: { source: number; level: number };
  label?: string;
}

const DIV_X = 12;
const DIV_Y = 8;

export function ScopeScreen({ capture, channels, trigger, label = 'Oscilloscope screen' }: ScopeScreenProps) {
  const ref = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const canvas = ref.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth || 600;
    const h = canvas.clientHeight || 400;
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
    const g = canvas.getContext('2d');
    if (!g) return;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = '#05080c';
    g.fillRect(0, 0, w, h);
    const left = 14;
    const right = 14;
    const top = 14;
    const bottom = 4;
    const pw = w - left - right;
    const ph = h - top - bottom;
    const dx = pw / DIV_X;
    const dy = ph / DIV_Y;
    // the graticule: divisions, and fine ticks on the centre lines
    g.strokeStyle = 'rgba(160,170,185,0.22)';
    g.lineWidth = 1;
    g.setLineDash([1, 3]);
    for (let i = 0; i <= DIV_X; i++) {
      g.beginPath();
      g.moveTo(left + i * dx + 0.5, top);
      g.lineTo(left + i * dx + 0.5, top + ph);
      g.stroke();
    }
    for (let j = 0; j <= DIV_Y; j++) {
      g.beginPath();
      g.moveTo(left, top + j * dy + 0.5);
      g.lineTo(left + pw, top + j * dy + 0.5);
      g.stroke();
    }
    g.setLineDash([]);
    g.strokeStyle = 'rgba(160,170,185,0.4)';
    for (let i = 0; i <= DIV_X * 5; i++) {
      const x = left + (i * dx) / 5 + 0.5;
      g.beginPath();
      g.moveTo(x, top + ph / 2 - 3);
      g.lineTo(x, top + ph / 2 + 3);
      g.stroke();
    }
    for (let j = 0; j <= DIV_Y * 5; j++) {
      const y = top + (j * dy) / 5 + 0.5;
      g.beginPath();
      g.moveTo(left + pw / 2 - 3, y);
      g.lineTo(left + pw / 2 + 3, y);
      g.stroke();
    }
    const yOf = (ch: ScopeChannelView, v: number) => top + ph / 2 - ((v + ch.offset) / ch.scale) * dy;
    // the traces
    capture?.channels.forEach((c, k) => {
      const view = channels[k];
      if (!c.on || !c.points || !view) return;
      g.strokeStyle = SCOPE_COLORS[k]!;
      g.lineWidth = 1.4;
      g.beginPath();
      const n = c.points.length;
      c.points.forEach((v, i) => {
        const x = left + (i / Math.max(1, n - 1)) * pw;
        const y = Math.min(top + ph, Math.max(top, yOf(view, v)));
        if (i === 0) g.moveTo(x, y);
        else g.lineTo(x, y);
      });
      g.stroke();
    });
    // each channel's ground marker on the left
    channels.forEach((ch, k) => {
      if (!ch.on) return;
      const y = Math.min(top + ph, Math.max(top, yOf(ch, 0)));
      g.fillStyle = SCOPE_COLORS[k]!;
      g.beginPath();
      g.moveTo(2, y - 5);
      g.lineTo(left - 2, y);
      g.lineTo(2, y + 5);
      g.fill();
      g.fillStyle = '#05080c';
      g.font = 'bold 8px sans-serif';
      g.fillText(String(k + 1), 3, y + 3);
    });
    // the trigger's level on the right, and its position at the top
    const src = channels[trigger.source - 1];
    if (src) {
      const y = Math.min(top + ph, Math.max(top, yOf(src, trigger.level)));
      g.fillStyle = '#ff8c1a';
      g.beginPath();
      g.moveTo(w - 2, y - 5);
      g.lineTo(w - right + 2, y);
      g.lineTo(w - 2, y + 5);
      g.fill();
    }
    if (capture?.trigger_ps && capture.x0_ps && capture.dx_ps && capture.channels.some((c) => c.points)) {
      const n = capture.channels.find((c) => c.points)?.points?.length ?? 1200;
      const tx = (Number(BigInt(capture.trigger_ps) - BigInt(capture.x0_ps)) / Number(capture.dx_ps)) / Math.max(1, n - 1);
      if (tx >= 0 && tx <= 1) {
        const x = left + tx * pw;
        g.fillStyle = '#ff8c1a';
        g.beginPath();
        g.moveTo(x - 5, 2);
        g.lineTo(x + 5, 2);
        g.lineTo(x, top - 2);
        g.fill();
      }
    }
  }, [capture, channels, trigger]);
  const status = capture?.status ?? 'STOP';
  return (
    <div className="es-scope-screen" role="img" aria-label={`${label}: ${status}`}>
      <canvas ref={ref} />
      <span className={`es-scope-screen__status es-scope-screen__status--${status === "T'D" ? 'td' : status.toLowerCase()}`}>{status}</span>
    </div>
  );
}
