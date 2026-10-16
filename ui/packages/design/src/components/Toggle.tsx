// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixSwitch from '@radix-ui/react-switch';
import * as RadixToggle from '@radix-ui/react-toggle';
import { forwardRef, useId, type ReactNode } from 'react';
import { cx } from '../cx';

export interface ToggleProps {
  checked: boolean;
  onCheckedChange: (checked: boolean) => void;
  label: ReactNode;
  disabled?: boolean;
  /** Hide the visible label (kept for assistive technology). */
  hideLabel?: boolean;
  className?: string;
}

/** An on/off switch with a visible label. */
export const Toggle = forwardRef<HTMLButtonElement, ToggleProps>(function Toggle({ checked, onCheckedChange, label, disabled, hideLabel, className }, ref) {
  const id = useId();
  return (
    <label className={cx('es-switch', className)} htmlFor={id}>
      <RadixSwitch.Root ref={ref} id={id} className="es-switch__root" checked={checked} onCheckedChange={onCheckedChange} disabled={disabled}>
        <RadixSwitch.Thumb className="es-switch__thumb" />
      </RadixSwitch.Root>
      <span className={hideLabel ? 'es-sr-only' : undefined}>{label}</span>
    </label>
  );
});

export interface ToggleButtonProps {
  pressed: boolean;
  onPressedChange: (pressed: boolean) => void;
  label: string;
  children: ReactNode;
  disabled?: boolean;
  size?: 'md' | 'sm';
  className?: string;
}

/** A button with a pressed state (Radix Toggle), announced as a toggle button. */
export const ToggleButton = forwardRef<HTMLButtonElement, ToggleButtonProps>(function ToggleButton(
  { pressed, onPressedChange, label, children, disabled, size = 'md', className },
  ref,
) {
  return (
    <RadixToggle.Root
      ref={ref}
      className={cx('es-button', 'es-icon-button', size === 'sm' && 'es-button--sm', className)}
      pressed={pressed}
      onPressedChange={onPressedChange}
      disabled={disabled}
      aria-label={label}
      title={label}
    >
      {children}
    </RadixToggle.Root>
  );
});
