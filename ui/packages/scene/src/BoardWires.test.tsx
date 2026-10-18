// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { render } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { BoardView, routeWire, type BoardItem } from './BoardView';

const box = (id: string, x: number, y: number): BoardItem => ({
  id,
  label: id,
  svg: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 40 20"><rect width="40" height="20"/></svg>',
  x,
  y,
  width: 40,
  height: 20,
});

describe('bench wires', () => {
  it('leaves each pin by a stub, then runs square to the other', () => {
    const d = routeWire({ x: 10, y: 20, side: 'down' }, { x: 50, y: 100, side: 'up' }, 0);
    expect(d).toBe('M 10.0 20.0 L 10.0 30.0 L 50.0 30.0 L 50.0 90.0 L 50.0 100.0');
    // a later lane steps its stubs out so parallel wires do not overlap
    expect(routeWire({ x: 0, y: 0, side: 'right' }, { x: 40, y: 10, side: 'left' }, 2)).toBe('M 0.0 0.0 L 16.0 0.0 L 16.0 10.0 L 24.0 10.0 L 40.0 10.0');
  });

  it('goes round a part a straight run would cross', () => {
    // from under a board (x 0..100, y 50..150) up to a part above it at x 40
    const board = { x: 0, y: 50, width: 100, height: 100 };
    const d = routeWire({ x: 60, y: 150, side: 'down' }, { x: 40, y: 20, side: 'down' }, 0, [board]);
    expect(d).toBe('M 60.0 150.0 L 60.0 160.0 L -8.0 160.0 L -8.0 30.0 L 40.0 30.0 L 40.0 20.0');
  });

  it('draws a net between two parts, at their pins or along their bottom edges', () => {
    const { container } = render(
      <BoardView
        items={[box('A', 0, 0), box('B', 100, 60)]}
        wires={[{ net: 'SIG', color: '#27ae60', ends: [{ item: 'A', pin: '1', order: 0, count: 1 }, { item: 'B', pin: '2', order: 0, count: 1 }] }]}
      />,
    );
    const net = container.querySelector('[data-net="SIG"]');
    expect(net).not.toBeNull();
    const paths = net!.querySelectorAll('path');
    expect(paths).toHaveLength(2); // the outline and the wire
    expect(paths[1]!.getAttribute('stroke')).toBe('#27ae60');
    // no pin element in these boxes: the ends sit on the bottom edges' middles
    expect(paths[1]!.getAttribute('d')!.startsWith('M 20.0 20.0')).toBe(true);
    expect(net!.querySelectorAll('circle')).toHaveLength(2);
  });

  it('draws nothing for a net with one end on the bench', () => {
    const { container } = render(<BoardView items={[box('A', 0, 0)]} wires={[{ net: 'X', color: '#000', ends: [{ item: 'A', pin: '1' }, { item: 'Z', pin: '1' }] }]} />);
    expect(container.querySelector('[data-net="X"]')).toBeNull();
  });
});
