// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Mounts the full shell against the mock transport and checks it with axe
// (SPEC-011 §6.3). Also exercises a transport action through the UI.

import { MockTransport } from '@embsim/protocol';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import axe from 'axe-core';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { App } from './App';
import { disconnectSession } from './connection';
import { useLayout } from './stores/layout';
import { useSession } from './stores/session';

async function mountApp() {
  const transport = new MockTransport({ latencyMs: 0 });
  const utils = render(<App connect={{ transport, isDemo: true }} />);
  await waitFor(() => expect(useSession.getState().status).toBe('open'), { timeout: 5000 });
  await waitFor(() => expect(screen.getByRole('application', { name: 'Schematic canvas' })).toBeTruthy());
  return { ...utils, transport };
}

describe('workbench shell', () => {
  beforeEach(() => {
    window.location.hash = '';
    useLayout.setState({ perspective: 'design' });
  });
  afterEach(() => {
    disconnectSession();
  });

  it('mounts the design perspective with no axe violations', async () => {
    const { container } = await mountApp();
    expect(screen.getAllByText('Demo session').length).toBeGreaterThan(0);
    expect(screen.getByRole('button', { name: 'Run' })).toBeTruthy();
    const results = await axe.run(container, {
      rules: { 'color-contrast': { enabled: false } }, // jsdom has no layout; contrast is checked in the Playwright run
    });
    expect(results.violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target.join(' ')).join(', ')}`)).toEqual([]);
  }, 20000);

  it('runs and pauses through the toolbar and mirrors the clock', async () => {
    await mountApp();
    const run = screen.getByRole('button', { name: 'Run' });
    await act(async () => {
      fireEvent.click(run);
    });
    await waitFor(() => expect(useSession.getState().running).toBe(true));
    await new Promise((r) => setTimeout(r, 120));
    await act(async () => {
      fireEvent.click(screen.getByRole('button', { name: 'Pause' }));
    });
    await waitFor(() => expect(useSession.getState().running).toBe(false));
    expect(useSession.getState().now > 0n).toBe(true);
    expect(screen.getByTestId('clock').textContent).toMatch(/ms|µs|s/);
  }, 20000);

  it('shows the run perspective with instruments and passes axe', async () => {
    const { container } = await mountApp();
    await act(async () => {
      useLayout.getState().setPerspective('run');
    });
    await waitFor(() => expect(screen.getByRole('img', { name: /Oscilloscope/ })).toBeTruthy());
    expect(screen.getByRole('button', { name: /Hold SW1/ })).toBeTruthy();
    const results = await axe.run(container, { rules: { 'color-contrast': { enabled: false } } });
    expect(results.violations.map((v) => `${v.id}: ${v.nodes.map((n) => n.target.join(' ')).join(', ')}`)).toEqual([]);
  }, 20000);
});
