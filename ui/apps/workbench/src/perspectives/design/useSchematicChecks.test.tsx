// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { Project } from '@embsim/protocol';
import { useSchematicChecks } from './useSchematicChecks';

const jobs = vi.hoisted(() => {
  const instances: Stub[] = [];
  class Stub {
    onmessage: ((e: MessageEvent) => void) | null = null;
    onerror: ((e: { message: string }) => void) | null = null;
    postMessage = vi.fn();
    terminate = vi.fn();
    constructor() { instances.push(this); }
  }
  return { instances, Stub };
});
vi.mock('./checks.worker?worker&inline', () => ({ default: jobs.Stub }));
const project = (name: string): Project => ({ schema_version: 1, kind: 'project', name, components: [], nets: {} });
afterEach(() => { vi.useRealTimers(); jobs.instances.length = 0; });

describe('revision-bound schematic checks', () => {
  it('cancels old jobs and refuses a late result from a previous revision', () => {
    vi.useFakeTimers();
    const catalog = {};
    const view = renderHook(({ source }) => useSchematicChecks(source, catalog), { initialProps: { source: project('a') } });
    act(() => vi.advanceTimersByTime(120));
    const old = jobs.instances[0]!;
    view.rerender({ source: project('b') });
    expect(old.terminate).toHaveBeenCalled();
    act(() => old.onmessage?.({ data: { issues: [{ id: 'obsolete' }] } } as MessageEvent));
    expect(view.result.current.pending).toBe(true);
    expect(view.result.current.issues).toEqual([]);
    act(() => vi.advanceTimersByTime(120));
    act(() => jobs.instances[1]!.onmessage?.({ data: { issues: [] } } as MessageEvent));
    expect(view.result.current.pending).toBe(false);
    view.unmount();
  });
  it('surfaces startup failure, allows retry and terminates timed-out work', () => {
    vi.useFakeTimers();
    const catalog = {}, source = project('a');
    const view = renderHook(() => useSchematicChecks(source, catalog));
    act(() => vi.advanceTimersByTime(120));
    act(() => jobs.instances[0]!.onerror?.({ message: 'Worker unavailable' }));
    expect(view.result.current.error).toBe('Worker unavailable');
    act(() => view.result.current.refresh());
    expect(view.result.current.pending).toBe(true);
    act(() => vi.advanceTimersByTime(20120));
    expect(view.result.current.error).toContain('timed out');
    expect(jobs.instances[1]!.terminate).toHaveBeenCalled();
    act(() => jobs.instances[1]!.onmessage?.({ data: { issues: [] } } as MessageEvent));
    expect(view.result.current.error).toContain('timed out');
    view.unmount();
  });
});
