// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixDialog from '@radix-ui/react-dialog';
import { Search } from 'lucide-react';
import { useEffect, useId, useMemo, useRef, useState, type ReactNode } from 'react';
import { uiText } from '../uiText';

export interface Command {
  id: string;
  label: string;
  group?: string;
  icon?: ReactNode;
  shortcut?: string;
  keywords?: string[];
  disabled?: boolean;
  run: () => void;
}

export interface CommandPaletteProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  commands: Command[];
  placeholder?: string;
}

/** Fuzzy-ish subsequence match; returns a score (lower is better) or -1. */
export function matchCommand(query: string, command: Command): number {
  const q = query.trim().toLowerCase();
  if (!q) return 0;
  const hay = [command.label, command.group ?? '', ...(command.keywords ?? [])].join(' ').toLowerCase();
  const idx = hay.indexOf(q);
  if (idx >= 0) return idx;
  let hi = 0;
  let score = 0;
  for (const ch of q) {
    const next = hay.indexOf(ch, hi);
    if (next < 0) return -1;
    score += next - hi;
    hi = next + 1;
  }
  return 1000 + score;
}

/** A command palette: Ctrl/Cmd-K opens it from the app; listbox semantics with arrow keys and Enter. */
export function CommandPalette({ open, onOpenChange, commands, placeholder = 'Type a command…' }: CommandPaletteProps) {
  const [query, setQuery] = useState('');
  const [active, setActive] = useState(0);
  const listId = useId();
  const inputRef = useRef<HTMLInputElement>(null);

  const visible = useMemo(() => {
    return commands
      .filter((c) => !c.disabled)
      .map((c) => ({ c, score: matchCommand(query, c) }))
      .filter((x) => x.score >= 0)
      .sort((a, b) => a.score - b.score)
      .map((x) => x.c);
  }, [commands, query]);

  useEffect(() => {
    if (open) {
      setQuery('');
      setActive(0);
    }
  }, [open]);

  useEffect(() => {
    setActive((a) => Math.min(a, Math.max(0, visible.length - 1)));
  }, [visible.length]);

  useEffect(() => {
    const el = document.getElementById(`${listId}-${active}`);
    el?.scrollIntoView({ block: 'nearest' });
  }, [active, listId]);

  const run = (c: Command) => {
    onOpenChange(false);
    c.run();
  };

  let lastGroup: string | undefined;
  return (
    <RadixDialog.Root open={open} onOpenChange={onOpenChange}>
      <RadixDialog.Portal>
        <RadixDialog.Overlay className="es-dialog__overlay" />
        <RadixDialog.Content className="es-dialog__content es-palette" aria-describedby={undefined} onOpenAutoFocus={(e) => {
          e.preventDefault();
          inputRef.current?.focus();
        }}>
          <RadixDialog.Title className="es-sr-only">{uiText('palette.title', 'Command palette')}</RadixDialog.Title>
          <div className="es-palette__input">
            <Search aria-hidden="true" />
            <input
              ref={inputRef}
              role="combobox"
              aria-expanded="true"
              aria-controls={listId}
              aria-activedescendant={visible.length ? `${listId}-${active}` : undefined}
              aria-autocomplete="list"
              aria-label={uiText('palette.search', 'Search commands')}
              placeholder={placeholder}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'ArrowDown') {
                  e.preventDefault();
                  setActive((a) => (a + 1) % Math.max(1, visible.length));
                } else if (e.key === 'ArrowUp') {
                  e.preventDefault();
                  setActive((a) => (a - 1 + visible.length) % Math.max(1, visible.length));
                } else if (e.key === 'Enter') {
                  e.preventDefault();
                  const c = visible[active];
                  if (c) run(c);
                } else if (e.key === 'Home') {
                  setActive(0);
                } else if (e.key === 'End') {
                  setActive(Math.max(0, visible.length - 1));
                }
              }}
            />
          </div>
          <ul id={listId} role="listbox" className="es-palette__list" aria-label={uiText('palette.commands', 'Commands')}>
            {visible.length === 0 && <li className="es-palette__empty">{uiText('palette.none', 'No matching command')}</li>}
            {visible.map((c, i) => {
              const header = !query && c.group !== lastGroup ? c.group : undefined;
              lastGroup = c.group;
              return (
                <li key={c.id} role="presentation">
                  {header && (
                    <div className="es-palette__group" aria-hidden="true">
                      {header}
                    </div>
                  )}
                  <div
                    id={`${listId}-${i}`}
                    role="option"
                    aria-selected={i === active}
                    className="es-palette__item"
                    onMouseEnter={() => setActive(i)}
                    onClick={() => run(c)}
                  >
                    {c.icon}
                    <span className="es-palette__item-label">{c.label}</span>
                    {c.group && query && <span className="es-palette__item-hint">{c.group}</span>}
                    {c.shortcut && <kbd className="es-kbd">{c.shortcut}</kbd>}
                  </div>
                </li>
              );
            })}
          </ul>
        </RadixDialog.Content>
      </RadixDialog.Portal>
    </RadixDialog.Root>
  );
}
