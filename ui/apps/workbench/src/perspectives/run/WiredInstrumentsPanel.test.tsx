// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { WiredInstrumentsPanel } from './WiredInstrumentsPanel';

afterEach(() => { useProject.setState({ project: null, hierarchy: [] }); useSession.setState({ outputs: {} }); });
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
  expect(trace).toHaveBeenCalledWith('device:A1.current', 200000);
  const scope = within(screen.getByRole('region', { name: 'S1 Scope input' }));
  expect(scope.getByText('Input 1 MΩ ∥ 20 pF')).toBeTruthy();
  expect(scope.getByRole('button', { name: 'Add trace' }).hasAttribute('disabled')).toBe(true);
  expect(scope.queryByRole('group', { name: /Terminal voltage/ })).toBeNull();
});

it('shows nested native values, qualified traces and a shared-definition warning', () => {
  useProject.setState({ editable: true, busy: false, hierarchy: [{ path: '', block: '', nets: {} }, { path: 'A', block: 'front', nets: {}, params: { r: 2e6 } }, { path: 'B', block: 'front', nets: {}, params: { r: 3e6 } }], project: { schema_version: 1, kind: 'project', name: 'nested', components: [{ id: 'A', component: 'block/front' }, { id: 'B', component: 'block/front' }], nets: {}, blocks: { front: { name: 'Front', params: { r: { type: 'number', default: '1meg' } }, components: [{ id: 'S', component: 'emblink/scope_input', params: { input_resistance: '$r' } }], nets: { IN: ['S.+'], REF: ['S.-'] }, ports: { IN: 'S.+', REF: 'S.-' } } } } });
  useSession.setState({ outputs: { 'A.S.voltage': 2.2, 'B.S.voltage': 3.3 } });
  const locate = vi.fn(), trace = vi.fn();
  render(<WiredInstrumentsPanel onLocate={locate} onTrace={trace} />);
  const a = within(screen.getByRole('region', { name: 'A.S Scope input' }));
  expect(a.getByText('Input 2 MΩ ∥ 15 pF')).toBeTruthy();
  expect(a.getByRole('group', { name: 'Terminal voltage: 2.2 V' })).toBeTruthy();
  fireEvent.click(a.getByRole('button', { name: 'Open instrument sheet' }));
  expect(locate).toHaveBeenCalledWith('S', 'A');
  fireEvent.change(a.getByLabelText('A.S sample rate'), { target: { value: '500000' } });
  fireEvent.click(a.getByRole('button', { name: 'Trace current' }));
  expect(trace).toHaveBeenCalledWith('device:A.S.current', 500000);
  fireEvent.click(a.getByRole('button', { name: 'Configure instrument' }));
  expect(a.getByText(/changes affect 2 instances/)).toBeTruthy();
  expect(a.getByLabelText('A.S input_resistance').hasAttribute('disabled')).toBe(true);
  expect(a.getByLabelText('A.S input preset').hasAttribute('disabled')).toBe(true);
});

it('refuses invalid trace rates and keeps preset selection consistent after a custom edit', () => {
  useProject.setState({ editable: true, busy: false, hierarchy: [], project: { schema_version: 1, kind: 'project', name: 'settings', components: [{ id: 'S', component: 'emblink/scope_input' }], nets: { A: ['S.+'], B: ['S.-'] } } });
  const trace = vi.fn(); render(<WiredInstrumentsPanel onLocate={vi.fn()} onTrace={trace} />);
  fireEvent.change(screen.getByLabelText('S sample rate'), { target: { value: '-1' } });
  fireEvent.click(screen.getByRole('button', { name: 'Add trace' }));
  expect(trace).not.toHaveBeenCalled();
  expect(screen.getByRole('alert').textContent).toContain('1 to 10,000,000');
  fireEvent.click(screen.getByRole('button', { name: 'Configure instrument' }));
  fireEvent.change(screen.getByLabelText('S input preset'), { target: { value: '10x' } });
  expect((screen.getByLabelText('S input_resistance') as HTMLInputElement).value).toBe('10meg');
  fireEvent.change(screen.getByLabelText('S input_capacitance'), { target: { value: '20p' } });
  expect((screen.getByLabelText('S input preset') as HTMLSelectElement).value).toBe('custom');
});
