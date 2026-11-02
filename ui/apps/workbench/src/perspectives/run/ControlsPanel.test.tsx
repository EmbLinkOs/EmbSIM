// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { ToastProvider } from '@embsim/design';
import { fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useSession } from '../../stores/session';
import { ControlsPanel } from './ControlsPanel';

const calls = vi.hoisted(() => ({ serial: [] as unknown[][], set: [] as unknown[][] }));

vi.mock('../../actions', () => ({
  errorMessage: (e: unknown) => String(e),
  inputs: {
    serial: (...a: unknown[]) => {
      calls.serial.push(a);
      return Promise.resolve();
    },
    set: (...a: unknown[]) => {
      calls.set.push(a);
      return Promise.resolve();
    },
  },
}));

afterEach(() => {
  useSession.setState({ inputs: [], status: 'closed' });
  calls.serial.length = 0;
  calls.set.length = 0;
});

it("sends a terminal's text as a typed line and its hex bytes as written", () => {
  useSession.setState({
    status: 'open',
    inputs: [
      { target: 'VT1', input: 'tx_text', type: 'string' },
      { target: 'VT1', input: 'tx_hex', type: 'string' },
    ] as never,
  });
  render(
    <ToastProvider>
      <ControlsPanel />
    </ToastProvider>,
  );
  const boxes = screen.getAllByRole('textbox');
  const text = boxes[0]!;
  const hex = boxes[1]!;
  fireEvent.change(text, { target: { value: 'hello' } });
  fireEvent.submit(text.closest('form')!);
  fireEvent.change(hex, { target: { value: '01 03 0c 00' } });
  fireEvent.submit(hex.closest('form')!);
  expect(calls.serial).toEqual([['VT1', 'hello\n']]);
  expect(calls.set).toEqual([['VT1', 'tx_hex', '01 03 0c 00']]);
});
