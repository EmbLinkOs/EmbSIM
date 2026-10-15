// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { Button, IconButton, Input, matchCommand, SegmentedControl, type Command } from './index';

describe('design components', () => {
  it('renders an icon button with an accessible name', () => {
    render(
      <IconButton label="Run">
        <svg />
      </IconButton>,
    );
    expect(screen.getByRole('button', { name: 'Run' })).toBeTruthy();
  });

  it('exposes a pressed state', () => {
    render(<Button pressed>Hold</Button>);
    expect(screen.getByRole('button', { name: 'Hold' }).getAttribute('aria-pressed')).toBe('true');
  });

  it('labels an input with its unit', () => {
    render(<Input label="Resistance" unit="Ω" defaultValue="10k" />);
    const input = screen.getByLabelText(/Resistance/) as HTMLInputElement;
    expect(input.value).toBe('10k');
  });

  it('switches a segmented control by click', () => {
    let value = 'a';
    render(
      <SegmentedControl
        label="Mode"
        value={value}
        onValueChange={(v) => {
          value = v;
        }}
        options={[
          { value: 'a', label: 'A' },
          { value: 'b', label: 'B' },
        ]}
      />,
    );
    fireEvent.click(screen.getByRole('radio', { name: 'B' }));
    expect(value).toBe('b');
  });

  it('matches commands by substring and subsequence', () => {
    const c: Command = { id: 'run', label: 'Run simulation', group: 'Control', run: () => undefined };
    expect(matchCommand('', c)).toBe(0);
    expect(matchCommand('sim', c)).toBeGreaterThanOrEqual(0);
    expect(matchCommand('rsm', c)).toBeGreaterThanOrEqual(1000);
    expect(matchCommand('xyz', c)).toBe(-1);
  });
});
