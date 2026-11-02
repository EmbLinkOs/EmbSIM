// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { formatSi, type NetState } from '@embsim/protocol';
import { t as tr } from '../../i18n';

export function differentialVoltage(nets: Record<string, NetState>, a?: string, b?: string): number | null {
  const va = a ? nets[a]?.voltage : undefined, vb = b ? nets[b]?.voltage : undefined;
  return Number.isFinite(va) && Number.isFinite(vb) ? va! - vb! : null;
}
export function VoltageComparison({ names, nets, leads, onChange, active, onPick }: { names: string[]; nets: Record<string, NetState>; leads: {a?: string; b?: string}; onChange: (side:'a'|'b', net:string) => void; active: 'a'|'b'|null; onPick: (side:'a'|'b'|null) => void }) {
  const value = differentialVoltage(nets, leads.a, leads.b);
  return <div className="wb-voltage-comparison">
    <output aria-label={tr('vc.output')}>{value === null ? '—' : formatSi(value, 'V')}</output>
    {(['a','b'] as const).map((side) => <div key={side} className={`wb-voltage-comparison__lead wb-voltage-comparison__lead--${side}`}><label>{side.toUpperCase()}<select aria-label={tr('vc.probeNet', { side: side.toUpperCase() })} value={names.includes(leads[side] ?? '') ? leads[side] : ''} onChange={(e) => onChange(side,e.target.value)}><option value="">{tr('vc.selectNet')}</option>{names.map((name) => <option key={name}>{name}</option>)}</select></label><Button size="sm" variant="ghost" pressed={active===side} onClick={() => onPick(active===side ? null : side)}>Pick {side.toUpperCase()}</Button></div>)}
    <p>{active ? tr('vc.pick', { side: active.toUpperCase() }) : tr('vc.hint')}</p>
    <small>{tr('vc.ideal')}</small>
    {value === null && leads.a && leads.b && <p role="status">{tr('vc.needVoltages')}</p>}
  </div>;
}
