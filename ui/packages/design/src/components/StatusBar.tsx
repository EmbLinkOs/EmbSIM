// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { ReactNode } from 'react';
import { cx } from '../cx';

export interface StatusItem {
  id: string;
  icon?: ReactNode;
  label?: ReactNode;
  value?: ReactNode;
  title?: string;
  /** Pushes following items to the right. */
  spacer?: boolean;
}

export function StatusBar({ items, className }: { items: StatusItem[]; className?: string }) {
  return (
    <footer className={cx('es-statusbar', className)} aria-label="Status" tabIndex={0}>
      {items.map((item) =>
        item.spacer ? (
          <span key={item.id} className="es-statusbar__spacer" />
        ) : (
          <span key={item.id} className="es-statusbar__item" title={item.title}>
            {item.icon}
            {item.label && <span>{item.label}</span>}
            {item.value !== undefined && <span className="es-statusbar__value">{item.value}</span>}
          </span>
        ),
      )}
    </footer>
  );
}
