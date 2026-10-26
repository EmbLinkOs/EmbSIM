// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The scenario editor on a project without scenarios or probes: it renders
// once and settles (a store selector that made a new empty list each render
// rendered without end, React's error 185, and blanked the workbench).

import { TooltipProvider, ToastProvider } from '@embsim/design';
import type { Project } from '@embsim/protocol';
import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { emptyScenario, useVerify } from '../../stores/verify';
import { ScenarioEditor } from './ScenarioEditor';

const bare: Project = {
  schema_version: 1,
  kind: 'project',
  name: 'bare',
  policy: 'electrical',
  boards: [{ id: 'B1', board: 'emblink/bluepill' }],
  components: [],
  nets: {},
} as unknown as Project;

describe('scenario editor', () => {
  afterEach(() => {
    useVerify.getState().closeDraft();
    useProject.setState({ project: null });
  });


  it('blocks run and save while expectation JSON is invalid and recovers when corrected', async () => {
    useProject.setState({ project: bare });
    useSession.setState({ capabilities: ['scenario', 'scenario.edit'], running: false });
    useVerify.getState().openDraft(null, { ...emptyScenario(), assertions: [
      { id: 'check', at: '10ms', probe: 'led', expect: { logic: '1' } },
    ] });
    render(<TooltipProvider><ToastProvider><ScenarioEditor /></ToastProvider></TooltipProvider>);
    const run = screen.getByRole('button', { name: 'Run' }) as HTMLButtonElement;
    const save = screen.getByRole('button', { name: 'Save' }) as HTMLButtonElement;
    expect(run.disabled).toBe(false);
    useVerify.setState({ results: { check: { ok: true, observed: 1 } } });
    fireEvent.change(screen.getByLabelText('Assertion 1 expectation'), { target: { value: '{broken' } });
    expect(run.disabled).toBe(true);
    expect(save.disabled).toBe(true);
    expect(useVerify.getState().results).toEqual({});
    expect(useVerify.getState().draft?.dirty).toBe(true);
    expect(screen.getByRole('alert').textContent).toContain('valid JSON');
    fireEvent.change(screen.getByLabelText('Assertion 1 expectation'), { target: { value: '{"logic":"0"}' } });
    expect(run.disabled).toBe(false);
    expect(save.disabled).toBe(false);
    expect(useVerify.getState().draft?.scenario.assertions[0]?.expect).toEqual({ logic: '0' });
    fireEvent.change(screen.getByLabelText('Assertion 1 time'), { target: { value: '-2ms' } });
    expect(run.disabled).toBe(true);
    expect(save.disabled).toBe(true);
  });

  it('renders a project without scenarios or probes', async () => {
    useProject.setState({ project: bare });
    useSession.setState({ capabilities: ['scenario', 'scenario.edit'] });
    render(
      <TooltipProvider>
        <ToastProvider>
          <ScenarioEditor />
        </ToastProvider>
      </TooltipProvider>,
    );
    expect(screen.getByText('The project lists no scenarios.')).toBeTruthy();
    // a new scenario's assertions table reads the project's probes
    await act(async () => {
      useVerify.getState().openDraft(null, emptyScenario());
    });
    expect(screen.getByText('No probes')).toBeTruthy();
  });

  it('shows measured windows and blocks nonnumeric measurement bounds', async () => {
    useProject.setState({ project: bare });
    useSession.setState({ capabilities: ['scenario', 'scenario.edit'], running: false });
    useVerify.getState().openDraft(null, { ...emptyScenario(), assertions: [
      { id: 'mean', between: ['1ms', '5ms'], probe: 'current', measure: 'mean', expect: { min: 0, max: .01 } },
    ] });
    render(<TooltipProvider><ToastProvider><ScenarioEditor /></ToastProvider></TooltipProvider>);
    expect(screen.getByRole('combobox', { name: 'Assertion 1 measurement' }).textContent).toContain('Time mean');
    expect((screen.getByRole('button', { name: 'Run' }) as HTMLButtonElement).disabled).toBe(false);
    fireEvent.change(screen.getByLabelText('Assertion 1 expectation'), { target: { value: '{"logic":"1"}' } });
    expect(screen.getByRole('alert').textContent).toContain('measurements require numeric');
    expect((screen.getByRole('button', { name: 'Run' }) as HTMLButtonElement).disabled).toBe(true);
  });
});
