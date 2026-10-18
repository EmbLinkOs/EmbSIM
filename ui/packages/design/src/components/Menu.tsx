// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Menu: a button that opens a list of actions below it (WAI-ARIA menu
// button). Enter, Space or ArrowDown open it on the first item, ArrowUp on
// the last; the arrows, Home and End move between enabled items; Enter or a
// click runs one and closes; Escape, Tab or a click outside closes and gives
// the focus back to the button.

import { useEffect, useId, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { cx } from '../cx';

export interface MenuItem {
  id: string;
  label: ReactNode;
  icon?: ReactNode;
  shortcut?: string;
  disabled?: boolean;
  /** draws a line above the item */
  separator?: boolean;
  onSelect(): void;
}

export interface MenuProps {
  /** the button's content */
  trigger: ReactNode;
  /** the button's accessible name */
  label: string;
  items: MenuItem[];
  className?: string;
  /** the list's alignment under the button */
  align?: 'start' | 'end';
}

export function Menu({ trigger, label, items, className, align = 'start' }: MenuProps) {
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(-1);
  const buttonRef = useRef<HTMLButtonElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const itemRefs = useRef<(HTMLButtonElement | null)[]>([]);
  const id = useId();
  const enabled = items.map((it, i) => (it.disabled ? -1 : i)).filter((i) => i >= 0);

  const close = (focusButton = true) => {
    setOpen(false);
    setActive(-1);
    if (focusButton) buttonRef.current?.focus();
  };
  const openAt = (which: 'first' | 'last') => {
    setOpen(true);
    setActive(which === 'first' ? (enabled[0] ?? -1) : (enabled[enabled.length - 1] ?? -1));
  };

  useEffect(() => {
    if (open && active >= 0) itemRefs.current[active]?.focus();
  }, [open, active]);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: PointerEvent) => {
      const t = e.target as Node;
      if (!listRef.current?.contains(t) && !buttonRef.current?.contains(t)) close(false);
    };
    document.addEventListener('pointerdown', onDown);
    return () => document.removeEventListener('pointerdown', onDown);
  }, [open]);

  const move = (delta: number) => {
    if (!enabled.length) return;
    const at = enabled.indexOf(active);
    const next = enabled[(at + delta + enabled.length) % enabled.length]!;
    setActive(next);
  };

  const onListKey = (e: KeyboardEvent<HTMLDivElement>) => {
    if (e.key === 'ArrowDown') move(1);
    else if (e.key === 'ArrowUp') move(-1);
    else if (e.key === 'Home') setActive(enabled[0] ?? -1);
    else if (e.key === 'End') setActive(enabled[enabled.length - 1] ?? -1);
    else if (e.key === 'Escape') close();
    else if (e.key === 'Tab') close(false);
    else return;
    if (e.key !== 'Tab') e.preventDefault();
  };

  const select = (it: MenuItem) => {
    if (it.disabled) return;
    close();
    it.onSelect();
  };

  return (
    <div className={cx('es-menu', className)}>
      <button
        ref={buttonRef}
        type="button"
        className="es-menu__button"
        aria-label={label}
        aria-haspopup="menu"
        aria-expanded={open}
        aria-controls={open ? id : undefined}
        onClick={() => (open ? close() : openAt('first'))}
        onKeyDown={(e) => {
          if (e.key === 'ArrowDown') {
            e.preventDefault();
            openAt('first');
          } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            openAt('last');
          }
        }}
      >
        {trigger}
      </button>
      {open && (
        <div ref={listRef} id={id} role="menu" aria-label={label} className={cx('es-menu__list', align === 'end' && 'es-menu__list--end')} onKeyDown={onListKey}>
          {items.map((it, i) => (
            <div key={it.id} role="none">
              {it.separator && <div role="separator" className="es-menu__separator" />}
              <button
                ref={(el) => {
                  itemRefs.current[i] = el;
                }}
                type="button"
                role="menuitem"
                tabIndex={i === active ? 0 : -1}
                aria-disabled={it.disabled || undefined}
                className="es-menu__item"
                onClick={() => select(it)}
                onMouseEnter={() => !it.disabled && setActive(i)}
              >
                <span className="es-menu__icon" aria-hidden="true">
                  {it.icon}
                </span>
                <span className="es-menu__label">{it.label}</span>
                {it.shortcut && <kbd className="es-menu__shortcut">{it.shortcut}</kbd>}
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
