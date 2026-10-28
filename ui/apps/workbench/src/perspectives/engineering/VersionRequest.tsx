// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input } from '@embsim/design';
import { sheetAt, type ManifestInstance } from '@embsim/protocol';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';

/** Leaf versions belong to their sheet definition, not a display-only override. */
export function VersionRequest({item, loaded, disabled}: {item: ManifestInstance; loaded: string; disabled: boolean}) {
  const [value, setValue] = useState(item.requested_version ?? ''), [pending, setPending] = useState(false), [error, setError] = useState('');
  const busy = useProject((s) => s.busy);
  const apply = async () => {
    if (disabled || busy || pending) return;
    setPending(true); setError('');
    try {
      await useProject.getState().mutate(`Request version for ${item.path}`, (project) => {
        const part = item.kind === 'board' ? project.boards?.find((b) => b.id === item.id) : sheetAt(project, item.sheet).sheet.components.find((c) => c.id === item.id);
        if (!part) throw new Error('Part no longer exists. Refresh the report.');
        const version = value.trim();
        if ((part.version ?? '') === version) return null;
        if (version) part.version = version;
        else delete part.version;
        return project;
      });
    } catch (e) { setError(errorMessage(e)); }
    finally { setPending(false); }
  };
  return <form aria-label={`Version request for ${item.path}`} className="wb-version-request" onSubmit={(e) => {e.preventDefault(); void apply();}}>
    <label>Requested version for {item.path}<Input aria-label={`Requested version for ${item.path}`} value={value} onChange={(e) => setValue(e.target.value)} disabled={disabled || busy || pending} placeholder="e.g. ^1.0.0; empty = unpinned" /></label>
    <small>Loaded {loaded || 'unknown'}{item.requested_version ? ` · requested ${item.requested_version}${item.version_satisfied === true ? ' · satisfied' : ' · not verified'}` : ' · no version request'}.</small>
    {item.sheet && <small>Edits this shared sheet definition and every instance using it.</small>}
    <small>Changes validate against the first loaded revision, restart simulation and clear acquisitions. Incompatible requests leave the circuit unchanged.</small>
    <Button size="sm" type="submit" disabled={disabled || busy || pending || value.trim() === (item.requested_version ?? '')}>Apply version request</Button>
    {error && <p role="alert">{error}</p>}
  </form>;
}
