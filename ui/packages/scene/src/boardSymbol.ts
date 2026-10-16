// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A schematic symbol for a board, made from its ports (SPEC-006 §3): a
// block whose pins follow the board's connectors, so the schematic reads
// like the board. The first connector goes down the left edge, the second
// down the right, the others along the bottom; each in its pin order.

import type { SymbolDescription } from '@embsim/protocol';
import { GRID } from './geometry';

export interface BoardPort {
  connector?: string;
  pin?: number;
  doc?: string;
}

const PITCH = 2 * GRID;
const HEADER = 3 * GRID; // room for the board's name

function escapeXml(s: string): string {
  return s.replace(/[<>&"']/g, (c) => ({ '<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;', "'": '&apos;' })[c]!);
}

/** The ports grouped by connector, each group in pin order. */
export function portGroups(ports: Record<string, BoardPort>): string[][] {
  const groups = new Map<string, { name: string; pin: number; order: number }[]>();
  Object.entries(ports).forEach(([name, p], order) => {
    const key = p.connector ?? '';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key)!.push({ name, pin: p.pin ?? order, order });
  });
  return [...groups.values()].map((g) => g.sort((a, b) => a.pin - b.pin || a.order - b.order).map((x) => x.name));
}

export function boardSymbol(title: string, ports: Record<string, BoardPort>): { symbol: SymbolDescription; svg: string } {
  const groups = portGroups(ports);
  const left = groups[0] ?? [];
  const right = groups[1] ?? [];
  const bottom = groups.slice(2).flat();
  const rows = Math.max(left.length, right.length, 1);
  const width = Math.max(16 * GRID, (bottom.length + 1) * PITCH);
  const height = HEADER + rows * PITCH + (bottom.length ? 3 * GRID : GRID);
  const pins: SymbolDescription['pins'] = {};
  const parts: string[] = [];
  const stub = GRID / 2;

  left.forEach((name, i) => {
    const y = HEADER + i * PITCH + GRID;
    pins[name] = { x: 0, y, side: 'left' };
    parts.push(`<line class="s" x1="0" y1="${y}" x2="${stub}" y2="${y}"/>`);
    parts.push(`<text class="t" x="${stub + 3}" y="${y + 2.5}">${escapeXml(name)}</text>`);
  });
  right.forEach((name, i) => {
    const y = HEADER + i * PITCH + GRID;
    pins[name] = { x: width, y, side: 'right' };
    parts.push(`<line class="s" x1="${width}" y1="${y}" x2="${width - stub}" y2="${y}"/>`);
    parts.push(`<text class="t" x="${width - stub - 3}" y="${y + 2.5}" text-anchor="end">${escapeXml(name)}</text>`);
  });
  bottom.forEach((name, i) => {
    const x = (i + 1) * PITCH;
    pins[name] = { x, y: height, side: 'bottom' };
    parts.push(`<line class="s" x1="${x}" y1="${height}" x2="${x}" y2="${height - stub}"/>`);
    parts.push(`<text class="t" x="${x + 2.5}" y="${height - stub - 3}" transform="rotate(-90 ${x + 2.5} ${height - stub - 3})">${escapeXml(name)}</text>`);
  });

  const svg =
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" width="${width}" height="${height}">` +
    `<style>.s{stroke:var(--symbol-stroke,#cdd6e4);fill:none;stroke-width:1.5;stroke-linecap:round}` +
    `.f{stroke:var(--symbol-stroke,#cdd6e4);fill:var(--symbol-fill,#1b2230);stroke-width:1.5}` +
    `.t{fill:var(--symbol-stroke,#cdd6e4);font-family:sans-serif;font-size:7px}` +
    `.h{fill:var(--symbol-stroke,#cdd6e4);font-family:sans-serif;font-size:9px;font-weight:600}</style>` +
    `<rect class="f" x="${stub}" y="0" width="${width - 2 * stub}" height="${height - (bottom.length ? stub : 0)}" rx="4"/>` +
    `<text class="h" x="${width / 2}" y="${GRID * 1.8}" text-anchor="middle">${escapeXml(title)}</text>` +
    parts.join('') +
    `</svg>`;
  return { symbol: { svg: 'generated', width, height, pins }, svg };
}
