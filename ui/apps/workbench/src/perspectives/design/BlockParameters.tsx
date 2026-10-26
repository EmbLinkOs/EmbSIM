// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { useState } from 'react';
import type { CircuitBlock, ParamDescription, Project } from '@embsim/protocol';
import { removeParameter, saveParameter } from './hierarchyTools';

export function BlockParameters({
  block,
  definition,
  writable,
  mutate,
  fail,
}: {
  block: string;
  definition: CircuitBlock;
  writable: boolean;
  mutate(label: string, fn: (p: Project) => Project): Promise<void>;
  fail(e: unknown): void;
}) {
  const [authoring, setAuthoring] = useState(false);
  const [editing, setEditing] = useState<string | null>(null),
    [name, setName] = useState(''),
    [type, setType] = useState<ParamDescription['type']>('number'),
    [unit, setUnit] = useState(''),
    [minimum, setMinimum] = useState(''),
    [maximum, setMaximum] = useState(''),
    [choices, setChoices] = useState(''),
    [required, setRequired] = useState(true),
    [hasDefault, setHasDefault] = useState(true),
    [value, setValue] = useState(''),
    [doc, setDoc] = useState('');
  const load = (key: string | null) => {
    const s = key ? definition.params?.[key] : undefined;
    setAuthoring(true);
    setEditing(key);
    setName(key ?? '');
    setType(s?.type ?? 'number');
    setUnit(s?.unit ?? '');
    setMinimum(String(s?.min ?? ''));
    setMaximum(String(s?.max ?? ''));
    setChoices(s?.enum?.join('\n') ?? '');
    setRequired(s?.required ?? true);
    setHasDefault(s?.default !== undefined || !key);
    setValue(
      s?.default === undefined
        ? ''
        : typeof s.default === 'object'
          ? JSON.stringify(s.default)
          : String(s.default),
    );
    setDoc(s?.doc ?? '');
  };
  const save = async () => {
    const spec: ParamDescription = {
      type,
      required,
      ...(unit ? { unit } : {}),
      ...(doc ? { doc } : {}),
    };
    if (minimum) spec.min = minimum;
    if (maximum) spec.max = maximum;
    if (type === 'enum')
      spec.enum = choices
        .split('\n')
        .map((s) => s.trim())
        .filter(Boolean);
    if (hasDefault) {
      if (type === 'boolean') {
        if (value !== 'true' && value !== 'false') throw new Error('Boolean default must be true or false');
        spec.default = value === 'true';
      } else spec.default = type === 'table' ? (JSON.parse(value) as [number, number][]) : value;
    }
    await mutate('Edit block parameter declaration', (p) => {
      saveParameter(p, block, editing, name.trim(), spec);
      return p;
    });
    load(null);
    setAuthoring(false);
  };
  return (
    <section className="wb-hierarchy__parameters" aria-label="Block parameter declarations">
      <h3>Block parameters</h3>
      <p className="es-muted">
        Bind an internal value with $name. Parent instances can override the default.
      </p>
      {Object.entries(definition.params ?? {}).map(([key, spec]) => (
        <div className="wb-hierarchy__port" key={key}>
          <strong>{key}</strong>
          <small>
            {spec.type}
            {spec.unit ? ` · ${spec.unit}` : ''} ·{' '}
            {spec.default === undefined ? 'No default' : JSON.stringify(spec.default)}
          </small>
          <div className="es-row">
            <Button size="sm" disabled={!writable} onClick={() => load(key)}>
              Edit {key}
            </Button>
            <Button
              size="sm"
              variant="ghost"
              disabled={!writable}
              onClick={() =>
                void mutate('Remove block parameter', (p) => {
                  removeParameter(p, block, key);
                  return p;
                }).catch(fail)
              }
            >
              Remove {key}
            </Button>
          </div>
        </div>
      ))}
      <Button
        size="sm"
        disabled={!writable}
        onClick={() => {
          load(null);
          setAuthoring(!authoring);
        }}
      >
        {authoring ? 'Hide parameter form' : 'New parameter'}
      </Button>
      {authoring && (
        <form
          onSubmit={(e) => {
            e.preventDefault();
            void save().catch(fail);
          }}
        >
          <label>
            Parameter name
            <input
              aria-label="Declaration name"
              disabled={!writable}
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="resistance"
            />
          </label>
          <label>
            Type
            <select
              aria-label="Declaration type"
              disabled={!writable}
              value={type}
              onChange={(e) => setType(e.target.value as ParamDescription['type'])}
            >
              {(['number', 'integer', 'boolean', 'string', 'enum', 'table'] as const).map((t) => (
                <option key={t}>{t}</option>
              ))}
            </select>
          </label>
          <label>
            Unit
            <input disabled={!writable} value={unit} onChange={(e) => setUnit(e.target.value)} />
          </label>
          {(type === 'number' || type === 'integer') && (
            <div className="wb-hierarchy__bounds">
              <label>
                Minimum
                <input disabled={!writable} value={minimum} onChange={(e) => setMinimum(e.target.value)} />
              </label>
              <label>
                Maximum
                <input disabled={!writable} value={maximum} onChange={(e) => setMaximum(e.target.value)} />
              </label>
            </div>
          )}
          {type === 'enum' && (
            <label>
              Choices, one per line
              <textarea disabled={!writable} value={choices} onChange={(e) => setChoices(e.target.value)} />
            </label>
          )}
          <label className="wb-hierarchy__checkbox">
            <input
              type="checkbox"
              disabled={!writable}
              checked={required}
              onChange={(e) => setRequired(e.target.checked)}
            />
            Required
          </label>
          <label className="wb-hierarchy__checkbox">
            <input
              type="checkbox"
              disabled={!writable}
              checked={hasDefault}
              onChange={(e) => setHasDefault(e.target.checked)}
            />
            Provide a default
          </label>
          {hasDefault && (
            <label>
              Default value
              <input
                aria-label="Declaration default"
                disabled={!writable}
                value={value}
                onChange={(e) => setValue(e.target.value)}
                placeholder={type === 'table' ? '[[0,0],[1,1]]' : '1k'}
              />
            </label>
          )}
          <label>
            Description
            <input disabled={!writable} value={doc} onChange={(e) => setDoc(e.target.value)} />
          </label>
          <div className="es-row">
            <Button size="sm" type="submit" disabled={!writable || !name}>
              {editing ? 'Save declaration' : 'Add declaration'}
            </Button>
            {editing && (
              <Button size="sm" variant="ghost" onClick={() => load(null)}>
                Cancel declaration edit
              </Button>
            )}
          </div>
        </form>
      )}
    </section>
  );
}
