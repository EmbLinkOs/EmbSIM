// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import type { AcquisitionSettings } from '@embsim/instruments';
import { formatTime } from '@embsim/protocol';
import { useState } from 'react';
import { useInstruments } from '../../stores/instruments';
import { useSession } from '../../stores/session';
import { useProject } from '../../stores/project';
import { probes as actions, errorMessage } from '../../actions';

export function AcquisitionPanel() {
  const inst = useInstruments(), now = useSession((s) => s.now);
  const project = useProject((s) => s.project);
  const numeric = inst.probes.filter((p) => ['analog','digital','value'].includes(p.kind));
  const [source, setSource] = useState('');
  const [edge, setEdge] = useState<AcquisitionSettings['edge']>('rising');
  const [mode, setMode] = useState<AcquisitionSettings['mode']>('single');
  const [level, setLevel] = useState('1.65'), [band, setBand] = useState('0.05');
  const [span, setSpan] = useState('20'), [pre, setPre] = useState('25'), [holdoff, setHoldoff] = useState('0');
  const [error, setError] = useState(''), [moving, setMoving] = useState<string | null>(null);
  const chosen = numeric.some((p) => p.probe === source) ? source : String(numeric[0]?.probe ?? '');
  const digital = numeric.find((p) => p.probe === chosen)?.kind === 'digital';
  const arm = () => {
    try {
      if (!chosen || ![span,pre,holdoff,level,band].every((v) => v.trim() !== '' && Number.isFinite(Number(v)))) throw new Error('Enter finite capture settings');
      inst.arm({ mode, source: chosen, edge, level: Number(level), hysteresis: Number(band), span: BigInt(Math.round(Number(span) * 1e9)), preTrigger: Number(pre) / 100, holdoff: BigInt(Math.round(Number(holdoff) * 1e9)) }, now);
      setError('');
    } catch (e) { setError(errorMessage(e)); }
  };
  return <div className="wb-acquisition">
    <p className="es-muted">Ideal observation probes. Physical input loading belongs to wired instruments. Triggers use sampled edges in virtual time.</p>
    <p role="status" aria-label="Acquisition status">{inst.acquisition ? `${inst.acquisition.phase}: ${inst.acquisition.reason}` : 'Live · following received samples'}{inst.acquisition?.frame ? ` · #${inst.acquisition.frame.number} · T = ${formatTime(inst.acquisition.frame.at)}` : ''}</p>
    <div className="wb-acquisition__grid">
      <label>Trigger source<select value={chosen} onChange={(e) => setSource(e.target.value)} aria-label="Trigger source">{numeric.map((p) => <option key={p.probe} value={p.probe}>{p.probe} · {p.target}</option>)}</select></label>
      <label>Acquisition mode<select value={mode} onChange={(e) => setMode(e.target.value as typeof mode)}><option value="single">Single</option><option value="normal">Normal · repeat</option></select></label>
      <label>Trigger edge<select value={edge} onChange={(e) => setEdge(e.target.value as typeof edge)}><option value="rising">Rising</option><option value="falling">Falling</option><option value="either">Either</option></select></label>
      <label>Trigger level<input type="number" step="any" disabled={digital} value={level} onChange={(e) => setLevel(e.target.value)} /></label>
      <label>Hysteresis<input type="number" min="0" step="any" disabled={digital} value={band} onChange={(e) => setBand(e.target.value)} /></label>
      <label>Capture span (ms)<input type="number" min="0.000001" step="any" value={span} onChange={(e) => setSpan(e.target.value)} /></label>
      <label>Pre-trigger (%)<input type="number" min="0" max="100" value={pre} onChange={(e) => setPre(e.target.value)} /></label>
      <label>Holdoff (ms)<input type="number" min="0" step="any" value={holdoff} onChange={(e) => setHoldoff(e.target.value)} /></label>
    </div>
    <div className="es-row"><Button size="sm" onClick={arm} disabled={!chosen}>{inst.acquisition ? 'Rearm capture' : 'Arm capture'}</Button><Button size="sm" variant="ghost" onClick={inst.live}>Live view</Button></div>

    {error && <p role="alert">{error}</p>}
    <p className="es-muted">A single capture freezes waveforms and exports, while firmware continues. Move voltage/logic probes by dragging their markers or choosing a net below. Rearm to replace a frozen capture.</p>
    {numeric.map((p) => <label key={p.probe} className="wb-acquisition__probe">{p.probe}<select aria-label={`Move probe ${p.probe}`} value={p.target.startsWith('net:') ? p.target.slice(4) : ''} disabled={moving !== null || !['analog','digital'].includes(p.kind) || !/^(net|pin):/.test(p.target)} onChange={(e) => { setMoving(String(p.probe)); void actions.move(String(p.probe), `net:${e.target.value}`).then(() => setError('')).catch((e: unknown) => setError(errorMessage(e))).finally(() => setMoving(null)); }}><option value="" disabled>{p.target}</option>{Object.keys(project?.nets ?? {}).sort().map((n) => <option key={n}>{n}</option>)}</select></label>)}
  </div>;
}
