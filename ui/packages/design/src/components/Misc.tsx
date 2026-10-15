// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { HTMLAttributes, ReactNode } from 'react';
import { cx } from '../cx';

export type BadgeTone = 'default' | 'accent' | 'warning' | 'danger' | 'info';

export function Badge({ tone = 'default', className, children, ...rest }: HTMLAttributes<HTMLSpanElement> & { tone?: BadgeTone }) {
  return (
    <span className={cx('es-badge', tone !== 'default' && `es-badge--${tone}`, className)} {...rest}>
      {children}
    </span>
  );
}

export function Kbd({ children }: { children: ReactNode }) {
  return <kbd className="es-kbd">{children}</kbd>;
}

export function EmptyState({ icon, title, children }: { icon?: ReactNode; title: ReactNode; children?: ReactNode }) {
  return (
    <div className="es-empty">
      {icon}
      <div className="es-empty__title">{title}</div>
      {children && <div>{children}</div>}
    </div>
  );
}

export type SignalKind = 'low' | 'high' | 'z' | 'x' | 'analog' | 'idle';

/** A colour swatch plus a textual level so state is readable without colour. */
export function SignalChip({ kind, label }: { kind: SignalKind; label?: ReactNode }) {
  const names: Record<SignalKind, string> = { low: 'low', high: 'high', z: 'floating', x: 'contention', analog: 'analog', idle: 'unknown' };
  return (
    <span className={cx('es-signal', `es-signal--${kind}`)}>
      <span className="es-signal__swatch" aria-hidden="true" />
      <span>{label ?? names[kind]}</span>
    </span>
  );
}

export interface Column<T> {
  id: string;
  header: ReactNode;
  cell: (row: T) => ReactNode;
  width?: string;
  mono?: boolean;
}

export interface TableProps<T> {
  columns: Column<T>[];
  rows: T[];
  rowKey: (row: T) => string;
  selectedKey?: string | null;
  onSelect?: (row: T) => void;
  label: string;
  empty?: ReactNode;
}

/** A compact data table; rows are focusable and selectable when `onSelect` is given. */
export function Table<T>({ columns, rows, rowKey, selectedKey, onSelect, label, empty }: TableProps<T>) {
  return (
    <table className="es-table" aria-label={label}>
      <thead>
        <tr>
          {columns.map((c) => (
            <th key={c.id} style={c.width ? { width: c.width } : undefined} scope="col">
              {c.header}
            </th>
          ))}
        </tr>
      </thead>
      <tbody>
        {rows.length === 0 && (
          <tr>
            <td colSpan={columns.length} className="es-muted">
              {empty ?? 'Nothing to show'}
            </td>
          </tr>
        )}
        {rows.map((row) => {
          const key = rowKey(row);
          const selectable = Boolean(onSelect);
          return (
            <tr
              key={key}
              data-selected={selectedKey === key ? 'true' : undefined}
              tabIndex={selectable ? 0 : undefined}
              aria-selected={selectable ? selectedKey === key : undefined}
              onClick={selectable ? () => onSelect?.(row) : undefined}
              onKeyDown={
                selectable
                  ? (e) => {
                      if (e.key === 'Enter' || e.key === ' ') {
                        e.preventDefault();
                        onSelect?.(row);
                      }
                    }
                  : undefined
              }
            >
              {columns.map((c) => (
                <td key={c.id} className={c.mono ? 'es-mono' : undefined}>
                  {c.cell(row)}
                </td>
              ))}
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}
