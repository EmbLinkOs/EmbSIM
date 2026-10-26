// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { render, screen } from '@testing-library/react';
import type { ComponentDescription } from '@embsim/protocol';
import rgb from '../../../../../../catalog/components/wp154a4sureqbfzgc/component.json';
import { afterEach, expect, it } from 'vitest';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { MetersPanel } from './MetersPanel';

afterEach(() => {
  useProject.setState({ project: null, catalog: {} });
  useSession.setState({ outputs: {}, nets: {} });
});

it('uses declared output units for named currents and custom quantities', () => {
  const description = { ...rgb, outputs: [...rgb.outputs, { name: 'custom', type: 'number', unit: '°C' }] } as unknown as ComponentDescription;
  useProject.setState({ project: { schema_version: 1, kind: 'project', name: 'units', policy: 'electrical', components: [{ id: 'D1', component: description.id }], nets: {} },
    catalog: { [description.id]: { description, files: {} } } });
  useSession.setState({ outputs: { 'D1.red_current': .003, 'D1.custom': 2 }, nets: {} });
  render(<MetersPanel />);
  expect(screen.getByRole('group', { name: 'D1.red_current: 3 mA' })).toBeTruthy();
  expect(screen.getByRole('group', { name: 'D1.custom: 2 °C' })).toBeTruthy();
});
