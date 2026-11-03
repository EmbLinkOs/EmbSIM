// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Input, Select, useToast } from '@embsim/design';
import { wireSegments } from '@embsim/scene';
import { useEffect, useState } from 'react';
import { errorMessage } from '../../actions';
import { useProject } from '../../stores/project';
import { t as tr } from '../../i18n';

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
  return <section className="wb-wire-properties wb-inspector__section" aria-label={tr('wire.selected')}>
    <h3>Selected track · {wire.net}</h3>
    <p className="es-muted">{edges.length} segment{edges.length === 1 ? '' : 's'} · {Math.round(length)} sheet units</p>
    <form onSubmit={(e) => {
      e.preventDefault(); if (!valid || disabled) return;
      void store.reshapeWire(index, Number(segment), Number(offset), project).catch((e: unknown) => toast({ kind: 'error', title: tr('fail.reshape'), description: errorMessage(e) }));
    }}>
      <Select label={tr('wire.segment')} disabled={disabled} value={segment} onValueChange={setSegment} options={edges.map(([a, b], i) => ({ value: String(i), label: `${i + 1}: ${a.y === b.y ? 'horizontal' : a.x === b.x ? 'vertical' : 'diagonal'}`, disabled: a.x !== b.x && a.y !== b.y }))} />
      <Input label={tr('wire.offset')} mono hint={tr('wire.offsetHint')} value={offset} disabled={disabled} onChange={(e) => setOffset(e.target.value)} />
      <Button size="sm" type="submit" disabled={disabled || !valid}>{tr('wire.move')}</Button>
    </form>
    <p className="es-muted">{tr('wire.dragHint')}</p>
  </section>;
}
