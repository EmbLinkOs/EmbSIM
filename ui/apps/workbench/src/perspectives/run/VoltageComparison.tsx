// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { formatSi, type NetState } from '@embsim/protocol';

export function differentialVoltage(nets: Record<string, NetState>, a?: string, b?: string): number | null {
  const va = a ? nets[a]?.voltage : undefined, vb = b ? nets[b]?.voltage : undefined;
  return Number.isFinite(va) && Number.isFinite(vb) ? va! - vb! : null;
}
export function VoltageComparison({ names, nets, leads, onChange, active, onPick }: { names: string[]; nets: Record<string, NetState>; leads: {a?: string; b?: string}; onChange: (side:'a'|'b', net:string) => void; active: 'a'|'b'|null; onPick: (side:'a'|'b'|null) => void }) {
  const value = differentialVoltage(nets, leads.a, leads.b);
  return <div className="wb-voltage-comparison">
    <output aria-label="Differential voltage A minus B">{value === null ? '—' : formatSi(value, 'V')}</output>
    {(['a','b'] as const).map((side) => <div key={side} className={`wb-voltage-comparison__lead wb-voltage-comparison__lead--${side}`}><label>{side.toUpperCase()}<select aria-label={`Voltage probe ${side.toUpperCase()} net`} value={names.includes(leads[side] ?? '') ? leads[side] : ''} onChange={(e) => onChange(side,e.target.value)}><option value="">Select net</option>{names.map((name) => <option key={name}>{name}</option>)}</select></label><Button size="sm" variant="ghost" pressed={active===side} onClick={() => onPick(active===side ? null : side)}>Pick {side.toUpperCase()}</Button></div>)}
    <p>{active ? `Pick probe ${active.toUpperCase()}: click a wire or pin, or focus it and press Enter.` : 'A − B · drag either marker to another wire or pin.'}</p>
    <small>Ideal observation probes: zero loading. Use wired instruments to include input resistance, capacitance or burden.</small>
    {value === null && leads.a && leads.b && <p role="status">Both nets need finite solved voltages; logic states alone do not specify a voltage.</p>}
  </div>;
}
