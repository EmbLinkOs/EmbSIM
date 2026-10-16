// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, Select, useToast } from '@embsim/design';
import { wireSegments } from '@embsim/scene';
import { useEffect, useState } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';

export function WireProperties({ index }: { index: number }) {
  const store = useProject();
  const project = store.project;
  const wire = project?.layout?.wires?.[index];
  const [segment, setSegment] = useState('0');
  const [offset, setOffset] = useState('0');
  const { toast } = useToast();
  useEffect(() => { setSegment('0'); setOffset('0'); }, [wire]);
  if (!project || !wire) return null;
  const edges = wireSegments(wire);
  const valid = offset.trim() !== '' && Number.isFinite(Number(offset)) && Math.round(Number(offset) / 10) !== 0;
  const disabled = !store.editable || store.busy;
  const length = edges.reduce((sum, [a, b]) => sum + Math.hypot(a.x - b.x, a.y - b.y), 0);
  return <section className="wb-wire-properties wb-inspector__section" aria-label="Selected track">
    <h3>Selected track · {wire.net}</h3>
    <p className="es-muted">{edges.length} segment{edges.length === 1 ? '' : 's'} · {Math.round(length)} sheet units</p>
    <form onSubmit={(e) => {
      e.preventDefault(); if (!valid || disabled) return;
      void store.reshapeWire(index, Number(segment), Number(offset), project).catch((e: unknown) => toast({ kind: 'error', title: 'Reshape failed', description: errorMessage(e) }));
    }}>
      <Select label="Track segment" disabled={disabled} value={segment} onValueChange={setSegment} options={edges.map(([a, b], i) => ({ value: String(i), label: `${i + 1}: ${a.y === b.y ? 'horizontal' : a.x === b.x ? 'vertical' : 'diagonal'}`, disabled: a.x !== b.x && a.y !== b.y }))} />
      <Input label="Segment offset" mono hint="Positive moves right/down; snapped to 10 units. Endpoints and existing junctions stay attached." value={offset} disabled={disabled} onChange={(e) => setOffset(e.target.value)} />
      <Button size="sm" type="submit" disabled={disabled || !valid}>Move track segment</Button>
    </form>
    <p className="es-muted">Or drag the segment handles on the canvas. Reshaping changes drawing geometry only.</p>
  </section>;
}
