// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input } from '@embsim/design';
import { validateJumperStyle, type JumperStyle, type Point3 } from '@embsim/bench/model';
import type { BenchWire } from '@embsim/protocol';
import { useEffect, useRef, useState } from 'react';

/** A draft becomes one undoable edit; typing never reconnects a live circuit. */
export function BenchWireEditor({ wire, editable, midpoint, onDress, onReconnect, onRemove }: {
  wire: BenchWire;
  editable: boolean;
  midpoint: Point3;
  onDress(style: JumperStyle): void;
  onReconnect(from: string, to: string): void;
  onRemove(): void;
}) {
  const section = useRef<HTMLElement>(null);
  useEffect(() => { section.current?.scrollIntoView({ block: 'nearest' }); }, []);
  const [from, setFrom] = useState(wire.from);
  const [to, setTo] = useState(wire.to);
  const [color, setColor] = useState(wire.color ?? '#2e86de');
  const [rise, setRise] = useState(wire.rise_mm?.toString() ?? '');
  const [points, setPoints] = useState<string[][]>((wire.points ?? []).map((p) => p.map(String)));
  const style: JumperStyle = { color, rise_mm: rise.trim() ? Number(rise) : undefined, points: points.length ? points.map((p) => p.map((v) => v.trim() ? Number(v) : NaN) as Point3) : undefined };
  let error = '';
  try { validateJumperStyle(style); } catch (e) { error = (e as Error).message; }
  return <section ref={section} className="wb-bench-list__form wb-wire-editor" aria-label="Selected jumper">
    <h3>Selected jumper</h3>
    <form onSubmit={(e) => { e.preventDefault(); if (editable && !error) onDress(style); }}>
      <Input label="Wire colour" type="color" value={color} onChange={(e) => setColor(e.target.value)} disabled={!editable} />
      <Input label="Route rise (mm, blank = automatic)" type="number" min={0} max={1000} step="any" value={rise} onChange={(e) => setRise(e.target.value)} disabled={!editable || points.length > 0} />
      <p>Route points: X, height, Y in millimetres. Ends stay attached to their terminals. Crossings do not connect.</p>
      {points.map((point, index) => <div className="wb-wire-point" key={index}>
        {['X', 'Height', 'Y'].map((axis, k) => <Input key={axis} label={`Point ${index + 1} ${axis}`} type="number" step="any" min={k === 1 ? 0 : undefined} value={point[k]} disabled={!editable} onChange={(e) => setPoints((old) => old.map((p, i) => i === index ? p.map((v, j) => j === k ? e.target.value : v) : p))} />)}
        <Button size="sm" variant="ghost" disabled={!editable} aria-label={`Remove point ${index + 1}`} onClick={() => setPoints((old) => old.filter((_, i) => i !== index))}>×</Button>
      </div>)}
      <div className="wb-bench-list__row">
        <Button size="sm" variant="ghost" disabled={!editable || points.length >= 64} onClick={() => setPoints((old) => [...old, midpoint.map((n) => String(Math.round(n * 100) / 100))])}>Add route point</Button>
        <Button size="sm" variant="ghost" disabled={!editable || !points.length} onClick={() => { setPoints([]); setRise(''); }}>Automatic route</Button>
      </div>
      {error && <p role="alert">{error}</p>}
      <Button size="sm" type="submit" disabled={!editable || Boolean(error)}>Apply appearance</Button>
    </form>
    <form onSubmit={(e) => { e.preventDefault(); if (editable && from.trim() && to.trim() && from.trim() !== to.trim()) onReconnect(from.trim(), to.trim()); }}>
      <Input label="Reconnect from" value={from} onChange={(e) => setFrom(e.target.value)} disabled={!editable} />
      <Input label="Reconnect to" value={to} onChange={(e) => setTo(e.target.value)} disabled={!editable} />
      <p>Reconnecting changes the circuit and restarts simulation. Appearance edits keep the clock and measurements.</p>
      <Button size="sm" type="submit" disabled={!editable || !from.trim() || !to.trim() || from.trim() === to.trim() || (from === wire.from && to === wire.to)}>Reconnect jumper</Button>
    </form>
    <Button size="sm" variant="ghost" disabled={!editable} onClick={onRemove}>Remove selected jumper</Button>
  </section>;
}
