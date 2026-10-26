// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { Meter } from '@embsim/instruments';
import { formatSi, parseSi } from '@embsim/protocol';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';

const KINDS: Record<string, { title: string; resistance: string; fallback: number; current: string }> = {
  'emblink/voltmeter': { title: 'Voltmeter', resistance: 'input_resistance', fallback: 1e7, current: 'current' },
  'emblink/ammeter': { title: 'Ammeter', resistance: 'shunt_resistance', fallback: 1, current: 'current' },
  'emblink/scope_input': { title: 'Scope input', resistance: 'input_resistance', fallback: 1e6, current: 'current' },
  'emblink/function_generator': { title: 'Generator', resistance: 'output_resistance', fallback: 50, current: 'i' },
};

export const isWiredInstrument = (reference: string) => Object.hasOwn(KINDS, reference);

/** Readouts are engine observations. Merely adding a trace never inserts a load;
 * the visible terminals belong to components wired into the project. */
export function WiredInstrumentsPanel({ onLocate, onTrace }: { onLocate(id: string): void; onTrace(target: string): void }) {
  const project = useProject((s) => s.project);
  const outputs = useSession((s) => s.outputs);
  const instruments = project?.components.filter((c) => isWiredInstrument(c.component)) ?? [];
  return <div className="wb-wired-instruments" aria-label="Wired instruments">
    <p className="es-muted">Wired instruments load the circuit. Observation traces alone add no electrical load.</p>
    {!instruments.length && <p className="es-muted">Place a voltmeter, ammeter, scope input or function generator from the Instruments category in Design, then wire both leads.</p>}
    {instruments.map((c) => {
      const kind = KINDS[c.component]!;
      const ammeter = c.component === 'emblink/ammeter';
      const r = parseSi(c.params?.[kind.resistance] as number | string | undefined) ?? kind.fallback;
      const cap = parseSi(c.params?.input_capacitance as number | string | undefined) ?? 15e-12;
      const lead = (pin: string) => Object.entries(project!.nets).find(([, pins]) => pins.includes(`${c.id}.${pin}`))?.[0] ?? 'unconnected';
      const positive = lead('+'), negative = lead('-');
      const connected = positive !== 'unconnected' && negative !== 'unconnected';
      return <section key={c.id} className="wb-wired-instrument" aria-label={`${c.id} ${kind.title}`}>
        <header><strong>{c.id}</strong><span>{kind.title}</span></header>
        <p className="wb-wired-instrument__impedance">{ammeter ? 'Shunt' : c.component === 'emblink/function_generator' ? 'Output' : 'Input'} {formatSi(r, 'Ω')}{c.component === 'emblink/scope_input' ? ` ∥ ${formatSi(cap, 'F')}` : ''}</p>
        <p className="wb-wired-instrument__leads"><span>+ {positive}</span><span>− {negative}</span></p>
        {connected ? <div className="wb-wired-instrument__readings"><Meter label={ammeter ? 'Burden voltage' : 'Terminal voltage'} value={outputs[`${c.id}.voltage`]} unit="V" /><Meter label={c.component === 'emblink/function_generator' ? 'Current into output' : 'Current into +'} value={outputs[`${c.id}.${kind.current}`]} unit="A" /></div> : <p role="status">Wire both leads to include this instrument in the solve.</p>}
        <div className="es-row"><Button size="sm" variant="ghost" onClick={() => onLocate(c.id)}>View leads</Button><Button size="sm" variant="ghost" disabled={!connected} onClick={() => onTrace(`device:${c.id}.${ammeter ? 'current' : 'voltage'}`)}>Add trace</Button></div>
      </section>;
    })}
  </div>;
}
