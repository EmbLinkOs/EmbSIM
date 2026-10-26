// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { parseSi } from '@embsim/protocol';
import { useState } from 'react';
import { useProject } from '../../stores/project';
import { errorMessage } from '../../actions';
import {
  configureInstrument,
  instrumentFingerprint,
  INSTRUMENT_KINDS,
  type InstrumentInstance,
} from './instrumentConnections';

export function InstrumentSettings({
  instrument: i,
  close,
}: {
  instrument: InstrumentInstance;
  close(): void;
}) {
  const store = useProject(),
    kind = INSTRUMENT_KINDS[i.component.component]!;
  const fields = [
    kind.resistance,
    ...(i.component.component === 'emblink/scope_input' ? ['input_capacitance'] : []),
  ];
  const [expected] = useState(() => instrumentFingerprint(i));
  const [positive, setPositive] = useState(i.positive),
    [negative, setNegative] = useState(i.negative);
  const [values, setValues] = useState<Record<string, string>>(() =>
    Object.fromEntries(
      fields.map((key) => [
        key,
        String(i.values[key] ?? (key === 'input_capacitance' ? '15p' : kind.fallback)),
      ]),
    ),
  );
  const [error, setError] = useState('');
  const bound = (key: string) =>
    typeof i.component.params?.[key] === 'string' && String(i.component.params[key]).startsWith('$');
  const r = parseSi(values.input_resistance),
    cap = parseSi(values.input_capacitance);
  const preset =
    r === 1e6 && Math.abs((cap ?? -1) - 100e-12) < 1e-21
      ? '1x'
      : r === 1e7 && Math.abs((cap ?? -1) - 12e-12) < 1e-21
        ? '10x'
        : r === 50 && cap === 0
          ? '50'
          : 'custom';
  const writable = store.editable && !store.busy;
  const apply = async () => {
    try {
      await store.mutate('Configure physical instrument', (p) =>
        configureInstrument(
          p,
          i.path,
          i.component.id,
          {
            positive,
            negative,
            expected,
            values: Object.fromEntries(Object.entries(values).filter(([key]) => !bound(key))),
          },
          store.sceneCatalog,
        ),
      );
      close();
    } catch (e) {
      setError(errorMessage(e));
    }
  };
  return (
    <form
      className="wb-instrument-settings"
      aria-label={`Configure ${i.qualified}`}
      onSubmit={(e) => {
        e.preventDefault();
        void apply();
      }}
    >
      <p>
        Applying changed leads or impedance restarts simulation and clears live/frozen acquisition. Export
        evidence first.
      </p>
      {i.block && (
        <p>
          Shared definition <strong>{i.block}</strong>: changes affect {i.shared} instances. Bound values must
          be edited through parent block parameters.
        </p>
      )}
      {(['positive', 'negative'] as const).map((side) => (
        <label key={side}>
          {side === 'positive' ? 'Positive lead' : 'Negative lead'}
          <select
            aria-label={`${i.qualified} ${side} lead`}
            value={side === 'positive' ? positive : negative}
            disabled={!writable}
            onChange={(e) =>
              side === 'positive' ? setPositive(e.target.value) : setNegative(e.target.value)
            }
          >
            <option value="">Unconnected</option>
            {i.nets.map((net) => (
              <option key={net}>{net}</option>
            ))}
          </select>
        </label>
      ))}
      {i.component.component === 'emblink/scope_input' && (
        <label>
          Scope input preset
          <select
            aria-label={`${i.qualified} input preset`}
            value={preset}
            disabled={!writable || fields.some(bound)}
            onChange={(e) => {
              if (e.target.value === '1x') setValues({ input_resistance: '1meg', input_capacitance: '100p' });
              if (e.target.value === '10x')
                setValues({ input_resistance: '10meg', input_capacitance: '12p' });
              if (e.target.value === '50') setValues({ input_resistance: '50', input_capacitance: '0' });
            }}
          >
            <option value="custom">Custom</option>
            <option value="1x">1× equivalent · 1 MΩ ∥ 100 pF</option>
            <option value="10x">10× equivalent · 10 MΩ ∥ 12 pF</option>
            <option value="50">50 Ω termination</option>
          </select>
          <small>
            Representative effective loading at the circuit terminals; no manufacturer calibration,
            attenuation error or bandwidth qualification.
          </small>
        </label>
      )}
      {fields.map((key) => (
        <label key={key}>
          {key === 'input_capacitance'
            ? 'Input capacitance (F)'
            : `${i.component.component === 'emblink/ammeter' ? 'Shunt' : i.component.component === 'emblink/function_generator' ? 'Output' : 'Input'} resistance (Ω)`}
          <input
            aria-label={`${i.qualified} ${key}`}
            value={bound(key) ? String(i.values[key] ?? '') : (values[key] ?? '')}
            disabled={!writable || bound(key)}
            onChange={(e) => setValues({ ...values, [key]: e.target.value })}
          />
          {bound(key) && <small>Bound to {String(i.component.params?.[key])}</small>}
        </label>
      ))}
      {error && <p role="alert">{error}</p>}
      <div className="es-row">
        <Button size="sm" type="submit" disabled={!writable}>
          Apply instrument settings
        </Button>
        <Button size="sm" variant="ghost" onClick={close}>
          Cancel instrument settings
        </Button>
      </div>
    </form>
  );
}
