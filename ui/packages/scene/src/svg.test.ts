// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { scopeArtwork, scopeCss } from './svg';

describe('artwork scoping', () => {
  it('prefixes every selector of a stylesheet', () => {
    expect(scopeCss('.pcb{fill:#1f4fb3}.a, .b > c{x:1}', '[data-board-item="B1"]')).toBe(
      '[data-board-item="B1"] .pcb{fill:#1f4fb3}[data-board-item="B1"] .a, [data-board-item="B1"] .b > c{x:1}',
    );
  });

  it('keeps two boards from restyling each other', () => {
    const inner = '<style>.pcb{fill:teal}</style><rect class="pcb"/>';
    expect(scopeArtwork(inner, 'B1')).toContain('[data-board-item="B1"] .pcb{fill:teal}');
    expect(scopeArtwork(inner, 'B2')).toContain('[data-board-item="B2"] .pcb{fill:teal}');
  });

  it('renames referenced ids and their references, and leaves hotspot ids', () => {
    const inner =
      '<defs><radialGradient id="glow-green"/></defs><circle id="led-user" fill="url(#glow-green)"/><use href="#glow-green"/>';
    const out = scopeArtwork(inner, 'B2');
    expect(out).toContain('id="B2--glow-green"');
    expect(out).toContain('fill="url(#B2--glow-green)"');
    expect(out).toContain('href="#B2--glow-green"');
    expect(out).toContain('id="led-user"');
  });
});
