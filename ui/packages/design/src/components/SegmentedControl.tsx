// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as ToggleGroup from '@radix-ui/react-toggle-group';
import type { ReactNode } from 'react';
import { cx } from '../cx';

export interface SegmentedOption<T extends string> {
  value: T;
  label: ReactNode;
  icon?: ReactNode;
  disabled?: boolean;
  title?: string;
}

export interface SegmentedControlProps<T extends string> {
  value: T;
  onValueChange: (value: T) => void;
  options: SegmentedOption<T>[];
  /** Accessible name of the group. */
  label: string;
  size?: 'md' | 'sm';
  className?: string;
}

/** A single-choice group of buttons (Radix ToggleGroup, roving tabindex, arrow keys). */
export function SegmentedControl<T extends string>({ value, onValueChange, options, label, size = 'md', className }: SegmentedControlProps<T>) {
  return (
    <ToggleGroup.Root
      type="single"
      value={value}
      onValueChange={(v) => {
        if (v) onValueChange(v as T);
      }}
      aria-label={label}
      className={cx('es-segmented', size === 'sm' && 'es-segmented--sm', className)}
    >
      {options.map((o) => (
        <ToggleGroup.Item key={o.value} value={o.value} className="es-segmented__item" disabled={o.disabled} title={o.title}>
          {o.icon}
          {o.label}
        </ToggleGroup.Item>
      ))}
    </ToggleGroup.Root>
  );
}
