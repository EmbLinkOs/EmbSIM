// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import type { CanvasContext } from '@embsim/scene';

export interface ContextAction { label: string; shortcut?: string; disabled?: boolean; danger?: boolean; run(): void }

/** A real keyboard-operable menu, kept inside the viewport and focus-restoring. */
export function SchematicContextMenu({ context, actions, onClose }: { context: CanvasContext; actions: ContextAction[]; onClose(): void }) {
  const ref = useRef<HTMLDivElement>(null);
  const [position, setPosition] = useState({ x: context.x, y: context.y });
  useLayoutEffect(() => {
    const rect = ref.current!.getBoundingClientRect();
    setPosition({ x: Math.max(8, Math.min(context.x, innerWidth - rect.width - 8)), y: Math.max(8, Math.min(context.y, innerHeight - rect.height - 8)) });
  }, [context]);
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | SVGElement | null;
    ref.current?.querySelector<HTMLButtonElement>('button:not(:disabled)')?.focus();
    const closeOutside = (e: PointerEvent) => { if (!ref.current?.contains(e.target as Node)) onClose(); };
    const close = () => onClose();
    window.addEventListener('pointerdown', closeOutside, true);
    window.addEventListener('resize', close);
    window.addEventListener('wheel', close, true);
    return () => {
      window.removeEventListener('pointerdown', closeOutside, true);
      window.removeEventListener('resize', close);
      window.removeEventListener('wheel', close, true);
      if (previous?.isConnected) previous.focus({ preventScroll: true });
    };
  }, [onClose]);
  return <div ref={ref} className="wb-schematic-context" role="menu" aria-label="Schematic actions" style={{ left: position.x, top: position.y }} onKeyDown={(e) => {
    if (e.key === 'Escape' || e.key === 'Tab') { e.preventDefault(); onClose(); return; }
    const buttons = [...ref.current!.querySelectorAll<HTMLButtonElement>('button:not(:disabled)')];
    const current = buttons.indexOf(document.activeElement as HTMLButtonElement);
    const next = e.key === 'ArrowDown' ? (current + 1) % buttons.length : e.key === 'ArrowUp' ? (current - 1 + buttons.length) % buttons.length : e.key === 'Home' ? 0 : e.key === 'End' ? buttons.length - 1 : -1;
    if (next >= 0) { e.preventDefault(); buttons[next]?.focus(); }
  }}>
    <div className="wb-schematic-context__heading">{context.selection.components.length ? `${context.selection.components.length} selected` : context.net ? `Net ${context.net}` : 'Sheet actions'}</div>
    {actions.map((action) => <button key={action.label} type="button" role="menuitem" aria-label={action.label} disabled={action.disabled} className={action.danger ? 'is-danger' : undefined} onClick={() => { onClose(); action.run(); }}><span>{action.label}</span>{action.shortcut && <kbd>{action.shortcut}</kbd>}</button>)}
  </div>;
}
