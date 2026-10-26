// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { WiredInstrumentsPanel } from './WiredInstrumentsPanel';

afterEach(() => { useProject.setState({ project: null }); useSession.setState({ outputs: {} }); });
it('shows connected impedance and engine readings, then traces the correct physical quantity', () => {
  useProject.setState({ project: { schema_version: 1, kind: 'project', name: 'instruments', policy: 'electrical', components: [{ id: 'A1', component: 'emblink/ammeter', params: { shunt_resistance: 10 } }, { id: 'S1', component: 'emblink/scope_input', params: { input_capacitance: '20p' } }], nets: { PLUS: ['A1.+'], MINUS: ['A1.-'] } } });
  useSession.setState({ outputs: { 'A1.voltage': .45, 'A1.current': .045 } });
  const locate = vi.fn(), trace = vi.fn();
  render(<WiredInstrumentsPanel onLocate={locate} onTrace={trace} />);
  const a = within(screen.getByRole('region', { name: 'A1 Ammeter' }));
  expect(a.getByText('Shunt 10 Ω')).toBeTruthy();
  expect(a.getByRole('group', { name: 'Burden voltage: 450 mV' })).toBeTruthy();
  expect(a.getByRole('group', { name: 'Current into +: 45 mA' })).toBeTruthy();
  fireEvent.click(a.getByRole('button', { name: 'View leads' }));
  fireEvent.click(a.getByRole('button', { name: 'Add trace' }));
  expect(locate).toHaveBeenCalledWith('A1');
  expect(trace).toHaveBeenCalledWith('device:A1.current');
  const scope = within(screen.getByRole('region', { name: 'S1 Scope input' }));
  expect(scope.getByText('Input 1 MΩ ∥ 20 pF')).toBeTruthy();
  expect(scope.getByRole('button', { name: 'Add trace' }).hasAttribute('disabled')).toBe(true);
  expect(scope.queryByRole('group', { name: /Terminal voltage/ })).toBeNull();
});
