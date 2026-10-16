// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Project } from '@embsim/protocol';
import { useProject } from './project';
const rpc = vi.hoisted(() => ({ get: vi.fn(), topology: vi.fn() }));
vi.mock('../connection', () => ({ getClient: () => ({ project: rpc }), primeStores: vi.fn() }));
afterEach(() => { useProject.setState({ project: null }); vi.clearAllMocks(); });
describe('project refresh revision identity', () => {
  it('keeps active drafting sources valid when a notification repeats unchanged content', async () => {
    const source: Project = { schema_version: 1, kind: 'project', name: 'same', components: [], nets: {} };
    useProject.setState({ project: source });
    rpc.get.mockResolvedValue({ project: structuredClone(source) });
    rpc.topology.mockResolvedValue(null);
    await useProject.getState().refresh();
    await useProject.getState().refresh();
    expect(useProject.getState().project).toBe(source);
  });
  it('replaces the source when server content actually changes', async () => {
    const source: Project = { schema_version: 1, kind: 'project', name: 'a', components: [], nets: {} };
    const changed = { ...source, name: 'b' };
    useProject.setState({ project: source });
    rpc.get.mockResolvedValue({ project: changed }); rpc.topology.mockResolvedValue(null);
    await useProject.getState().refresh();
    expect(useProject.getState().project).toBe(changed);
  });
});
