// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { arrange, autoRoute, createLayout } from '@embsim/scene';
import type { LayoutJob } from './layoutPlanner';
self.onmessage = (event: MessageEvent<LayoutJob>) => {
  try {
    const job = event.data;
    const result = job.kind === 'layout' ? createLayout(job.project, job.catalog, job.options)
      : job.kind === 'route' ? autoRoute(job.project, job.catalog, job.affected ? new Set(job.affected) : undefined)
        : arrange(job.project, job.catalog, job.ids, job.action);
    self.postMessage({ result });
  } catch (error) { self.postMessage({ error: error instanceof Error ? error.message : String(error) }); }
};
