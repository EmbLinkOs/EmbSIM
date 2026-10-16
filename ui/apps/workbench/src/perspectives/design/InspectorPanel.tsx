// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, EmptyState, Input, Select, SignalChip, Toggle, useToast } from '@embsim/design';
import { parseSi, type ParamDescription } from '@embsim/protocol';
import { describeNetState } from '@embsim/scene';
import { Layers, MousePointer2 } from 'lucide-react';
import { useEffect, useState } from 'react';
import { errorMessage } from '../../actions';
import { insideOf, openInside } from '../../stores/openInside';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useSession } from '../../stores/session';
import { FidelityBadges } from './LibraryPanel';

const UNIT_LABEL: Record<string, string> = { ohm: 'Ω', F: 'F', V: 'V', A: 'A', ms: 'ms', W: 'W', '%': '%' };

export function InspectorPanel() {
  const selection = useSelection((s) => s.selection);
  const net = useSelection((s) => s.net);
  const project = useProject((s) => s.project);
  const wires = project?.layout?.wires ?? [];
  const id = selection.components[0] ?? null;
  const wireNet = selection.wires.length ? wires[selection.wires[0]!]?.net ?? null : null;
  const netName = net ?? wireNet;
  if (id && project?.boards?.some((b) => b.id === id)) return <BoardInspector id={id} />;
  if (id) return <ComponentInspector id={id} />;
  if (netName) return <NetInspector net={netName} />;
  return (
    <EmptyState icon={<MousePointer2 />} title="Nothing selected">
      Select a component or a wire on the schematic, or a net in the nets list.
    </EmptyState>
  );
}

/** Opens a board's or a composite part's inside on the schematic. */
function OpenInsideButton({ id }: { id: string }) {
  const { toast } = useToast();
  if (!insideOf(id)) return null;
  return (
    <Button size="sm" variant="ghost" icon={<Layers />} onClick={() => void openInside(id).catch((e: unknown) => toast({ kind: 'error', title: 'Open failed', description: errorMessage(e) }))}>
      Open inside
    </Button>
  );
}

function BoardInspector({ id }: { id: string }) {
  const board = useProject((s) => s.project?.boards?.find((b) => b.id === id));
  const entry = useProject((s) => (board ? s.catalog[board.board] : undefined));
  if (!board) return <EmptyState title={`${id} no longer exists`} />;
  const desc = entry?.description as { name?: string; description?: string; version?: string; mcu?: { ref?: string } } | undefined;
  return (
    <div className="wb-inspector">
      <div className="wb-inspector__head">
        <span className="wb-inspector__id">{id}</span>
        <span>{desc?.name ?? board.board}</span>
      </div>
      <div className="es-muted" style={{ fontSize: 'var(--text-xs)' }}>
        <span className="es-mono">{board.board}</span>
        {desc?.version && ` v${desc.version}`}
        {desc?.description && ` — ${desc.description}`}
      </div>
      <section className="wb-inspector__section" aria-label="Board">
        <dl className="wb-inspector__facts">
          <dt>MCU</dt>
          <dd className="es-mono">{desc?.mcu?.ref ?? '—'}</dd>
          <dt>Firmware</dt>
          <dd className="es-mono">{board.firmware?.image ?? 'none'}</dd>
          {board.firmware?.toolchain && (
            <>
              <dt>Toolchain</dt>
              <dd className="es-mono">{board.firmware.toolchain}</dd>
            </>
          )}
        </dl>
        <OpenInsideButton id={id} />
      </section>
    </div>
  );
}

function ComponentInspector({ id }: { id: string }) {
  const component = useProject((s) => s.project?.components.find((c) => c.id === id));
  const entry = useProject((s) => (component ? s.catalog[component.component] : undefined));
  const nets = useProject((s) => s.project?.nets);
  const setParam = useProject((s) => s.setParam);
  const outputs = useSession((s) => s.outputs);
  const { toast } = useToast();
  if (!component) return <EmptyState title={`${id} no longer exists`} />;
  const desc = entry?.description;
  const pinNet = (pin: string) => Object.entries(nets ?? {}).find(([, pins]) => pins.includes(`${id}.${pin}`))?.[0];
  const tiers = desc ? (['functional', 'timed', 'electrical'] as const).filter((t) => desc.fidelity[t]) : [];
  const save = (param: string, value: unknown) => {
    setParam(id, param, value).catch((e: unknown) => toast({ kind: 'error', title: `Could not set ${param}`, description: errorMessage(e) }));
  };
  return (
    <div className="wb-inspector">
      <div className="wb-inspector__head">
        <span className="wb-inspector__id">{id}</span>
        <span>{desc?.name ?? component.component}</span>
        {desc && <FidelityBadges tiers={tiers} />}
      </div>
      <div className="es-muted" style={{ fontSize: 'var(--text-xs)' }}>
        <span className="es-mono">{component.component}</span>
        {desc && ` v${desc.version}`}
        {desc?.description && ` — ${desc.description}`}
      </div>
      <OpenInsideButton id={id} />

      <section className="wb-inspector__section" aria-label="Parameters">
        <h3>Parameters</h3>
        {desc?.params && Object.keys(desc.params).length ? (
          <div className="wb-inspector__grid">
            {Object.entries(desc.params).map(([name, p]) => (
              <ParamField key={name} name={name} param={p} value={component.params?.[name]} onCommit={(v) => save(name, v)} />
            ))}
          </div>
        ) : (
          <span className="es-muted">No parameters</span>
        )}
      </section>

      <section className="wb-inspector__section" aria-label="Pins">
        <h3>Pins</h3>
        <table className="es-table wb-inspector__pins">
          <thead>
            <tr>
              <th scope="col">Pin</th>
              <th scope="col">No.</th>
              <th scope="col">Model</th>
              <th scope="col">Net</th>
            </tr>
          </thead>
          <tbody>
            {(desc?.pins ?? []).map((pin) => (
              <tr key={pin.name}>
                <td className="es-mono">{pin.name}</td>
                <td className="es-mono">{pin.number ?? '—'}</td>
                <td>{pin.model?.kind ?? '—'}</td>
                <td className="es-mono">{pinNet(pin.name) ?? <span className="es-muted">unconnected</span>}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </section>

      {desc?.outputs?.length ? (
        <section className="wb-inspector__section" aria-label="Outputs">
          <h3>Outputs</h3>
          <dl className="wb-kv">
            {desc.outputs.map((o) => (
              <div key={o.name} style={{ display: 'contents' }}>
                <dt>{o.name}</dt>
                <dd>{String(outputs[`${id}.${o.name}`] ?? '—')}</dd>
              </div>
            ))}
          </dl>
        </section>
      ) : null}

      {desc && (
        <section className="wb-inspector__section" aria-label="Fidelity">
          <h3>Fidelity</h3>
          {(['functional', 'timed', 'electrical'] as const).map((tier) => {
            const f = desc.fidelity[tier];
            return (
              <div key={tier} className="wb-fidelity-card">
                <div className="wb-fidelity-card__title">
                  <span>{tier}</span>
                  <Badge tone={f ? 'accent' : 'default'}>{f ? 'provided' : 'not provided'}</Badge>
                </div>
                {f && <div>{f.claims}</div>}
                {f?.omissions?.length ? <div className="wb-fidelity-card__omissions">Omits: {f.omissions.join(', ')}</div> : null}
                {f?.range && <div className="wb-fidelity-card__omissions">Range: {Object.entries(f.range).map(([k, [lo, hi]]) => `${k} ${lo}…${hi}`).join('; ')}</div>}
              </div>
            );
          })}
        </section>
      )}

      {desc && (
        <section className="wb-inspector__section" aria-label="Provenance">
          <h3>Provenance</h3>
          <dl className="wb-kv">
            <dt>Author</dt>
            <dd>{desc.provenance.author ?? '—'}</dd>
            <dt>License</dt>
            <dd>{desc.provenance.license ?? '—'}</dd>
            <dt>Sources</dt>
            <dd>
              {desc.provenance.sources?.length
                ? desc.provenance.sources.map((s, i) => (
                    <div key={i}>
                      {s.url ? (
                        <a href={s.url} target="_blank" rel="noreferrer">
                          {s.title}
                        </a>
                      ) : (
                        s.title
                      )}
                    </div>
                  ))
                : 'none'}
            </dd>
          </dl>
        </section>
      )}
    </div>
  );
}

function ParamField({ name, param, value, onCommit }: { name: string; param: ParamDescription; value: unknown; onCommit: (v: unknown) => void }) {
  const current = value ?? param.default;
  const [text, setText] = useState(String(current ?? ''));
  useEffect(() => setText(String(current ?? '')), [current]);
  const label = `${name}${param.advanced ? ' (advanced)' : ''}`;
  if (param.type === 'boolean') {
    return <Toggle label={label} checked={Boolean(current)} onCheckedChange={(v) => onCommit(v)} />;
  }
  if (param.type === 'enum' && param.enum) {
    return (
      <div className="es-field">
        <span className="es-field__label">{label}</span>
        <Select label={label} value={String(current)} options={param.enum.map((e) => ({ value: e, label: e }))} onValueChange={(v) => onCommit(v)} />
      </div>
    );
  }
  const unit = param.unit ? UNIT_LABEL[param.unit] ?? param.unit : undefined;
  const min = parseSi(param.min);
  const max = parseSi(param.max);
  const outOfRange = (n: number) => (min !== undefined && n < min) || (max !== undefined && n > max);
  const commit = () => {
    if (text === String(current ?? '')) return;
    if (param.type === 'number' || param.type === 'integer') {
      const parsed = parseSi(text);
      if (parsed === undefined || outOfRange(parsed)) return;
      // SI suffixes ("4.7k") are accepted by the loader (SPEC-005 §4); send them as written.
      const n = Number(text);
      onCommit(Number.isFinite(n) && text.trim() !== '' ? n : text.trim());
    } else onCommit(text);
  };
  const invalid = (() => {
    if (param.type !== 'number' && param.type !== 'integer') return false;
    if (text.trim() === '') return true;
    const parsed = parseSi(text);
    return parsed === undefined || outOfRange(parsed);
  })();
  return (
    <Input
      label={label}
      unit={unit}
      mono
      value={text}
      title={param.doc}
      onChange={(e) => setText(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === 'Enter') commit();
      }}
      error={invalid ? `Enter a number${min !== undefined || max !== undefined ? ` between ${param.min ?? '−∞'} and ${param.max ?? '∞'}` : ''}` : undefined}
      hint={param.doc}
    />
  );
}

function NetInspector({ net }: { net: string }) {
  const pins = useProject((s) => s.project?.nets[net]);
  const topology = useProject((s) => s.topology);
  const state = useSession((s) => s.nets[net]);
  const renameNet = useProject((s) => s.renameNet);
  const selectNet = useSelection((s) => s.selectNet);
  const { toast } = useToast();
  const [name, setName] = useState(net);
  useEffect(() => setName(net), [net]);
  const island = topology?.nets.find((n) => n.name === net)?.island;
  const kind = topology?.islands.find((i) => i.id === island)?.kind;
  const vis = describeNetState(state, [0, 3.3]);
  const rename = () => {
    if (name === net) return;
    renameNet(net, name)
      .then(() => selectNet(name))
      .catch((e: unknown) => toast({ kind: 'error', title: 'Rename failed', description: errorMessage(e) }));
  };
  return (
    <div className="wb-inspector">
      <div className="wb-inspector__head">
        <span className="wb-inspector__id">{net}</span>
        <Badge>net</Badge>
      </div>
      <section className="wb-inspector__section" aria-label="Net">
        <Input
          label="Name"
          mono
          value={name}
          onChange={(e) => setName(e.target.value)}
          onBlur={rename}
          onKeyDown={(e) => {
            if (e.key === 'Enter') rename();
          }}
        />
        <dl className="wb-kv">
          <dt>Island</dt>
          <dd>{island ? `${island} (${kind})` : '—'}</dd>
          <dt>State</dt>
          <dd>
            <SignalChip kind={vis.kind} label={vis.text} />
          </dd>
          <dt>Pins</dt>
          <dd>{pins?.length ? pins.join(', ') : 'none'}</dd>
        </dl>
        <Button size="sm" variant="ghost" onClick={() => selectNet(null)}>
          Close
        </Button>
      </section>
    </div>
  );
}
