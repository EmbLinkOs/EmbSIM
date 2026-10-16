// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixTabs from '@radix-ui/react-tabs';
import type { ReactNode } from 'react';
import { cx } from '../cx';

export interface TabItem<T extends string> {
  value: T;
  label: ReactNode;
  content: ReactNode;
  disabled?: boolean;
}

export interface TabsProps<T extends string> {
  value: T;
  onValueChange: (value: T) => void;
  items: TabItem<T>[];
  label: string;
  className?: string;
}

export function Tabs<T extends string>({ value, onValueChange, items, label, className }: TabsProps<T>) {
  return (
    <RadixTabs.Root value={value} onValueChange={(v) => onValueChange(v as T)} className={cx('es-tabs', className)}>
      <RadixTabs.List className="es-tabs__list" aria-label={label}>
        {items.map((t) => (
          <RadixTabs.Trigger key={t.value} value={t.value} className="es-tabs__trigger" disabled={t.disabled}>
            {t.label}
          </RadixTabs.Trigger>
        ))}
      </RadixTabs.List>
      {items.map((t) => (
        <RadixTabs.Content key={t.value} value={t.value} className="es-tabs__content">
          {t.content}
        </RadixTabs.Content>
      ))}
    </RadixTabs.Root>
  );
}
