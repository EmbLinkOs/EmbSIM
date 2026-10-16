// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { forwardRef, type ButtonHTMLAttributes, type ReactNode } from 'react';
import { cx } from '../cx';

export type ButtonVariant = 'default' | 'primary' | 'ghost' | 'danger';

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  size?: 'md' | 'sm';
  icon?: ReactNode;
  pressed?: boolean;
}

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button(
  { variant = 'default', size = 'md', icon, pressed, className, children, type = 'button', ...rest },
  ref,
) {
  return (
    <button
      ref={ref}
      type={type}
      className={cx('es-button', variant !== 'default' && `es-button--${variant}`, size === 'sm' && 'es-button--sm', className)}
      aria-pressed={pressed}
      {...rest}
    >
      {icon}
      {children}
    </button>
  );
});

export interface IconButtonProps extends Omit<ButtonProps, 'icon' | 'children'> {
  /** The accessible name; also the tooltip when `title` is not given. */
  label: string;
  children: ReactNode;
}

/** A square button holding only an icon; `label` is required for assistive technology. */
export const IconButton = forwardRef<HTMLButtonElement, IconButtonProps>(function IconButton({ label, className, children, ...rest }, ref) {
  return (
    <Button ref={ref} className={cx('es-icon-button', className)} aria-label={label} title={rest.title ?? label} {...rest}>
      {children}
    </Button>
  );
});
