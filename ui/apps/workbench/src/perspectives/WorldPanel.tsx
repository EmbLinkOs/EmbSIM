// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The world (ADR-026): the room the circuit sits in. Each field the room
// declares (temperature, humidity, pressure, light, sound, wind) is read by
// every sensor bound to it that the user has not set by hand; a field left
// out leaves each sensor at its own default. Bodies are objects on the bench
// that distance sensors see. The panel edits the project's world, so it is
// saved with the project, undone like any edit, and applied at once to the
// running circuit; what scenarios have changed since shows as "now".

import { Button, EmptyState, IconButton, useToast } from '@embsim/design';
import { WORLD_FIELDS, type Project, type WorldBody, type WorldField, type WorldState } from '@embsim/protocol';
import { Globe2, Plus, Trash2 } from 'lucide-react';
import { useCallback, useEffect, useMemo, useState } from 'react';
import { getClient } from '../connection';
import { errorMessage } from '../actions';
import { useProject } from '../stores/project';
import { useSession } from '../stores/session';

const FORMAT: Record<WorldField, (v: number) => string> = {
  temperature: (v) => `${v.toFixed(1)} °C`,
  humidity: (v) => `${v.toFixed(1)} %RH`,
  pressure: (v) => `${v.toFixed(2)} hPa`,
  illuminance: (v) => `${v >= 1000 ? v.toFixed(0) : v.toFixed(1)} lx`,
  sound: (v) => `${v.toFixed(0)} dB`,
  wind: (v) => `${v.toFixed(1)} m/s`,
};

/** the room's light on a logarithmic slider: 0.1 lx (night) to 100 000 lx (sun) */
const logLux = { to: (lx: number) => Math.log10(Math.max(0.1, lx)), from: (s: number) => Math.round(10 ** s * 10) / 10 };

export function WorldPanel() {
  const project = useProject((s) => s.project);
  const catalog = useProject((s) => s.catalog);
  const editable = useProject((s) => s.editable);
  const now = useSession((s) => s.now);
  const connected = useSession((s) => s.status === 'open');
  const { toast } = useToast();
  const [live, setLive] = useState<WorldState | null>(null);
  const [draft, setDraft] = useState<Partial<Record<WorldField, number>>>({});

  // what the room is now, after the scenario's and the user's inputs
  useEffect(() => {
    if (!connected) return;
    let gone = false;
    const t = setTimeout(() => {
      getClient()
        .call('world.get', {})
        .then((w) => !gone && setLive(w))
        .catch(() => undefined);
    }, 150);
    return () => {
      gone = true;
      clearTimeout(t);
    };
  }, [connected, project, now]);

  const mutate = useCallback(
    (label: string, fn: (p: Project) => Project) => {
      void useProject.getState().mutate(label, fn).catch((e: unknown) => toast({ kind: 'error', title: `${label} failed`, description: errorMessage(e) }));
    },
    [toast],
  );

  const readers = useMemo(() => {
    const out: Partial<Record<WorldField | 'distance', string[]>> = {};
    for (const c of project?.components ?? []) {
      for (const i of catalog[c.component]?.description.inputs ?? []) {
        if (!i.world) continue;
        (out[i.world] ??= []).push(c.id);
      }
    }
    return out;
  }, [project, catalog]);

  if (!project) return <EmptyState icon={<Globe2 />} title="No project">Open a project to set its room.</EmptyState>;
  const fields = project.world?.fields ?? {};
  const bodies = project.world?.bodies ?? [];

  const setField = (name: WorldField, value: number | undefined) =>
    mutate(value === undefined ? `Leave ${name} to the parts` : `Room ${name}`, (p) => {
      const f = { ...(p.world?.fields ?? {}) };
      if (value === undefined) delete f[name];
      else f[name] = value;
      p.world = { ...(p.world ?? {}), fields: f };
      return p;
    });

  const setBodies = (label: string, fn: (b: WorldBody[]) => WorldBody[]) =>
    mutate(label, (p) => {
      p.world = { ...(p.world ?? {}), bodies: fn(structuredClone(p.world?.bodies ?? [])) };
      return p;
    });

  const addBody = (kind: 'box' | 'wall' | 'ball') => {
    let n = 1;
    while (bodies.some((b) => b.id === `${kind}${n}`)) n++;
    const id = `${kind}${n}`;
    // in front of the bench's back edge, where a sensor facing the back sees it
    const body: WorldBody =
      kind === 'wall'
        ? { id, name: `Wall ${n}`, shape: 'box', at: [0, -400, 150], size: [600, 20, 300], color: '#d7d2c8' }
        : kind === 'ball'
          ? { id, name: `Ball ${n}`, shape: 'sphere', at: [0, -200, 40], size: [80, 80, 80], color: '#e74c3c' }
          : { id, name: `Box ${n}`, shape: 'box', at: [0, -200, 50], size: [120, 120, 100], color: '#b08d5b' };
    setBodies(`Add ${body.name}`, (b) => [...b, body]);
  };

  return (
    <div className="wb-world" aria-label="The world">
      <section>
        <h3>The room</h3>
        <p className="wb-world__hint">A field you set is what every sensor bound to it reads, unless you set the sensor itself. Left unset, each part keeps its own value.</p>
        {WORLD_FIELDS.map((f) => {
          const declared = fields[f.name] !== undefined;
          const value = draft[f.name] ?? fields[f.name] ?? f.value;
          const nowValue = live?.fields[f.name];
          const differs = nowValue && nowValue.set && Math.abs(nowValue.value - (fields[f.name] ?? NaN)) > 1e-9;
          const log = f.name === 'illuminance';
          const commit = (v: number) => {
            setDraft((d) => ({ ...d, [f.name]: undefined }));
            setField(f.name, v);
          };
          return (
            <div key={f.name} className="wb-world__field">
              <label className="wb-world__set">
                <input type="checkbox" checked={declared} disabled={!editable} onChange={(e) => setField(f.name, e.target.checked ? value : undefined)} />
                <span>{f.label}</span>
              </label>
              <input
                type="range"
                aria-label={`${f.label}, ${f.unit}`}
                min={log ? -1 : f.min}
                max={log ? 5 : f.max}
                step={log ? 0.01 : f.step}
                value={log ? logLux.to(value) : value}
                disabled={!editable || !declared}
                onChange={(e) => setDraft((d) => ({ ...d, [f.name]: log ? logLux.from(Number(e.target.value)) : Number(e.target.value) }))}
                onPointerUp={() => draft[f.name] !== undefined && commit(draft[f.name]!)}
                onKeyUp={() => draft[f.name] !== undefined && commit(draft[f.name]!)}
              />
              <span className="wb-world__value">{declared ? FORMAT[f.name](value) : 'each part'}</span>
              {differs && <span className="wb-world__now">now {FORMAT[f.name](nowValue.value)}</span>}
              {readers[f.name] && <span className="wb-world__readers">{readers[f.name]!.join(' ')}</span>}
            </div>
          );
        })}
      </section>
      <section>
        <h3>On the bench</h3>
        <p className="wb-world__hint">
          Objects distance sensors see{readers.distance ? ` (${readers.distance.join(', ')})` : ''}. Drag them on the Bench, or set where they are, in mm from the bench's centre: x right, y towards you, z up.
        </p>
        {bodies.map((b, i) => (
          <div key={b.id} className="wb-world__body">
            <span className="wb-world__id">{b.name ?? b.id}</span>
            {(['x', 'y', 'z'] as const).map((axis, k) => (
              <label key={axis} className="wb-world__coord">
                {axis}
                <input
                  type="number"
                  step={5}
                  value={b.at[k]}
                  disabled={!editable}
                  onChange={(e) => {
                    const v = Number(e.target.value);
                    if (!Number.isFinite(v)) return;
                    setBodies(`Move ${b.name ?? b.id}`, (list) => {
                      list[i]!.at[k] = v;
                      return list;
                    });
                  }}
                />
              </label>
            ))}
            <IconButton label={`Remove ${b.name ?? b.id}`} size="sm" variant="ghost" disabled={!editable} onClick={() => setBodies(`Remove ${b.name ?? b.id}`, (list) => list.filter((x) => x.id !== b.id))}>
              <Trash2 />
            </IconButton>
          </div>
        ))}
        <div className="wb-world__add">
          <Button size="sm" variant="ghost" disabled={!editable} onClick={() => addBody('box')}>
            <Plus /> Box
          </Button>
          <Button size="sm" variant="ghost" disabled={!editable} onClick={() => addBody('wall')}>
            <Plus /> Wall
          </Button>
          <Button size="sm" variant="ghost" disabled={!editable} onClick={() => addBody('ball')}>
            <Plus /> Ball
          </Button>
        </div>
      </section>
    </div>
  );
}
