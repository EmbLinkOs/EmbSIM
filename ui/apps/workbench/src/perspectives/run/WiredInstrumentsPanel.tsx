// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { Meter } from '@embsim/instruments';
import { formatSi, parseSi } from '@embsim/protocol';
import { useMemo, useState } from 'react';
import { InstrumentSettings } from './InstrumentSettings';
import { INSTRUMENT_KINDS, instrumentInstances, type InstrumentInstance } from './instrumentConnections';
export { isWiredInstrument } from './instrumentConnections';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';

/** Readouts are engine observations. Merely adding a trace never inserts a load;
 * the visible terminals belong to components wired into the project. */
export function WiredInstrumentsPanel({
  onLocate,
  onTrace,
}: {
  onLocate(id: string, path?: string): void;
  onTrace(target: string, rate: number): void;
}) {
  const project = useProject((s) => s.project),
    hierarchy = useProject((s) => s.hierarchy);
  const instruments = useMemo(() => instrumentInstances(project, hierarchy), [project, hierarchy]);
  return (
    <div className="wb-wired-instruments" aria-label="Wired instruments">
      <p className="es-muted">
        Wired instruments load the circuit. Traces observe actual terminal voltage/current without adding
        another load.
      </p>
      {!instruments.length && (
        <p className="es-muted">
          Place a voltmeter, ammeter, scope input or function generator from Instruments, then wire both
          leads.
        </p>
      )}
      {instruments.map((i) => (
        <InstrumentCard key={i.qualified} instrument={i} onLocate={onLocate} onTrace={onTrace} />
      ))}
    </div>
  );
}

function InstrumentCard({
  instrument: i,
  onLocate,
  onTrace,
}: {
  instrument: InstrumentInstance;
  onLocate(id: string, path?: string): void;
  onTrace(target: string, rate: number): void;
}) {
  const outputs = useSession((s) => s.outputs),
    writable = useProject((s) => s.editable && !s.busy);
  const [editing, setEditing] = useState(false),
    [rate, setRate] = useState('200000'),
    [error, setError] = useState('');
  const c = i.component,
    kind = INSTRUMENT_KINDS[c.component]!,
    ammeter = c.component === 'emblink/ammeter';
  const r = parseSi(i.values[kind.resistance] as string | number | undefined) ?? kind.fallback;
  const cap = parseSi(i.values.input_capacitance as string | number | undefined) ?? 15e-12;
  const connected = Boolean(i.positive && i.negative);
  const trace = (quantity: string) => {
    const hz = Number(rate);
    if (!Number.isInteger(hz) || hz < 1 || hz > 10000000) {
      setError('Use a sample rate from 1 to 10,000,000 Hz');
      return;
    }
    setError('');
    onTrace(`device:${i.qualified}.${quantity}`, hz);
  };
  return (
    <section className="wb-wired-instrument" aria-label={`${i.qualified} ${kind.title}`}>
      <header>
        <strong>{i.qualified}</strong>
        <span>{kind.title}</span>
      </header>
      <p className="wb-wired-instrument__impedance">
        {ammeter ? 'Shunt' : c.component === 'emblink/function_generator' ? 'Output' : 'Input'}{' '}
        {formatSi(r, 'Ω')}
        {c.component === 'emblink/scope_input' ? ` ∥ ${formatSi(cap, 'F')}` : ''}
      </p>
      <p className="wb-wired-instrument__leads">
        <span>+ {i.positive || 'unconnected'}</span>
        <span>− {i.negative || 'unconnected'}</span>
      </p>
      {i.path && (
        <small>
          Sheet {i.path} · shared {i.block} definition
        </small>
      )}
      {connected ? (
        <div className="wb-wired-instrument__readings">
          <Meter
            label={ammeter ? 'Burden voltage' : 'Terminal voltage'}
            value={outputs[`${i.qualified}.voltage`]}
            unit="V"
          />
          <Meter
            label={c.component === 'emblink/function_generator' ? 'Current into output' : 'Current into +'}
            value={outputs[`${i.qualified}.${kind.current}`]}
            unit="A"
          />
        </div>
      ) : (
        <p role="status">Wire both leads to include this instrument in the solve.</p>
      )}
      <label className="wb-instrument-rate">
        Trace sample rate (Hz)
        <input
          aria-label={`${i.qualified} sample rate`}
          value={rate}
          onChange={(e) => setRate(e.target.value)}
          inputMode="numeric"
        />
      </label>
      <div className="es-row">
        <Button size="sm" variant="ghost" onClick={() => (i.path ? onLocate(c.id, i.path) : onLocate(c.id))}>
          {i.path ? 'Open instrument sheet' : 'View leads'}
        </Button>
        <Button
          size="sm"
          variant="ghost"
          disabled={!connected}
          onClick={() => trace(ammeter ? 'current' : 'voltage')}
        >
          Add trace
        </Button>
        <Button
          size="sm"
          variant="ghost"
          disabled={!connected}
          onClick={() => trace(ammeter ? 'voltage' : kind.current)}
        >
          {ammeter ? 'Trace burden voltage' : 'Trace current'}
        </Button>
        <Button size="sm" variant="ghost" disabled={!writable} onClick={() => setEditing(!editing)}>
          {editing ? 'Hide settings' : 'Configure instrument'}
        </Button>
      </div>
      {error && <p role="alert">{error}</p>}
      {editing && <InstrumentSettings instrument={i} close={() => setEditing(false)} />}
    </section>
  );
}
