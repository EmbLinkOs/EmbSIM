// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Input } from '@embsim/design';
import type { LibraryLock, LibraryLockCheck } from '@embsim/protocol';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { platform } from '../../platform/platform';
import { getClient } from '../../connection';
import { useProject } from '../../stores/project';

export function LibraryLockPanel() {
  const project = useProject((s) => s.project), busy = useProject((s) => s.busy);
  const [pending, setPending] = useState(false), [error, setError] = useState('');
  const [fileQuery, setFileQuery] = useState('');
  const [candidate, setCandidate] = useState<LibraryLock | null>(null);
  const [checked, setChecked] = useState<{lock: LibraryLock | undefined; result: LibraryLockCheck} | null>(null);
  const saved = project?.library_lock;
  const result = checked && checked.lock === saved ? checked.result : null;
  const act = async (op: 'capture' | 'check' | 'apply' | 'remove') => {
    if (!project || pending || busy) return;
    setPending(true); setError('');
    try {
      if (op === 'capture') {
        const value = await getClient().project.library_lock({op: 'capture'});
        if (!('kind' in value) || value.kind !== 'library_lock') throw new Error('Native session returned an invalid library lock');
        setCandidate(value);
        const check = await getClient().project.library_lock({op: 'check', lock: saved ?? value}) as LibraryLockCheck;
        setChecked({lock: saved, result: check});
      } else if (op === 'check') {
        setChecked({lock: saved, result: await getClient().project.library_lock({op: 'check'}) as LibraryLockCheck});
      } else {
        const value = op === 'apply' ? candidate : null;
        if (op === 'apply' && !value) return;
        await useProject.getState().mutate(op === 'apply' ? 'Pin library inputs' : 'Remove library baseline', (draft) => {
          if (value) draft.library_lock = value;
          else delete draft.library_lock;
          return draft;
        });
        setCandidate(null); setChecked(null);
      }
    } catch (e) { setError(errorMessage(e)); }
    finally { setPending(false); }
  };
  const disabled = pending || busy || !project;
  const files = candidate ? Object.entries(candidate.libraries).flatMap(([library, entry]) =>
    Object.entries(entry.files).map(([path, sha256]) => ({library, path, sha256})))
    .filter((f) => `${f.library}/${f.path} ${f.sha256}`.toLowerCase().includes(fileQuery.toLowerCase())) : [];
  const exportReview = async () => {
    if (!candidate) return;
    try { await (await platform()).saveTextFile(`${project?.name ?? 'project'}-library-lock.json`, JSON.stringify(candidate, null, 2), 'application/json'); }
    catch (e) { setError(errorMessage(e)); }
  };
  return <section aria-label="Library reproducibility" className="wb-library-lock">
    <div className="wb-library-lock__heading"><strong>Library reproducibility</strong>
      <Badge tone={saved ? 'accent' : 'warning'}>{saved ? 'Pinned library inputs' : 'Unpinned libraries'}</Badge>
    </div>
    <p>Pin distributed library contents (non-hidden files) and the effective catalog selection. Locked projects reject drift before model instantiation. The baseline describes files now on disk for subsequent loads. Firmware and resources outside library roots are separate inputs.</p>
    <div className="wb-library-lock__actions">
      <Button size="sm" variant="ghost" disabled={disabled} onClick={() => void act('capture')}>Review current libraries</Button>
      <Button size="sm" variant="ghost" disabled={disabled || !saved} onClick={() => void act('check')}>Check saved lock</Button>
      <Button size="sm" variant="ghost" disabled={disabled || !saved} onClick={() => void act('remove')}>Remove library lock</Button>
    </div>
    {pending && <p role="status">Reading and hashing library inputs…</p>}
    {error && <p role="alert">{error}</p>}
    {result && <div role="status">
      {saved ? result.ok ? 'Saved library lock matches current inputs.' : 'Library inputs have changed.' : 'Current inputs are ready for review.'}
      {result.changes.length > 0 && <details><summary>{result.changes.length} differences</summary>
        <ul>{result.changes.map((c, i) => <li key={i}><code>{c.scope}{c.path ? ` / ${c.path}` : ''}</code> — {c.change}</li>)}</ul>
      </details>}
    </div>}
    {candidate && <div>
      <p>Review baseline: {Object.keys(candidate.libraries).length} libraries, {Object.values(candidate.libraries).reduce((n, l) => n + Object.keys(l.files).length, 0)} files, {Object.keys(candidate.catalog).length} catalog selections. Applying changes project metadata and preserves simulation time. It does not save the project file automatically.</p>
      <details><summary>Libraries and versions</summary><ul>{Object.entries(candidate.libraries).map(([name, library]) => <li key={name}><strong>{name}</strong> {library.version} · {Object.keys(library.files).length} files</li>)}</ul></details>
      <details><summary>Review file identities</summary>
        <label>Find library file<Input aria-label="Find library file" value={fileQuery} onChange={(e) => setFileQuery(e.target.value)} /></label>
        <p>{files.length} matching files. Showing the first 100; exported review contains every file.</p>
        <ul>{files.slice(0, 100).map((f) => <li key={`${f.library}/${f.path}`}><code>{f.library}/{f.path}</code><br /><code>{f.sha256}</code></li>)}</ul>
      </details>
      <Button size="sm" variant="ghost" disabled={disabled} onClick={() => void exportReview()}>Export reviewed lock</Button>
      <Button size="sm" disabled={disabled} onClick={() => void act('apply')}>{saved ? 'Replace saved library lock' : 'Pin reviewed library inputs'}</Button>
      <Button size="sm" variant="ghost" disabled={disabled} onClick={() => setCandidate(null)}>Discard review</Button>
    </div>}
    <p>Review is read-only. Pinning, replacement and removal are explicit undoable edits. Hashes identify bytes; they do not establish model accuracy or publisher trust.</p>
  </section>;
}
