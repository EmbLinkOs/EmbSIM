// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixSlider from '@radix-ui/react-slider';
import { cx } from '../cx';

export interface SliderProps {
  value: number;
  onValueChange: (value: number) => void;
  min: number;
  max: number;
  step?: number;
  label: string;
  /** Formats the value for the readout and for `aria-valuetext`. */
  format?: (value: number) => string;
  disabled?: boolean;
  showValue?: boolean;
  className?: string;
}

/** A single-thumb slider with an optional numeric readout (arrow keys step, Home/End jump). */
export function Slider({ value, onValueChange, min, max, step = 1, label, format, disabled, showValue = true, className }: SliderProps) {
  const text = format ? format(value) : String(value);
  return (
    <div className={cx('es-slider-row', className)}>
      <RadixSlider.Root
        className="es-slider"
        value={[value]}
        onValueChange={(v) => onValueChange(v[0] ?? value)}
        min={min}
        max={max}
        step={step}
        disabled={disabled}
      >
        <RadixSlider.Track className="es-slider__track">
          <RadixSlider.Range className="es-slider__range" />
        </RadixSlider.Track>
        <RadixSlider.Thumb className="es-slider__thumb" aria-label={label} aria-valuetext={text} />
      </RadixSlider.Root>
      {showValue && (
        <span className="es-slider-row__value" aria-hidden="true">
          {text}
        </span>
      )}
    </div>
  );
}
