// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { appendSerialText, type SerialLine } from './SerialTerminal';

const texts = (lines: SerialLine[]) => lines.map((l) => [l.target, l.text, l.open ?? false]);

describe('appendSerialText', () => {
  it('joins pieces into lines, stamped with the first piece', () => {
    let lines: SerialLine[] = [];
    lines = appendSerialText(lines, 'VT1', 1n, 'wire: random read at 400 k');
    lines = appendSerialText(lines, 'VT1', 2n, 'Hz 0 5 Wire!\r\nwire: 0x51');
    lines = appendSerialText(lines, 'VT1', 3n, ' 2\r\n');
    expect(texts(lines)).toEqual([
      ['VT1', 'wire: random read at 400 kHz 0 5 Wire!', false],
      ['VT1', 'wire: 0x51 2', false],
    ]);
    expect(lines.map((l) => l.at)).toEqual([1n, 2n]);
  });

  it('keeps each port its own line', () => {
    let lines: SerialLine[] = [];
    lines = appendSerialText(lines, 'B1', 1n, 'Emb');
    lines = appendSerialText(lines, 'VT1', 1n, 'Emb');
    lines = appendSerialText(lines, 'B1', 2n, 'SIM\n');
    lines = appendSerialText(lines, 'VT1', 2n, 'SIM\n');
    expect(texts(lines)).toEqual([
      ['B1', 'EmbSIM', false],
      ['VT1', 'EmbSIM', false],
    ]);
  });

  it('starts a new line after a sent one and keeps a limit', () => {
    let lines: SerialLine[] = [{ at: 0n, direction: 'tx', target: 'VT1', text: 'x' }];
    lines = appendSerialText(lines, 'VT1', 1n, 'a\nb\nc\n', 2);
    expect(texts(lines)).toEqual([
      ['VT1', 'b', false],
      ['VT1', 'c', false],
    ]);
  });
});
