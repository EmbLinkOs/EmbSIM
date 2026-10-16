// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { DockLayout, type DockItem } from './components/DockLayout';
const items: DockItem[] = ['Library', 'Canvas', 'Inspector'].map((id) => ({ id, title: id, content: <span>{id} content</span> }));
describe('dock preference namespaces', () => {
  it('restores the original order after entering and leaving canvas focus mode', () => {
    localStorage.clear();
    const view = render(<DockLayout direction="row" items={items} storageKey="panels" />);
    view.rerender(<DockLayout direction="row" items={[items[1]!]} storageKey="focus" />);
    view.rerender(<DockLayout direction="row" items={items} storageKey="panels" />);
    expect(screen.getAllByRole('region').map((el) => el.getAttribute('aria-label'))).toEqual(['Library', 'Canvas', 'Inspector']);
    expect(JSON.parse(localStorage.getItem('panels')!).order).toEqual(['Library', 'Canvas', 'Inspector']);
  });
  it('loads each key separately and rejects unusable saved weights', () => {
    localStorage.setItem('custom', JSON.stringify({ order: ['Inspector', 'Canvas', 'Library'], weights: { Inspector: -2, Canvas: 'bad', Library: 2 } }));
    const view = render(<DockLayout direction="row" items={items} storageKey="custom" />);
    expect(screen.getAllByRole('region').map((el) => el.getAttribute('aria-label'))).toEqual(['Inspector', 'Canvas', 'Library']);
    expect(JSON.parse(localStorage.getItem('custom')!).weights).toEqual({ Library: 2, Canvas: 1, Inspector: 1 });
    view.rerender(<DockLayout direction="row" items={items} storageKey="new" />);
    expect(screen.getAllByRole('region').map((el) => el.getAttribute('aria-label'))).toEqual(['Library', 'Canvas', 'Inspector']);
  });
});
