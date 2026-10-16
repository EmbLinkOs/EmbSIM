// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The scenario editor on a project without scenarios or probes: it renders
// once and settles (a store selector that made a new empty list each render
// rendered without end, React's error 185, and blanked the workbench).

import { TooltipProvider, ToastProvider } from '@embsim/design';
import type { Project } from '@embsim/protocol';
import { act, render, screen } from '@testing-library/react';
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
});
