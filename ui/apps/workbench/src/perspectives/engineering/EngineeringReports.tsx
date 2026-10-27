// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, Tabs } from '@embsim/design';
import {
  assemblyCsv,
  assemblyLines,
  stableJson,
  formatSi,
  formatTime,
  verifyManifest,
  type ComponentDescription,
  type EngineeringManifest,
  type EngineeringManifestData,
  type Project,
} from '@embsim/protocol';
import { useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useHierarchy } from '../../stores/hierarchy';
import { useLayout } from '../../stores/layout';
import { useSession } from '../../stores/session';
import { useProject } from '../../stores/project';
import { LibraryLockPanel } from './LibraryLockPanel';

export function EngineeringReports() {
  const currentProject = useProject((s) => s.project),
    policy = useSession((s) => s.policy);
  const available = useProject((s) => Boolean(s.project) && !s.busy);
  const [open, setOpen] = useState(false),
    [pending, setPending] = useState(false),
    [error, setError] = useState('');
  const [result, setResult] = useState<{
    envelope: EngineeringManifest;
    data: EngineeringManifestData;
  } | null>(null);
  const [tab, setTab] = useState<'assembly' | 'models'>('assembly');
  const [query, setQuery] = useState(''),
    [fixtures, setFixtures] = useState(false);
  const request = useRef(0);
  const [exporting, setExporting] = useState(false);
  const load = async () => {
    const token = ++request.current;
    setPending(true);
    setError('');
    try {
      const envelope = await getClient().project.manifest({});
      const data = await verifyManifest(envelope);
      if (request.current === token) setResult({ envelope, data });
    } catch (e) {
      if (request.current === token) setError(errorMessage(e));
    } finally {
      if (request.current === token) setPending(false);
    }
  };
  const close = () => {
    setOpen(false);
    request.current++;
    setPending(false);
  };
  const lines = useMemo(() => (result ? assemblyLines(result.data) : []), [result]);
  const descriptions = useMemo(
    () =>
      result
        ? Object.fromEntries(
            Object.entries(result.data.catalog).map(([key, doc]) => [
              key,
              JSON.parse(doc.json) as ComponentDescription,
            ]),
          )
        : {},
    [result],
  );
  const stale = useMemo(
    () =>
      Boolean(
        result &&
          (!currentProject ||
            policy !== result.data.policy ||
            stableJson(currentProject) !== stableJson(JSON.parse(result.data.project.json))),
      ),
    [currentProject, policy, result],
  );
  const project = result ? (JSON.parse(result.data.project.json) as Project) : null;
  const shown = lines.filter(
    (line) =>
      (fixtures || !line.simulationFixture) &&
      `${line.reference} ${line.name} ${line.mpn} ${line.manufacturer} ${line.paths.join(' ')} ${JSON.stringify(line.params)}`
        .toLowerCase()
        .includes(query.toLowerCase()),
  );
  const download = async (kind: 'bom' | 'manifest') => {
    if (!result || pending || stale || exporting) return;
    setExporting(true);
    try {
      // Compare the native text, rather than a browser-rounded integer preview.
      const fresh = await verifyManifest(await getClient().project.manifest({}));
      if (fresh.project.json !== result.data.project.json || fresh.policy !== result.data.policy)
        throw new Error('Project changed since this snapshot. Refresh before exporting.');
      await (
        await platform()
      ).saveTextFile(
        `${project?.name ?? 'project'}-${kind === 'bom' ? 'bom.csv' : 'models.manifest.json'}`,
        kind === 'bom' ? assemblyCsv(lines, fixtures) : JSON.stringify(result.envelope, null, 2),
        kind === 'bom' ? 'text/csv' : 'application/json',
      );
    } catch (e) {
      setError(errorMessage(e));
    } finally {
      setExporting(false);
    }
  };
  const config = (params: Record<string, unknown>, desc?: ComponentDescription) => {
    const primary = [
      'resistance',
      'capacitance',
      'inductance',
      'input_resistance',
      'input_capacitance',
      'shunt_resistance',
      'output_resistance',
      'gain',
      'gbw',
      'slew',
      'rout',
      'part',
      'color',
      'kind',
      'dc',
      'v1',
      'v2',
      'freq',
    ];
    const keys = primary.filter((key) => desc?.params?.[key] && key in params).slice(0, 4);
    return (
      keys
        .map((key) => {
          const value = params[key],
            unit = desc?.params?.[key]?.unit ?? '';
          return `${key}: ${typeof value === 'number' ? formatSi(value, unit === 'ohm' ? 'Ω' : unit) : String(value)}`;
        })
        .join(' · ') || 'See resolved configuration'
    );
  };
  return (
    <>
      <Button
        size="sm"
        variant="ghost"
        disabled={!available}
        onClick={() => {
          setOpen(true);
          void load();
        }}
      >
        Engineering reports
      </Button>
      <Dialog
        open={open}
        onOpenChange={(value) => (value ? setOpen(true) : close())}
        title="Engineering reports"
        description="Configured assembly inventory and loaded model/source identities from one native project snapshot."
        className="wb-engineering-reports"
        footer={
          <>
            <Button size="sm" variant="ghost" disabled={pending} onClick={() => void load()}>
              Refresh report
            </Button>
            <Button
              size="sm"
              disabled={!result || pending || exporting || stale || Boolean(error)}
              onClick={() => void download('bom')}
            >
              Export BOM CSV
            </Button>
            <Button
              size="sm"
              disabled={!result || pending || exporting || stale || Boolean(error)}
              onClick={() => void download('manifest')}
            >
              Export model manifest
            </Button>
          </>
        }
      >
        {result?.data.source === 'native' && <LibraryLockPanel />}
        {result?.data.source === 'demo' && <p>Library locking requires a native session with filesystem access.</p>}
        {pending && <p role="status">Reading native inventory and verifying document identities…</p>}
        {stale && (
          <p role="status">
            Project changed since this snapshot. Refresh before exporting or locating parts.
          </p>
        )}
        {error && <p role="alert">{error}</p>}
        {result && !pending && (
          <>
            <div className="wb-engineering-reports__summary">
              <strong>{project?.name}</strong>
              <Badge tone={result.data.source === 'native' ? 'accent' : 'warning'}>
                {result.data.source === 'native'
                  ? 'Native snapshot · SHA-256 verified'
                  : 'Demo · unverified parameters'}
              </Badge>
              <span>
                {result.data.instances.length} instances · {Object.keys(result.data.catalog).length}{' '}
                descriptions · {formatTime(BigInt(result.data.now_ps))} · {result.data.policy}
              </span>
            </div>
            <p className="es-muted">
              Project blocks expand into concrete parts; catalog ICs and boards count as whole
              packages/assemblies. Model configuration groups remain separate. An unassigned MPN needs a
              procurement decision.
            </p>
            <Tabs
              value={tab}
              onValueChange={setTab}
              label="Engineering report views"
              items={[
                {
                  value: 'assembly',
                  label: 'Assembly',
                  content: (
                    <>
                      <div className="wb-engineering-reports__filters">
                        <label>
                          Find assembly item
                          <input
                            aria-label="Find assembly item"
                            value={query}
                            onChange={(e) => setQuery(e.target.value)}
                            placeholder="Reference, MPN, value…"
                          />
                        </label>
                        <label className="wb-engineering-reports__checkbox">
                          <input
                            type="checkbox"
                            checked={fixtures}
                            onChange={(e) => setFixtures(e.target.checked)}
                          />
                          Include simulation fixtures
                        </label>
                      </div>
                      <p>
                        {shown.reduce((n, line) => n + line.paths.length, 0)} items in {shown.length}{' '}
                        configuration groups. CSV exports all {fixtures ? 'inventory' : 'assembly'} groups,
                        independent of this search.
                      </p>
                      <div className="wb-engineering-reports__table">
                        <table aria-label="Assembly inventory">
                          <thead>
                            <tr>
                              <th>Qty</th>
                              <th>References</th>
                              <th>Part</th>
                              <th>Model configuration</th>
                              <th>Assignment</th>
                            </tr>
                          </thead>
                          <tbody>
                            {shown.slice(0, 200).map((line, index) => (
                              <tr key={index}>
                                <td>{line.paths.length}</td>
                                <td>
                                  <details>
                                    <summary>
                                      {line.paths.slice(0, 3).join(', ')}
                                      {line.paths.length > 3 ? ` +${line.paths.length - 3}` : ''}
                                    </summary>
                                    {line.instances.slice(0, 100).map((i) => (
                                      <button
                                        key={i.path}
                                        disabled={stale}
                                        onClick={() => {
                                          close();
                                          useHierarchy.getState().show(i.sheet, i.id);
                                          useLayout.getState().setPerspective('design');
                                        }}
                                      >
                                        Locate {i.path}
                                      </button>
                                    ))}
                                    {line.paths.length > 100 && (
                                      <small>All references are included in exports.</small>
                                    )}
                                  </details>
                                </td>
                                <td>
                                  <strong>{line.mpn || line.name}</strong>
                                  <small>
                                    {line.reference} · {line.version}
                                  </small>
                                  <small>
                                    {line.manufacturer} {line.package}
                                  </small>
                                </td>
                                <td>
                                  {line.unsafeIntegerPreview
                                    ? 'Use native manifest for exact values'
                                    : config(line.params, descriptions[line.catalog])}
                                  <details>
                                    <summary>Resolved configuration</summary>
                                    <pre>
                                      {line.unsafeIntegerPreview
                                        ? 'Native JSON text preserves integer digits beyond this browser preview.'
                                        : JSON.stringify(line.params, null, 2)}
                                    </pre>
                                  </details>
                                </td>
                                <td>
                                  {line.simulationFixture
                                    ? 'Simulation fixture'
                                    : line.mpn
                                      ? 'Named part'
                                      : 'MPN unassigned'}
                                  {line.notes.map((note) => (
                                    <small key={note}>{note}</small>
                                  ))}
                                </td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </div>
                      {shown.length > 200 && (
                        <p>
                          Showing 200 of {shown.length} groups. Refine the search; exports include every
                          group.
                        </p>
                      )}
                    </>
                  ),
                },
                {
                  value: 'models',
                  label: 'Models and sources',
                  content: (
                    <>
                      <p>
                        Loaded description digests include compiled model dependencies, separately from
                        package counts. Listed source hashes are declarations; this export does not download
                        or validate external datasheets.
                      </p>
                      <dl className="wb-engineering-reports__identity">
                        <dt>Project document</dt>
                        <dd>{result.data.project.sha256 || 'Demo only'}</dd>
                        <dt>Manifest</dt>
                        <dd>{result.envelope.sha256 || 'Demo only'}</dd>
                        <dt>Engine / ABI</dt>
                        <dd>
                          {result.data.engine} / {result.data.abi}
                        </dd>
                      </dl>
                      {Object.entries(descriptions).map(([key, desc]) => (
                        <details key={key}>
                          <summary>
                            {desc.name} · {desc.version} · {key}
                          </summary>
                          <code>{result.data.catalog[key]?.sha256 || 'Demo · no native identity'}</code>
                          <p>
                            {desc.fidelity?.[result.data.policy]?.claims ??
                              'No declared scope for this policy'}
                          </p>
                          <ul>
                            {desc.fidelity?.[result.data.policy]?.omissions?.map((omission) => (
                              <li key={omission}>{omission}</li>
                            ))}
                          </ul>
                          <p>License: {desc.provenance?.license ?? 'Unspecified'}</p>
                          {desc.provenance?.sources?.map((source, i) => (
                            <p key={i}>
                              {source.url?.startsWith('https://') ? (
                                <a href={source.url} target="_blank" rel="noreferrer">
                                  {source.title}
                                </a>
                              ) : (
                                source.title
                              )}{' '}
                              {source.rev}
                              <small>
                                {source.hash ?? 'No source digest declared'} · {source.notes}
                              </small>
                            </p>
                          ))}
                        </details>
                      ))}
                      <details>
                        <summary>Compiled model instances ({result.data.models.length})</summary>
                        <small>
                          Browser previews are approximate for very large integers. The native manifest
                          preserves the full JSON text.
                        </small>
                        {result.data.models.slice(0, 200).map((model) => (
                          <p key={model.path}>
                            <strong>{model.path}</strong> · {model.reference} · {model.model}
                            <small>
                              {model.catalog || 'No attached description'} · {JSON.stringify(model.params)}
                            </small>
                          </p>
                        ))}
                        {result.data.models.length > 200 && (
                          <p>Every compiled instance is included in the manifest export.</p>
                        )}
                      </details>
                      <h3>Loaded libraries</h3>
                      {result.data.libraries.map((library) => (
                        <p key={library.path}>
                          {library.name} · {library.version} · {library.items} descriptions
                          <small>{library.path}</small>
                        </p>
                      ))}
                    </>
                  ),
                },
              ]}
            />
            <p className="es-muted">
              This is an audit snapshot, not an assembly variant, dependency lock, PCB export or complete
              reproducible package. Firmware, Lua/model files and external resources are not embedded. A
              content hash does not qualify component accuracy.
            </p>
          </>
        )}
      </Dialog>
    </>
  );
}
