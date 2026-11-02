// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, EmptyState, Input, Slider, Toggle, useToast } from '@embsim/design';
import type { InputEntry } from '@embsim/protocol';
import { SlidersHorizontal } from 'lucide-react';
import { useState } from 'react';
import { errorMessage, inputs as inputActions } from '../../actions';
import { useSession } from '../../stores/session';
import { t as tr } from '../../i18n';

export function ControlsPanel() {
  const inputs = useSession((s) => s.inputs);
  const connected = useSession((s) => s.status === 'open');
  if (inputs.length === 0) {
    return (
      <EmptyState icon={<SlidersHorizontal />} title={tr('controls.none')}>
        {tr('controls.listed')}
      </EmptyState>
    );
  }
  return (
    <div className="wb-controls">
      {inputs.map((i) => (
        <ControlRow key={`${i.target}.${i.input}`} entry={i} disabled={!connected} />
      ))}
    </div>
  );
}

function ControlRow({ entry, disabled }: { entry: InputEntry; disabled: boolean }) {
  const { toast } = useToast();
  const [text, setText] = useState('');
  const fail = (e: unknown) => toast({ kind: 'error', title: tr('fail.input'), description: errorMessage(e) });
  const name = entry.label ?? `${entry.target} ${entry.input}`;
  const set = (v: unknown) => inputActions.set(entry.target, entry.input, v).catch(fail);

  let control: React.ReactNode;
  if (entry.type === 'boolean') {
    const on = Boolean(entry.value);
    control = (
      <div className="wb-control__actions">
        <Button
          size="sm"
          variant="primary"
          pressed={on}
          aria-label={tr('controls.holdOf', { name })}
          title={tr('controls.holdHint')}
          disabled={disabled}
          onPointerDown={(e) => {
            if (e.button !== 0) return;
            e.currentTarget.setPointerCapture(e.pointerId);
            void set(true);
          }}
          onPointerUp={() => void set(false)}
          onPointerCancel={() => void set(false)}
          onKeyDown={(e) => {
            if (e.key === ' ' && !e.repeat) {
              e.preventDefault();
              void set(true);
            }
          }}
          onKeyUp={(e) => {
            if (e.key === ' ') {
              e.preventDefault();
              void set(false);
            }
          }}
          onBlur={() => {
            if (on) void set(false);
          }}
        >
          {tr('controls.hold')}
        </Button>
        <Toggle label={tr('controls.toggleOf', { name })} hideLabel checked={on} disabled={disabled} onCheckedChange={(v) => void set(v)} />
      </div>
    );
  } else if (entry.type === 'number') {
    const min = entry.min ?? 0;
    const max = entry.max ?? 1;
    control = (
      <div style={{ width: 180 }}>
        <Slider label={name} min={min} max={max} step={(max - min) / 100} value={Number(entry.value ?? min)} onValueChange={(v) => void set(v)} disabled={disabled} format={(v) => `${v.toFixed(2)}${entry.unit ? ` ${entry.unit}` : ''}`} />
      </div>
    );
  } else if (entry.type === 'enum' && entry.enum) {
    control = (
      <select className="es-terminal__select" aria-label={name} value={String(entry.value)} disabled={disabled} onChange={(e) => void set(e.target.value)}>
        {entry.enum.map((v) => (
          <option key={v} value={v}>
            {v}
          </option>
        ))}
      </select>
    );
  } else {
    control = (
      <form
        className="es-row"
        onSubmit={(e) => {
          e.preventDefault();
          // a terminal's text is a line typed into it; any other string
          // input (a terminal's hex bytes, a part's text) is set as written
          if (entry.input === 'tx_text' || entry.input === 'tx') void inputActions.serial(entry.target, text + '\n').catch(fail);
          else void set(text);
          setText('');
        }}
      >
        <Input aria-label={tr('controls.textFor', { name })} value={text} onChange={(e) => setText(e.target.value)} mono disabled={disabled} />
        <Button size="sm" type="submit" disabled={disabled || !text}>
          {tr('controls.send')}
        </Button>
      </form>
    );
  }
  return (
    <div className="wb-control">
      <div>
        <div className="wb-control__name">{name}</div>
        <div className="wb-control__type">
          {entry.type}
          {entry.world && (
            <span title={entry.source === 'world' ? tr('controls.fromWorld.hint') : tr('controls.setHere.hint')}>
              {' · '}
              {entry.source === 'world' ? tr('controls.fromWorld', { field: entry.world ?? '' }) : tr('controls.setHere')}
            </span>
          )}
          {entry.type === 'boolean' && (
            <>
              {' · '}
              <span className="wb-control__state" aria-live="polite">
                {entry.value ? tr('controls.pressed') : tr('controls.released')}
              </span>
            </>
          )}
        </div>
      </div>
      {control}
    </div>
  );
}
