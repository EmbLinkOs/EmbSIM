// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import LayoutWorker from './layout.worker?worker&inline';
import type { Project } from '@embsim/protocol';
import type { Arrangement, LayoutOptions, LayoutResult, SceneCatalog } from '@embsim/scene';
export type LayoutJob = { project: Project; catalog: SceneCatalog } & (
  { kind: 'layout'; options: LayoutOptions } |
  { kind: 'route'; affected?: string[] } |
  { kind: 'arrange'; ids: string[]; action: Arrangement }
);

/** Heavy preview planning never occupies the canvas event loop. Each job owns
 * a worker and has a bounded lifetime; stale previews cannot be applied. */
export function planLayout(job: LayoutJob): Promise<LayoutResult> {
  return new Promise((resolve, reject) => {
    const worker = new LayoutWorker();
    const finish = () => { clearTimeout(timer); worker.terminate(); };
    const timer = window.setTimeout(() => { finish(); reject(new Error('Layout planning timed out. Try a smaller selection.')); }, 20000);
    worker.onmessage = (event: MessageEvent<{ result?: LayoutResult; error?: string }>) => {
      finish();
      if (event.data.result) resolve(event.data.result);
      else reject(new Error(event.data.error ?? 'Layout planning failed.'));
    };
    worker.onerror = (event) => { finish(); reject(new Error(event.message || 'The layout worker could not start.')); };
    try { worker.postMessage(job); } catch (error) { finish(); reject(error); }
  });
}
