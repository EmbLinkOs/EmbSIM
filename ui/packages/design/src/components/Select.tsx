// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixSelect from '@radix-ui/react-select';
import { Check, ChevronDown } from 'lucide-react';
import type { ReactNode } from 'react';
import { cx } from '../cx';

export interface SelectOption<T extends string> {
  value: T;
  label: ReactNode;
  disabled?: boolean;
}

export interface SelectProps<T extends string> {
  value: T;
  onValueChange: (value: T) => void;
  options: SelectOption<T>[];
  label: string;
  placeholder?: string;
  disabled?: boolean;
  size?: 'md' | 'sm';
  className?: string;
  id?: string;
}

/** A listbox-style select (Radix Select: typeahead, arrow keys, ARIA). */
export function Select<T extends string>({ value, onValueChange, options, label, placeholder, disabled, size = 'md', className, id }: SelectProps<T>) {
  return (
    <RadixSelect.Root value={value} onValueChange={(v) => onValueChange(v as T)} disabled={disabled}>
      <RadixSelect.Trigger id={id} className={cx('es-select__trigger', size === 'sm' && 'es-select__trigger--sm', className)} aria-label={label}>
        <RadixSelect.Value placeholder={placeholder} />
        <RadixSelect.Icon>
          <ChevronDown />
        </RadixSelect.Icon>
      </RadixSelect.Trigger>
      <RadixSelect.Portal>
        <RadixSelect.Content className="es-select__content" position="popper" sideOffset={4}>
          <RadixSelect.Viewport>
            {options.map((o) => (
              <RadixSelect.Item key={o.value} value={o.value} className="es-select__item" disabled={o.disabled}>
                <RadixSelect.ItemText>{o.label}</RadixSelect.ItemText>
                <RadixSelect.ItemIndicator>
                  <Check />
                </RadixSelect.ItemIndicator>
              </RadixSelect.Item>
            ))}
          </RadixSelect.Viewport>
        </RadixSelect.Content>
      </RadixSelect.Portal>
    </RadixSelect.Root>
  );
}
