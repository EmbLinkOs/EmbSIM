// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// DockLayout (M0): a resizable split of panels in one direction, with the
// panel order draggable by the handle in each header and persisted in
// localStorage under `storageKey`. Nested splits are plain composition (an
// item's content may be another DockLayout). Full docking (float, tab
// groups) is later milestone work.

import { GripVertical } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent, type ReactNode } from 'react';
import { cx } from '../cx';
import { Panel } from './Panel';

export interface DockItem {
  id: string;
  title: string;
  icon?: ReactNode;
  content: ReactNode;
  actions?: ReactNode;
  /** Minimum size in px along the split axis. Default 120. */
  minSize?: number;
  /** Initial relative weight. Default 1. */
  weight?: number;
  /** Render the content without the Panel chrome (for nested layouts). */
  bare?: boolean;
  flush?: boolean;
  padded?: boolean;
}

export interface DockLayoutProps {
  direction: 'row' | 'column';
  items: DockItem[];
  /** localStorage key for order and sizes; omit to keep state in memory only. */
  storageKey?: string;
  className?: string;
}

interface Persisted {
  order: string[];
  weights: Record<string, number>;
}

function load(key: string | undefined): Persisted | null {
  if (!key) return null;
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Persisted;
    if (!Array.isArray(parsed.order) || !parsed.order.every((id) => typeof id === 'string') || !parsed.weights || typeof parsed.weights !== 'object' || Array.isArray(parsed.weights)) return null;
    return parsed;
  } catch {
    return null;
  }
}

function save(key: string | undefined, state: Persisted): void {
  if (!key) return;
  try {
    localStorage.setItem(key, JSON.stringify({ order: state.order, weights: state.weights }));
  } catch {
    // Storage may be unavailable (private mode); layout then lives for the session only.
  }
}

function initialState(items: DockItem[], key: string | undefined): Persisted & { key?: string } {
  const stored = load(key);
  const ids = items.map((item) => item.id);
  const order = stored ? [...stored.order.filter((id) => ids.includes(id)), ...ids.filter((id) => !stored.order.includes(id))] : ids;
  const weights: Record<string, number> = {};
  for (const item of items) {
    const weight = stored?.weights?.[item.id];
    weights[item.id] = typeof weight === 'number' && Number.isFinite(weight) && weight > 0 ? weight : item.weight ?? 1;
  }
  return { key, order: [...new Set(order)], weights };
}

export function DockLayout({ direction, items, storageKey, className }: DockLayoutProps) {
  const ids = useMemo(() => items.map((i) => i.id), [items]);
  const [state, setState] = useState(() => initialState(items, storageKey));
  // Switch preference namespaces before effects can save the previous layout
  // under the new key. Stable item keys preserve the mounted canvas/viewport.
  if (state.key !== storageKey) setState(initialState(items, storageKey));

  // Keep the order in sync when the item set changes (a perspective adds a panel).
  useEffect(() => {
    setState((s) => {
      const order = [...s.order.filter((id) => ids.includes(id)), ...ids.filter((id) => !s.order.includes(id))];
      const weights = { ...s.weights };
      for (const item of items) weights[item.id] ??= item.weight ?? 1;
      if (order.length === s.order.length && order.every((id, i) => id === s.order[i]) && Object.keys(weights).length === Object.keys(s.weights).length) return s;
      return { ...s, order, weights };
    });
  }, [ids, items]);

  useEffect(() => save(storageKey, state), [storageKey, state]);

  const containerRef = useRef<HTMLDivElement>(null);
  const [dragging, setDragging] = useState<number | null>(null);
  const [dropTarget, setDropTarget] = useState<string | null>(null);
  const byId = useMemo(() => new Map(items.map((i) => [i.id, i])), [items]);
  const ordered = state.order.map((id) => byId.get(id)).filter((x): x is DockItem => Boolean(x));

  const resize = useCallback(
    (index: number, deltaPx: number) => {
      const el = containerRef.current;
      if (!el) return;
      const total = direction === 'row' ? el.clientWidth : el.clientHeight;
      if (total <= 0) return;
      setState((s) => {
        const a = ordered[index];
        const b = ordered[index + 1];
        if (!a || !b) return s;
        const sum = ordered.reduce((acc, it) => acc + (s.weights[it.id] ?? 1), 0);
        const pxPerWeight = total / sum;
        const wa = (s.weights[a.id] ?? 1) * pxPerWeight;
        const wb = (s.weights[b.id] ?? 1) * pxPerWeight;
        const minA = a.minSize ?? 120;
        const minB = b.minSize ?? 120;
        let d = deltaPx;
        if (wa + d < minA) d = minA - wa;
        if (wb - d < minB) d = wb - minB;
        return { ...s, weights: { ...s.weights, [a.id]: (wa + d) / pxPerWeight, [b.id]: (wb - d) / pxPerWeight } };
      });
    },
    [direction, ordered],
  );

  const onGutterPointerDown = (index: number) => (e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault();
    const start = direction === 'row' ? e.clientX : e.clientY;
    let last = start;
    setDragging(index);
    const target = e.currentTarget;
    target.setPointerCapture(e.pointerId);
    const move = (ev: PointerEvent) => {
      const pos = direction === 'row' ? ev.clientX : ev.clientY;
      resize(index, pos - last);
      last = pos;
    };
    const up = () => {
      setDragging(null);
      target.removeEventListener('pointermove', move);
      target.removeEventListener('pointerup', up);
      target.removeEventListener('pointercancel', up);
    };
    target.addEventListener('pointermove', move);
    target.addEventListener('pointerup', up);
    target.addEventListener('pointercancel', up);
  };

  const onGutterKeyDown = (index: number) => (e: KeyboardEvent<HTMLDivElement>) => {
    const grow = direction === 'row' ? 'ArrowRight' : 'ArrowDown';
    const shrink = direction === 'row' ? 'ArrowLeft' : 'ArrowUp';
    if (e.key === grow) {
      e.preventDefault();
      resize(index, 16);
    } else if (e.key === shrink) {
      e.preventDefault();
      resize(index, -16);
    }
  };

  const move = (id: string, delta: number) => {
    setState((s) => {
      const i = s.order.indexOf(id);
      const j = i + delta;
      if (i < 0 || j < 0 || j >= s.order.length) return s;
      const order = [...s.order];
      order.splice(i, 1);
      order.splice(j, 0, id);
      return { ...s, order };
    });
  };

  const reorder = (id: string, beforeId: string) => {
    setState((s) => {
      if (id === beforeId) return s;
      const order = s.order.filter((x) => x !== id);
      const j = order.indexOf(beforeId);
      order.splice(j < 0 ? order.length : j, 0, id);
      return { ...s, order };
    });
  };

  const totalWeight = ordered.reduce((acc, it) => acc + (state.weights[it.id] ?? 1), 0) || 1;
  return (
    <div ref={containerRef} className={cx('es-dock', `es-dock--${direction}`, className)}>
      {ordered.map((item, index) => {
        const weight = state.weights[item.id] ?? 1;
        const handle = (
          <button
            type="button"
            className="es-button es-button--ghost es-button--sm es-icon-button es-dock__handle"
            aria-label={`Move ${item.title} panel (drag, or Alt+Arrow keys)`}
            title="Drag to reorder; Alt+Arrow to move"
            draggable
            onDragStart={(e) => {
              e.dataTransfer.setData('text/x-embsim-dock', item.id);
              e.dataTransfer.effectAllowed = 'move';
            }}
            onKeyDown={(e) => {
              if (!e.altKey) return;
              const prev = direction === 'row' ? 'ArrowLeft' : 'ArrowUp';
              const next = direction === 'row' ? 'ArrowRight' : 'ArrowDown';
              if (e.key === prev) {
                e.preventDefault();
                move(item.id, -1);
              } else if (e.key === next) {
                e.preventDefault();
                move(item.id, 1);
              }
            }}
          >
            <GripVertical />
          </button>
        );
        return (
          <div key={item.id} style={{ display: 'contents' }}>
            <div
              className="es-dock__pane"
              style={{ flex: `${weight} 1 0px`, minWidth: direction === 'row' ? item.minSize ?? 120 : 0, minHeight: direction === 'column' ? item.minSize ?? 120 : 0 }}
            >
              <div
                className="es-dock__slot"
                data-drop={dropTarget === item.id ? 'true' : undefined}
                onDragOver={(e) => {
                  if (e.dataTransfer.types.includes('text/x-embsim-dock')) {
                    e.preventDefault();
                    e.dataTransfer.dropEffect = 'move';
                    if (dropTarget !== item.id) setDropTarget(item.id);
                  }
                }}
                onDragLeave={() => setDropTarget((t) => (t === item.id ? null : t))}
                onDrop={(e) => {
                  const id = e.dataTransfer.getData('text/x-embsim-dock');
                  setDropTarget(null);
                  if (id) {
                    e.preventDefault();
                    reorder(id, item.id);
                  }
                }}
              >
                {item.bare ? (
                  item.content
                ) : (
                  <Panel title={item.title} icon={item.icon} leading={handle} actions={item.actions} flush={item.flush} padded={item.padded}>
                    {item.content}
                  </Panel>
                )}
              </div>
            </div>
            {index < ordered.length - 1 && (
              <div
                className="es-dock__gutter"
                role="separator"
                aria-orientation={direction === 'row' ? 'vertical' : 'horizontal'}
                aria-label={`Resize ${item.title}`}
                aria-valuemin={0}
                aria-valuemax={100}
                aria-valuenow={Math.round((weight / totalWeight) * 100)}
                tabIndex={0}
                data-dragging={dragging === index ? 'true' : undefined}
                onPointerDown={onGutterPointerDown(index)}
                onKeyDown={onGutterKeyDown(index)}
              />
            )}
          </div>
        );
      })}
    </div>
  );
}
