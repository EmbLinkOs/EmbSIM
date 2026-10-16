// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { newProject, projectName } from './NewProjectDialog';

describe('new projects', () => {
  it('names a project as the schema takes it', () => {
    expect(projectName('  My first  circuit! ')).toBe('My-first-circuit');
    expect(projectName('***')).toBe('untitled');
    expect(projectName('uno_spi.v2')).toBe('uno_spi.v2');
  });

  it('starts from a board as B1, or from a blank sheet', () => {
    const p = newProject('demo', 'emblink/arduino_uno') as unknown as { boards: { id: string; board: string }[]; components: unknown[]; nets: object };
    expect(p.boards).toEqual([{ id: 'B1', board: 'emblink/arduino_uno', layout: { x: 300, y: 240 } }]);
    expect(p.components).toEqual([]);
    expect((newProject('blank', null) as unknown as { boards: unknown[] }).boards).toEqual([]);
  });
});
