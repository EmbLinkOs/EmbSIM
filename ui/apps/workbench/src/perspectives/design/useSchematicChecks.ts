// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useEffect, useState } from 'react';
import type { Project } from '@embsim/protocol';
import type { SceneCatalog, SchematicIssue } from '@embsim/scene';
import ChecksWorker from './checks.worker?worker&inline';

export function useSchematicChecks(project: Project | null, catalog: SceneCatalog) {
  const [revision, setRevision] = useState(0);
  const [report, setReport] = useState<{ source: Project | null; catalog: SceneCatalog; issues: SchematicIssue[]; error?: string } | null>(null);
  useEffect(() => {
    if (!project) return;
    let worker: Worker | undefined;
    let deadline: number | undefined;
    let active = true;
    const finish = (issues: SchematicIssue[], error?: string) => {
      if (!active) return;
      active = false;
      window.clearTimeout(deadline); worker?.terminate();
      setReport({ source: project, catalog, issues, error });
    };
    const debounce = window.setTimeout(() => {
      try {
        worker = new ChecksWorker();
        deadline = window.setTimeout(() => finish([], 'Checks timed out. Reduce the sheet size or retry.'), 20000);
        worker.onmessage = (e: MessageEvent<{ issues?: SchematicIssue[]; error?: string }>) => finish(e.data.issues ?? [], e.data.error);
        worker.onerror = (e) => finish([], e.message || 'The checks worker could not start.');
        worker.postMessage({ project, catalog });
      } catch (e) { finish([], e instanceof Error ? e.message : 'Checks could not start.'); }
    }, 120);
    return () => { active = false; window.clearTimeout(debounce); window.clearTimeout(deadline); worker?.terminate(); };
  }, [project, catalog, revision]);
  const current = report?.source === project && report?.catalog === catalog;
  return { issues: current ? report.issues : [], pending: !!project && !current, error: current ? report.error : undefined, refresh: () => { setReport(null); setRevision((n) => n + 1); } };
}
