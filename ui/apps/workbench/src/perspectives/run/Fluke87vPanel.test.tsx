// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { ToastProvider } from '@embsim/design';
import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useSession } from '../../stores/session';
import { Fluke87vPanel } from './Fluke87vPanel';

const sent = vi.hoisted(() => [] as unknown[]);

vi.mock('../../connection', () => ({
  getClient: () => ({
    input: {
      set: (p: unknown) => {
        sent.push(p);
        return Promise.resolve({});
      },
    },
  }),
}));

afterEach(() => {
  useSession.setState({ outputs: {} });
  sent.length = 0;
});

const show = () =>
  render(
    <ToastProvider>
      <Fluke87vPanel part="DMM" />
    </ToastProvider>,
  );

it('shows the reading, the unit and the jacks the switch position uses', () => {
  useSession.setState({ outputs: { 'DMM.position': 'vdc', 'DMM.display': '5.000', 'DMM.unit': 'V', 'DMM.fuse_ma': 'ok', 'DMM.fuse_a': 'ok' } });
  show();
  expect(screen.getByTestId('dmm-reading').textContent).toBe('5.000');
  expect(screen.getByRole('status').textContent).toContain('DC');
  expect(screen.getByRole('radio', { name: 'V⎓' }).getAttribute('aria-checked')).toBe('true');
  expect(screen.getByText(/Red lead in V/)).toBeTruthy();
  expect(screen.queryByRole('button', { name: /Replace/ })).toBeNull();
});

it('turns the switch and replaces a blown fuse', async () => {
  useSession.setState({ outputs: { 'DMM.position': 'vdc', 'DMM.display': 'OL', 'DMM.unit': 'V', 'DMM.fuse_ma': 'blown', 'DMM.fuse_a': 'ok' } });
  show();
  await act(async () => {
    fireEvent.click(screen.getByRole('radio', { name: 'mA⎓' }));
  });
  expect(sent).toEqual([{ target: 'DMM', input: 'function', value: 'ma_dc' }]);
  expect(screen.getByText(/440 mA: blown/)).toBeTruthy();
  await act(async () => {
    fireEvent.click(screen.getByRole('button', { name: /Replace/ }));
  });
  expect(sent.slice(1)).toEqual([
    { target: 'DMM', input: 'replace_fuse', value: true },
    { target: 'DMM', input: 'replace_fuse', value: false },
  ]);
});
