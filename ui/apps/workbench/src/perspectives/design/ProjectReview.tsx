// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button } from '@embsim/design';
import { useMemo, useState } from 'react';
import type { ProjectIssue } from './projectChecks';
export function ProjectReview({
  issues,
  locate,
  exportReport,
}: {
  issues: ProjectIssue[];
  locate(issue: ProjectIssue): void;
  exportReport(): Promise<void>;
}) {
  const [query, setQuery] = useState(''),
    [severity, setSeverity] = useState('all'),
    [source, setSource] = useState('all');
  const filtered = useMemo(
    () =>
      issues.filter(
        (i) =>
          (severity === 'all' || i.severity === severity) &&
          (source === 'all' || i.source === source) &&
          `${i.path || 'Main'} ${i.title} ${i.detail} ${i.code}`.toLowerCase().includes(query.toLowerCase()),
      ),
    [issues, severity, source, query],
  );
  return (
    <section className="wb-project-review" aria-label="Project electrical and drawing review">
      <div className="wb-project-review__filters">
        <label>
          Filter checks
          <input
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Net, reference, rule…"
          />
        </label>
        <label>
          Severity
          <select aria-label="Check severity" value={severity} onChange={(e) => setSeverity(e.target.value)}>
            {['all', 'error', 'warning', 'info'].map((v) => (
              <option key={v}>{v}</option>
            ))}
          </select>
        </label>
        <label>
          Check source
          <select aria-label="Check source" value={source} onChange={(e) => setSource(e.target.value)}>
            {['all', 'electrical', 'live', 'drawing', 'coverage'].map((v) => (
              <option key={v}>{v}</option>
            ))}
          </select>
        </label>
        <Button size="sm" onClick={() => void exportReport()}>
          Export check report
        </Button>
      </div>
      <p className="es-muted">
        {issues.filter((i) => i.severity === 'error').length} errors ·{' '}
        {issues.filter((i) => i.severity === 'warning').length} warnings ·{' '}
        {issues.filter((i) => i.severity === 'info').length} notes. Advisory metadata and drawing checks;
        native validation remains authoritative. Live results reflect the latest observation.
      </p>
      <div className="wb-project-review__results" role="list" aria-label="Project check results">
        {filtered.slice(0, 200).map((i) => (
          <div role="listitem" key={i.id}>
            <button onClick={() => locate(i)}>
              <span>
                <Badge
                  tone={i.severity === 'error' ? 'danger' : i.severity === 'warning' ? 'warning' : 'default'}
                >
                  {i.severity}
                </Badge>{' '}
                {i.path || 'Main'} · {i.title}
              </span>
              <small>
                {i.source} · {i.code}
              </small>
              <p>{i.detail}</p>
            </button>
          </div>
        ))}
        {!filtered.length && <p>No checks match this filter.</p>}
        {filtered.length > 200 && <p>Showing 200 of {filtered.length}; refine the filter.</p>}
      </div>
    </section>
  );
}
