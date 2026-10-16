// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { Menu, type MenuItem } from './components/Menu';

describe('Menu', () => {
  const make = () => {
    const ran: string[] = [];
    const items: MenuItem[] = [
      { id: 'new', label: 'New', onSelect: () => ran.push('new') },
      { id: 'off', label: 'Unavailable', disabled: true, onSelect: () => ran.push('off') },
      { id: 'open', label: 'Open', shortcut: 'Ctrl+O', onSelect: () => ran.push('open') },
    ];
    render(<Menu label="Project menu" trigger="Project" items={items} />);
    return ran;
  };

  it('opens on the first item, skips disabled ones and runs the chosen one', () => {
    const ran = make();
    const button = screen.getByRole('button', { name: 'Project menu' });
    expect(button.getAttribute('aria-expanded')).toBe('false');
    fireEvent.keyDown(button, { key: 'ArrowDown' });
    const menu = screen.getByRole('menu', { name: 'Project menu' });
    expect(document.activeElement?.textContent).toContain('New');
    fireEvent.keyDown(menu, { key: 'ArrowDown' });
    expect(document.activeElement?.textContent).toContain('Open');
    fireEvent.click(document.activeElement as HTMLElement);
    expect(ran).toEqual(['open']);
    expect(screen.queryByRole('menu')).toBeNull();
    expect(document.activeElement).toBe(button);
  });

  it('closes on Escape and outside clicks, and a disabled item does nothing', () => {
    const ran = make();
    const button = screen.getByRole('button', { name: 'Project menu' });
    fireEvent.click(button);
    fireEvent.click(screen.getByRole('menuitem', { name: /Unavailable/ }));
    expect(ran).toEqual([]);
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' });
    expect(screen.queryByRole('menu')).toBeNull();
    fireEvent.click(button);
    fireEvent.pointerDown(document.body);
    expect(screen.queryByRole('menu')).toBeNull();
    vi.restoreAllMocks();
  });
});
