// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Dialog, Input } from '@embsim/design';
import { captureTraces, formatSi, formatTime, transientCsv, verifyManifest, type CaptureTrace, type EngineeringManifestData, type Project, type TransientCapture as Capture } from '@embsim/protocol';
import { useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';

const COLORS = ['var(--accent)', 'var(--info)', 'var(--signal-analog)', 'var(--danger)', 'var(--warning)', 'var(--signal-low)', 'var(--text)', 'var(--signal-z)', 'var(--text-muted)'];
export function TransientCapture() {
  const project = useProject((s) => s.project), busy = useProject((s) => s.busy), catalog = useProject((s) => s.catalog);
  const running = useSession((s) => s.running), policy = useSession((s) => s.policy), supported = useSession((s) => s.capabilities.includes('analysis.transient'));
  const [open, setOpen] = useState(false), [pending, setPending] = useState(false), [error, setError] = useState('');
  const [inventory, setInventory] = useState<EngineeringManifestData | null>(null), [result, setResult] = useState<Capture | null>(null);
  const [start, setStart] = useState('0us'), [stop, setStop] = useState('500us'), [step, setStep] = useState('5us'), [targets, setTargets] = useState('');
  const [positive, setPositive] = useState(0), [negative, setNegative] = useState(-1), [hidden, setHidden] = useState<string[]>([]);
  const [left, setLeft] = useState(0), [right, setRight] = useState(0), [cursorA, setCursorA] = useState(0), [cursorB, setCursorB] = useState(0), [page, setPage] = useState(0);
  const revision = useRef<Project | null>(null), epoch = useRef(0);
  const stale = Boolean(inventory && revision.current !== project);
  const blocked = pending || busy || running || stale || !supported || policy !== 'electrical' || Boolean(inventory?.instances.some((i) => i.kind === 'board'));
  const load = async () => {
    const token = ++epoch.current, source = useProject.getState().project;
    setPending(true); setError(''); setInventory(null); setResult(null);
    try {
      const data = await verifyManifest(await getClient().project.manifest({}));
      if (token !== epoch.current) return;
      if (source !== useProject.getState().project) throw new Error('Circuit changed while preparing capture. Refresh to continue.');
      revision.current = source; setInventory(data);
    } catch (e) {if (token === epoch.current) setError(errorMessage(e));}
    finally {if (token === epoch.current) setPending(false);}
  };
  const capture = async () => {
    if (blocked) return;
    const token = ++epoch.current; setPending(true); setError(''); setResult(null);
    try {
      const data = await getClient().analysis.transient({schema_version: 1, kind: 'transient_request', start, stop, step, probes: targets.split(/[,\n]/).map((s) => s.trim()).filter(Boolean)});
      const source = await verifyManifest(data.source);
      if (token !== epoch.current) return;
      if (revision.current !== useProject.getState().project || source.project.json !== inventory?.project.json) throw new Error('Circuit changed since preparation. Refresh before another capture.');
      setResult(data); setLeft(0); setRight(Math.max(0, data.samples.length - 1)); setCursorA(0); setCursorB(Math.max(0, data.samples.length - 1)); setPage(0); setHidden([]); setPositive(0); setNegative(-1);
    } catch (e) {if (token === epoch.current) setError(errorMessage(e));}
    finally {if (token === epoch.current) setPending(false);}
  };
  const download = async (kind: 'json' | 'csv') => {
    if (!result) return;
    try {await (await platform()).saveTextFile(`transient-capture.${kind}`, kind === 'csv' ? transientCsv(result) : JSON.stringify(result, null, 2), kind === 'csv' ? 'text/csv' : 'application/json');}
    catch (e) {setError(errorMessage(e));}
  };
  const traces = result ? captureTraces(result, positive, negative >= 0 ? negative : undefined) : [];
  const cursorTime = (index: number) => result?.samples[index]?.at_ps ?? '0';
  const terminalOptions = inventory?.instances.filter((i) => i.kind === 'component').flatMap((part) => (catalog[part.reference]?.description.pins ?? []).map((pin) => `pin:${part.path}.${pin.name}`)) ?? [];
  return <>
    <Button size="sm" variant="ghost" disabled={!project || busy} onClick={() => {setOpen(true); void load();}}>Transient capture</Button>
    <Dialog open={open} onOpenChange={(v) => {if (!v) {epoch.current++; setOpen(false); setPending(false);}}} title="Transient capture" description="Capture an isolated native circuit response; keep your open bench intact." className="wb-transient-capture" footer={<><Button size="sm" variant="ghost" disabled={pending} onClick={() => void load()}>Refresh capture inputs</Button><Button size="sm" disabled={blocked || !targets.trim()} onClick={() => void capture()}>Capture isolated response</Button></>}>
      {!supported && <p role="alert">Transient capture requires an updated native electrical session.</p>}
      {policy !== 'electrical' && <p role="alert">Select electrical policy before capture.</p>}
      {running && <p role="status">Pause the source before capture. Its clock and runtime controls will be preserved.</p>}
      {stale && <p role="alert">Circuit changed. Refresh before capturing again; existing results retain their captured source.</p>}
      {inventory?.instances.some((i) => i.kind === 'board') && <p role="alert">This capture supports board-free circuits. Firmware replay is not implemented.</p>}
      {pending && <p role="status">Capturing native response…</p>}{error && <p role="alert">{error}</p>}
      <div className="wb-sweep-config">
        <label>Start after reset<Input aria-label="Capture start" value={start} disabled={blocked} onChange={(e) => setStart(e.target.value)} /></label>
        <label>Stop after reset<Input aria-label="Capture stop" value={stop} disabled={blocked} onChange={(e) => setStop(e.target.value)} /></label>
        <label>Sample interval<Input aria-label="Capture interval" value={step} disabled={blocked} onChange={(e) => setStep(e.target.value)} /></label>
        <label className="wb-sweep-wide">Add terminal<select aria-label="Add capture terminal" value="" disabled={blocked} onChange={(e) => {if (e.target.value) setTargets((old) => [...new Set([...old.split(/[,\n]/).map((s) => s.trim()).filter(Boolean), e.target.value])].join(', '));}}><option value="">Choose a concrete circuit terminal</option>{terminalOptions.map((p) => <option key={p} value={p}>{p}</option>)}</select></label>
        <label className="wb-sweep-wide">Voltage targets<textarea aria-label="Capture measurements" rows={2} value={targets} disabled={blocked} onChange={(e) => setTargets(e.target.value)} placeholder="net:IN, net:OUT or pin:A.R.2" /></label>
      </div>
      <p className="es-muted">Reset starts at 0; warm-up before Start is simulated. Up to 4097 uniform samples, eight voltage targets, 100 ms stop time and 256 compiled components. Reads add no load; connected instruments do. Narrow pulses and peaks between samples can be missed. Runtime stimulus and firmware are not replayed.</p>
      {result && <section aria-label="Transient results">
        <div className="es-row"><h3>Native circuit response</h3><Badge tone={result.ok ? 'accent' : 'danger'}>{result.ok ? `${result.samples.length} samples captured` : 'Capture incomplete'}</Badge></div>
        {result.failure && <p role="alert">{result.failure} · ended at {formatTime(BigInt(result.ended_at_ps))}. Only the captured prefix is shown.</p>}
        {result.diagnostics.length > 0 && <details><summary>{result.diagnostics.length} native diagnostics</summary>{result.diagnostics.map((d, i) => <p key={i}>{d.code}: {d.message}</p>)}</details>}
        <p>Captured source at {formatTime(BigInt(inventory?.now_ps ?? '0'))}; the open circuit was not restarted. Lines join sampled values; missing voltages break the trace.</p>
        {result.samples.length > 0 && <>
          <div className="wb-transient-legend">{traces.map((t, i) => <label key={t.id}><input type="checkbox" checked={!hidden.includes(t.id)} onChange={(e) => setHidden((old) => e.target.checked ? old.filter((id) => id !== t.id) : [...old, t.id])} /><span style={{color: COLORS[i]}}>{t.label}</span></label>)}</div>
          <div className="wb-transient-review"><label>Differential positive<select aria-label="Differential positive" value={positive} onChange={(e) => setPositive(Number(e.target.value))}>{result.request.probes.map((p, i) => <option key={p} value={i}>{p}</option>)}</select></label><label>Differential negative<select aria-label="Differential negative" value={negative} onChange={(e) => setNegative(Number(e.target.value))}><option value={-1}>Off</option>{result.request.probes.map((p, i) => <option key={p} value={i}>{p}</option>)}</select></label></div>
          <p className="es-muted">Differential review subtracts captured voltages and adds no physical instrument.</p>
          <div className="wb-transient-review"><label>View first sample<Input aria-label="View first sample" type="number" min={0} max={right} value={left} onChange={(e) => setLeft(Math.max(0, Math.min(right, Math.trunc(Number(e.target.value)))))} /></label><label>View last sample<Input aria-label="View last sample" type="number" min={left} max={result.samples.length-1} value={right} onChange={(e) => setRight(Math.max(left, Math.min(result.samples.length-1, Math.trunc(Number(e.target.value)))))} /></label><Button size="sm" variant="ghost" onClick={() => {setLeft(0); setRight(result.samples.length-1);}}>Fit capture</Button></div>
          <TransientPlot data={result} traces={traces.filter((t) => !hidden.includes(t.id))} colors={traces.map((t, i) => ({id: t.id, color: COLORS[i]!}))} left={left} right={right} cursorA={cursorA} cursorB={cursorB} />
          <div className="wb-transient-review"><label>Cursor A · {formatTime(BigInt(cursorTime(cursorA)))}<input aria-label="Cursor A sample" type="range" min={0} max={result.samples.length-1} value={cursorA} onChange={(e) => setCursorA(Number(e.target.value))} /></label><label>Cursor B · {formatTime(BigInt(cursorTime(cursorB)))}<input aria-label="Cursor B sample" type="range" min={0} max={result.samples.length-1} value={cursorB} onChange={(e) => setCursorB(Number(e.target.value))} /></label><strong>Δt {formatTime(BigInt(cursorTime(cursorB))-BigInt(cursorTime(cursorA)))}</strong></div>
          <div className="wb-sweep-table"><table aria-label="Transient cursor measurements"><thead><tr><th>Trace</th><th>Cursor A</th><th>Cursor B</th><th>B − A</th><th>View sample min / max</th></tr></thead><tbody>{traces.map((t) => {
            const a = t.values[cursorA] ?? null, b = t.values[cursorB] ?? null, known = t.values.slice(left, right+1).filter((v): v is number => v !== null);
            const volts = (v: number | null) => v === null ? 'Unavailable' : formatSi(v, 'V', 6);
            return <tr key={t.id}><td>{t.label}</td><td>{volts(a)}</td><td>{volts(b)}</td><td>{a === null || b === null ? 'Unavailable' : volts(b-a)}</td><td>{known.length ? `${volts(Math.min(...known))} / ${volts(Math.max(...known))}` : 'Unavailable'}</td></tr>;
          })}</tbody></table></div>
          <details><summary>Exact captured samples</summary><div className="es-row"><Button size="sm" disabled={page === 0} onClick={() => setPage(page-1)}>Previous samples</Button><span>Samples {page*100+1}–{Math.min(result.samples.length, (page+1)*100)}</span><Button size="sm" disabled={(page+1)*100 >= result.samples.length} onClick={() => setPage(page+1)}>Next samples</Button></div><div className="wb-sweep-table"><table aria-label="Native transient samples"><thead><tr><th>Time</th>{result.request.probes.map((p) => <th key={p}>{p}</th>)}</tr></thead><tbody>{result.samples.slice(page*100, (page+1)*100).map((s) => <tr key={s.at_ps}><td>{formatTime(BigInt(s.at_ps))}<small>{s.at_ps} ps</small></td>{s.measurements.map((m) => <td key={m.target}>{m.voltage === null ? 'Unavailable' : formatSi(m.voltage, 'V', 8)} <small>Logic {m.logic}</small></td>)}</tr>)}</tbody></table></div></details>
        </>}
        <div className="es-row"><Button size="sm" variant="ghost" onClick={() => void download('csv')}>Export transient CSV</Button><Button size="sm" variant="ghost" onClick={() => void download('json')}>Export transient evidence</Button></div>
      </section>}
    </Dialog>
  </>;
}

export function TransientPlot({data, traces, colors, left, right, cursorA, cursorB}: {data: Capture; traces: CaptureTrace[]; colors: {id: string; color: string}[]; left: number; right: number; cursorA: number; cursorB: number}) {
  const first = BigInt(data.samples[left]?.at_ps ?? '0'), last = BigInt(data.samples[right]?.at_ps ?? '0');
  const known = traces.flatMap((t) => t.values.slice(left, right+1)).filter((v): v is number => v !== null && Number.isFinite(v));
  if (!known.length) return <p>No numeric voltages in this view. Enable a trace or inspect missing-value evidence.</p>;
  let low = Math.min(...known), high = Math.max(...known);
  if (low === high) {if (low > 0) low *= .95; else if (high < 0) high *= .95; else {low=-1; high=1;}}
  const scale = Math.max(Math.abs(low), Math.abs(high)) || 1;
  const x = (index: number) => first === last ? 400 : 75 + 650*Number(BigInt(data.samples[index]!.at_ps)-first)/Number(last-first);
  const y = (v: number) => 220 - 185*(v/scale-low/scale)/(high/scale-low/scale);
  return <svg className="wb-sweep-plot wb-transient-plot" viewBox="0 0 800 275" role="img" aria-label="Native transient voltage traces against time">
    <title>Uniform native samples; missing values break traces. Exact cursor samples are in the measurement table.</title>
    {[0, .25, .5, .75, 1].map((t) => <g key={t}><line x1={75} x2={725} y1={35+185*t} y2={35+185*t} className="wb-sweep-grid" /><text x={65} y={40+185*t} textAnchor="end">{formatSi(high*(1-t)+low*t, 'V')}</text><text x={75+650*t} y={243} textAnchor="middle">{first !== last || t === .5 ? formatTime(first + BigInt(Math.round(Number(last-first)*t)), {padZeros: false}) : ''}</text></g>)}
    {traces.map((t) => {
      const visible = t.values.slice(left, right+1);
      const numeric = (v: number | null | undefined): v is number => v != null && Number.isFinite(v);
      const color = colors.find((c) => c.id === t.id)?.color ?? 'currentColor';
      let gap = true;
      const path = visible.map((v, i) => {
        if (!numeric(v)) {gap = true; return '';}
        const d = `${gap ? 'M' : 'L'}${x(i+left)},${y(v)}`; gap = false; return d;
      }).join(' ');
      return <g key={t.id}><path data-trace={t.id} d={path} fill="none" stroke={color} strokeWidth={2} strokeDasharray={t.id === 'difference' ? '6 3' : undefined} />
        {visible.map((v, i) => numeric(v) && !numeric(visible[i-1]) && !numeric(visible[i+1]) ? <circle key={i} cx={x(i+left)} cy={y(v)} r={4} fill={color} /> : null)}
      </g>;
    })}
    {[['A',cursorA],['B',cursorB]].map(([label,index]) => Number(index)>=left && Number(index)<=right && <g key={label}><line x1={x(Number(index))} x2={x(Number(index))} y1={30} y2={220} className="wb-transient-cursor" /><text x={x(Number(index))} y={22} textAnchor="middle">{label}</text></g>)}
    <text x={400} y={269} textAnchor="middle">Time after reset · uniformly sampled voltage</text>
  </svg>;
}
