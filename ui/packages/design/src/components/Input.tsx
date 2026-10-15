// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { forwardRef, useId, type InputHTMLAttributes, type ReactNode } from 'react';
import { cx } from '../cx';

export interface InputProps extends Omit<InputHTMLAttributes<HTMLInputElement>, 'size'> {
  /** Visible label; use `aria-label` instead to hide it. */
  label?: ReactNode;
  /** Unit suffix shown after the value, e.g. "Ω", "ms". */
  unit?: string;
  icon?: ReactNode;
  hint?: ReactNode;
  error?: ReactNode;
  mono?: boolean;
  wrapperClassName?: string;
}

/** A text or numeric input with an optional label, leading icon and unit suffix. */
export const Input = forwardRef<HTMLInputElement, InputProps>(function Input(
  { label, unit, icon, hint, error, mono, className, wrapperClassName, id: idProp, ...rest },
  ref,
) {
  const autoId = useId();
  const id = idProp ?? autoId;
  const hintId = `${id}-hint`;
  const control = (
    <span className={cx('es-input', error && 'es-input--invalid', wrapperClassName)}>
      {icon && <span className="es-input__icon">{icon}</span>}
      <input
        ref={ref}
        id={id}
        className={cx('es-input__control', mono && 'es-input__control--mono', className)}
        aria-invalid={error ? true : undefined}
        aria-describedby={hint || error ? hintId : undefined}
        {...rest}
      />
      {unit && (
        <span className="es-input__unit" aria-hidden="true">
          {unit}
        </span>
      )}
    </span>
  );
  if (!label && !hint && !error) return control;
  return (
    <div className="es-field">
      {label && (
        <label className="es-field__label" htmlFor={id}>
          {label}
          {unit && <span className="es-sr-only"> ({unit})</span>}
        </label>
      )}
      {control}
      {(error || hint) && (
        <span id={hintId} className={cx('es-field__hint', error && 'es-field__hint--error')}>
          {error ?? hint}
        </span>
      )}
    </div>
  );
});
