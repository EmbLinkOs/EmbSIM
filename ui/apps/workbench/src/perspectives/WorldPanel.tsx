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
import { WORLD_FIELDS, type Project, type WorldBody, type WorldField, type WorldRoom, type WorldState, type WorldSun } from '@embsim/protocol';
import { dayOfYear, ROOM, solarPosition, sunDirection, sunlit, windowOpening } from '@embsim/bench/model';
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
/** the eight ways a window can look */
const FACINGS: [number, string][] = [[0, 'north'], [45, 'north-east'], [90, 'east'], [135, 'south-east'], [180, 'south'], [225, 'south-west'], [270, 'west'], [315, 'north-west']];
const compass = (az: number) => FACINGS[Math.round(((az % 360) + 360) % 360 / 45) % 8]![1];
const clock = (h: number) => `${String(Math.floor(h) % 24).padStart(2, '0')}:${String(Math.round((h % 1) * 60) % 60).padStart(2, '0')}`;
const DEFAULT_SUN: WorldSun = { latitude_deg: 45, date: '06-21', solar_time_h: 10 };

/** what the bench sees of the sun: its place in the sky, and whether it shines on the table */
export function describeSun(sun: WorldSun, room: WorldRoom | undefined): string {
  const pos = solarPosition(sun.latitude_deg ?? 45, dayOfYear(sun.date), sun.solar_time_h ?? 12);
  if (pos.elevation <= 0) return 'Night: the ceiling lamp lights the bench';
  const size = room?.size_mm ? { width: room.size_mm[0], depth: room.size_mm[1], height: room.size_mm[2] } : ROOM;
  const onBench = sunlit(sunDirection(pos, room?.window_azimuth_deg ?? 180), [0, 0, 0], windowOpening(size));
  return `Sun ${pos.elevation.toFixed(0)}° up in the ${compass(pos.azimuth)}: ${onBench ? 'shining on the bench through the window' : 'daylight from the window, no direct sun on the bench'}`;
}

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
  const [hourDraft, setHourDraft] = useState<number | null>(null);

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

  const room = project.world?.room;
  const sun = project.world?.sun;
  const setRoom = (label: string, r: WorldRoom) =>
    mutate(label, (p) => {
      p.world = { ...(p.world ?? {}), room: r };
      return p;
    });
  const setSun = (label: string, v: WorldSun | undefined) =>
    mutate(label, (p) => {
      const w = { ...(p.world ?? {}) };
      if (v) w.sun = v;
      else delete w.sun;
      p.world = w;
      return p;
    });
  const size = room?.size_mm ?? [ROOM.width, ROOM.depth, ROOM.height];
  const sunNow = { ...DEFAULT_SUN, ...sun };

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
      <section aria-label="Room and daylight">
        <h3>Room and daylight</h3>
        <p className="wb-world__hint">How the 3D bench draws the room and lights it. The light sensors read the Light field above, not the sun.</p>
        <div className="wb-world__body">
          <span className="wb-world__id">Size</span>
          {(['width', 'depth', 'height'] as const).map((axis, k) => (
            <label key={axis} className="wb-world__coord">
              {axis[0]}
              <input
                type="number"
                aria-label={`Room ${axis}, m`}
                step={0.1}
                min={k === 2 ? 2.2 : 2.5}
                max={k === 2 ? 10 : 30}
                value={size[k]! / 1000}
                disabled={!editable}
                onChange={(e) => {
                  const v = Math.round(Number(e.target.value) * 1000);
                  if (!Number.isFinite(v) || v < (k === 2 ? 2200 : 2500) || v > (k === 2 ? 10000 : 30000)) return;
                  const next = [...size] as [number, number, number];
                  next[k] = v;
                  setRoom(`Room ${axis}`, { ...room, size_mm: next });
                }}
              />
            </label>
          ))}
          <span className="wb-world__value">m</span>
        </div>
        <div className="wb-world__field">
          <span>Window looks</span>
          <select aria-label="The window looks" value={Math.round((room?.window_azimuth_deg ?? 180) / 45) * 45 % 360} disabled={!editable} onChange={(e) => setRoom('Window direction', { ...room, window_azimuth_deg: Number(e.target.value) })}>
            {FACINGS.map(([az, name]) => <option key={az} value={az}>{name}</option>)}
          </select>
        </div>
        <div className="wb-world__field">
          <label className="wb-world__set">
            <input type="checkbox" checked={Boolean(sun)} disabled={!editable} onChange={(e) => setSun(e.target.checked ? 'Sun by the time of day' : 'Lamp light', e.target.checked ? sunNow : undefined)} />
            <span>Sun</span>
          </label>
          <input
            type="range"
            aria-label="Time of day, local solar time"
            min={0}
            max={24}
            step={0.25}
            value={hourDraft ?? sunNow.solar_time_h}
            disabled={!editable || !sun}
            onChange={(e) => setHourDraft(Number(e.target.value))}
            onPointerUp={() => { if (hourDraft !== null) { setSun('Time of day', { ...sunNow, solar_time_h: hourDraft }); setHourDraft(null); } }}
            onKeyUp={() => { if (hourDraft !== null) { setSun('Time of day', { ...sunNow, solar_time_h: hourDraft }); setHourDraft(null); } }}
          />
          <span className="wb-world__value">{sun ? clock(hourDraft ?? sunNow.solar_time_h ?? 12) : 'lamp'}</span>
        </div>
        {sun && (
          <div className="wb-world__body">
            <label className="wb-world__coord">
              lat
              <input type="number" aria-label="Latitude, degrees" step={0.5} min={-90} max={90} value={sunNow.latitude_deg} disabled={!editable} onChange={(e) => { const v = Number(e.target.value); if (Number.isFinite(v) && Math.abs(v) <= 90) setSun('Latitude', { ...sunNow, latitude_deg: v }); }} />
            </label>
            <label className="wb-world__coord">
              date
              <input type="text" aria-label="Date, MM-DD" pattern="(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])" size={5} defaultValue={sunNow.date} key={sunNow.date} disabled={!editable} onBlur={(e) => { if (/^(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$/.test(e.target.value)) setSun('Date', { ...sunNow, date: e.target.value }); }} />
            </label>
          </div>
        )}
        {sun && <p className="wb-world__hint" data-testid="sun-report">{describeSun(sunNow, room)}</p>}
      </section>
      <section>
        <h3>On the bench</h3>
        <p className="wb-world__hint">
          Objects distance sensors see{readers.distance ? ` (${readers.distance.join(', ')})` : ''}. Drag them on the Bench, or set where they are, in mm from the bench's centre: x right, y towards you, z up. A speed (v) moves a body towards you (negative: away) in virtual time; a travel (↔) sends it back and forth.
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
            <label className="wb-world__coord" title="Speed towards you (y), mm/s: the body moves in virtual time; sensors see it where it is">
              v
              <input
                type="number"
                aria-label={`${b.name ?? b.id} speed, mm/s`}
                step={10}
                value={b.velocity_mm_s?.[1] ?? 0}
                disabled={!editable}
                onChange={(e) => {
                  const v = Number(e.target.value);
                  if (!Number.isFinite(v)) return;
                  setBodies(`Move ${b.name ?? b.id}`, (list) => {
                    const body = list[i]!;
                    const [vx, , vz] = body.velocity_mm_s ?? [0, 0, 0];
                    if (v === 0 && vx === 0 && vz === 0) delete body.velocity_mm_s;
                    else body.velocity_mm_s = [vx, v, vz];
                    return list;
                  });
                }}
              />
            </label>
            <label className="wb-world__coord" title="Back and forth over this distance, mm (0: straight on)">
              ↔
              <input
                type="number"
                aria-label={`${b.name ?? b.id} travel, mm`}
                step={10}
                min={0}
                value={b.travel_mm ?? 0}
                disabled={!editable || !b.velocity_mm_s}
                onChange={(e) => {
                  const v = Number(e.target.value);
                  if (!Number.isFinite(v) || v < 0) return;
                  setBodies(`Travel of ${b.name ?? b.id}`, (list) => {
                    if (v === 0) delete list[i]!.travel_mm;
                    else list[i]!.travel_mm = v;
                    return list;
                  });
                }}
              />
            </label>
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
