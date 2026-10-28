// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button, Dialog, Input } from '@embsim/design';
import { acGrid, acCsv, verifyAc, verifyManifest, formatSi, type AcSweep, type EngineeringManifestData, type Project } from '@embsim/protocol';
import { useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';

export function AcSweeps() {
  const project=useProject((s) => s.project), busy=useProject((s) => s.busy);
  const running=useSession((s) => s.running), policy=useSession((s) => s.policy), supported=useSession((s) => s.capabilities.includes('analysis.ac'));
  const [open,setOpen]=useState(false), [pending,setPending]=useState(false), [error,setError]=useState('');
  const [inventory,setInventory]=useState<EngineeringManifestData | null>(null), [result,setResult]=useState<AcSweep | null>(null);
  const [source,setSource]=useState(''), [reference,setReference]=useState('net:GND'), [targets,setTargets]=useState('net:OUT');
  const [start,setStart]=useState('10'), [stop,setStop]=useState('100000'), [count,setCount]=useState('81'), [spacing,setSpacing]=useState('log');
  const [channel,setChannel]=useState(0), [cursor,setCursor]=useState(0), [reviewed,setReviewed]=useState('');
  const revision=useRef<Project | null>(null), epoch=useRef(0);
  const settings=JSON.stringify([source,reference,targets,start,stop,count,spacing]);
  const stale=Boolean(inventory && revision.current !== project);
  const blocked=pending || busy || running || stale || !supported || policy !== 'electrical' || !inventory || inventory.instances.some((i) => i.kind === 'board');
  const prepare=async () => {
    const token=++epoch.current, current=useProject.getState().project;
    setPending(true);setError('');setInventory(null);setResult(null);
    try {
      const data=await verifyManifest(await getClient().project.manifest({}));
      if(token !== epoch.current)return;
      if(current !== useProject.getState().project)throw new Error('Circuit changed while preparing AC analysis. Refresh to continue.');
      if(data.source !== 'native')throw new Error('AC analysis requires a native electrical session.');
      revision.current=current;setInventory(data);
      setSource(data.models.find((m) => m.model === 'vsource' || m.model === 'isource')?.path ?? '');
    }catch(e){if(token === epoch.current)setError(errorMessage(e));}
    finally{if(token === epoch.current)setPending(false);}
  };
  const analyze=async () => {
    if(blocked || !inventory)return;
    const token=++epoch.current;setPending(true);setError('');setResult(null);
    try {
      if(!start.trim() || !stop.trim() || !count.trim())throw new Error('Enter both frequency endpoints and a point count.');
      const request={schema_version:1 as const,kind:'ac_sweep_request' as const,source,reference,frequencies_hz:acGrid(Number(start),Number(stop),Number(count),spacing === 'log'),probes:targets.split(/[,\n]/).map((p) => p.trim()).filter(Boolean)};
      const data=await getClient().analysis.ac(request);
      await verifyAc(data,request,inventory.project.json);
      if(token !== epoch.current)return;
      if(revision.current !== useProject.getState().project)throw new Error('Circuit changed during AC analysis. Refresh before another sweep.');
      setResult(data);setCursor(0);setChannel(0);setReviewed(settings);
    }catch(e){if(token === epoch.current)setError(errorMessage(e));}
    finally{if(token === epoch.current)setPending(false);}
  };
  const download=async (kind:'json'|'csv') => {
    if(!result)return;
    try{await (await platform()).saveTextFile(`ac-sweep.${kind}`,kind === 'csv' ? acCsv(result) : JSON.stringify(result,null,2),kind === 'csv' ? 'text/csv' : 'application/json');}
    catch(e){setError(errorMessage(e));}
  };
  const point=result?.points[cursor], response=point?.responses[channel];
  return <><Button size="sm" variant="ghost" disabled={!project || busy} onClick={() => {setOpen(true);void prepare();}}>AC sweep</Button>
    <Dialog open={open} onOpenChange={(v) => {if(!v){epoch.current++;setOpen(false);setPending(false);}}} title="AC sweep" description="Native linear circuit response across frequency; your live bench stays intact." className="wb-transient-capture" footer={<><Button size="sm" variant="ghost" disabled={pending} onClick={() => void prepare()}>Refresh AC inputs</Button><Button size="sm" disabled={blocked || !source || !targets.trim()} onClick={() => void analyze()}>Run AC sweep</Button></>}>
      {!supported && <p role="alert">AC sweeps require an updated native linear electrical session.</p>}
      {policy !== 'electrical' && <p role="alert">Select electrical policy before AC analysis.</p>}
      {running && <p role="status">Pause the source before analysis.</p>}
      {inventory?.instances.some((i) => i.kind === 'board') && <p role="alert">Firmware circuits are not supported by this linear solver.</p>}
      {stale && <p role="alert">Circuit changed. Refresh AC inputs; existing exports retain their reviewed source.</p>}
      {pending && <p role="status">Preparing native AC analysis…</p>}{error && <p role="alert">{error}</p>}
      <div className="wb-sweep-config">
        <label>Excitation source<select aria-label="AC source" value={source} disabled={blocked} onChange={(e) => setSource(e.target.value)}><option value="">Choose an independent source</option>{inventory?.models.filter((m) => m.model === 'vsource' || m.model === 'isource').map((m) => <option key={m.path} value={m.path}>{m.path} · {m.model === 'vsource' ? '1 V' : '1 A'} phasor</option>)}</select></label>
        <label>Reference terminal<Input aria-label="AC reference" value={reference} disabled={blocked} onChange={(e) => setReference(e.target.value)} /></label>
        <label>Start (Hz)<Input aria-label="AC start frequency" type="number" value={start} disabled={blocked} onChange={(e) => setStart(e.target.value)} /></label>
        <label>Stop (Hz)<Input aria-label="AC stop frequency" type="number" value={stop} disabled={blocked} onChange={(e) => setStop(e.target.value)} /></label>
        <label>Frequency points<Input aria-label="AC point count" type="number" min={1} max={256} value={count} disabled={blocked} onChange={(e) => setCount(e.target.value)} /></label>
        <label>Frequency spacing<select aria-label="AC spacing" value={spacing} disabled={blocked} onChange={(e) => setSpacing(e.target.value)}><option value="log">Logarithmic</option><option value="linear">Linear</option></select></label>
        <label className="wb-sweep-wide">Output voltages<textarea aria-label="AC voltage targets" value={targets} disabled={blocked} onChange={(e) => setTargets(e.target.value)} rows={2} placeholder="net:OUT, pin:R1.2" /></label>
      </div>
      <p>Linear RLC networks, independent sources and wired R∥C instrument inputs only. Nonlinear, digital, scripted and firmware models are rejected. The chosen source has a unit real phasor; other sources have zero AC excitation. Source waveforms and DC offsets do not set the AC excitation. Instrument loading and series resistances remain physical.</p>
      {result && <section aria-label="AC sweep results"><h3>{result.points.length} native AC points</h3>
        {reviewed !== settings && <p role="status">Options changed. Recompute to apply them; plots and exports retain the reviewed settings.</p>}
        <p>{result.request.source} excitation · output minus {result.request.reference} · {result.unit === 'V/V' ? 'voltage gain (V/V)' : 'transimpedance (ohm)'} · wrapped phase relative to source</p>
        <label>Review output<select aria-label="AC review output" value={channel} onChange={(e) => setChannel(Number(e.target.value))}>{result.request.probes.map((p,i) => <option key={p} value={i}>{p}</option>)}</select></label>
        <AcPlot data={result} channel={channel} selected={cursor} phase={false} /><AcPlot data={result} channel={channel} selected={cursor} phase />
        <label>Frequency cursor<input aria-label="AC frequency cursor" type="range" min={0} max={result.points.length-1} value={cursor} onChange={(e) => setCursor(Number(e.target.value))} /></label>
        {response && point && <div className="wb-sweep-table"><table aria-label="AC cursor measurements"><thead><tr><th>Frequency</th><th>Magnitude · {result.unit}</th><th>dB · {result.unit === 'V/V' ? '1 V/V' : '1 ohm'}</th><th>Phase</th><th>Real</th><th>Imaginary</th></tr></thead><tbody><tr><td>{formatSi(point.frequency_hz,'Hz',8)}</td><td>{response.magnitude.toPrecision(9)}</td><td>{response.db === null ? '−∞ (zero)' : response.db.toFixed(6)}</td><td>{response.phase_deg === null ? 'Undefined (zero)' : `${response.phase_deg.toFixed(6)}°`}</td><td>{response.real.toPrecision(9)}</td><td>{response.imag.toPrecision(9)}</td></tr></tbody></table></div>}
        <details><summary>AC interpretation and limits</summary><p>Steady sinusoidal linear equations include source resistance, capacitor ESR, inductor DCR and meter/scope R∥C loading. Responses normalize to the internal source excitation, not to a separately measured loaded input. Current-source positive excitation flows from + to −. No noise, transient/initial-condition response, nonlinear linearization, phase unwrapping or manufacturer qualification is implied. Zero responses have undefined phase and logarithmic amplitude; singular or unreliable matrices fail without fabricated points.</p></details>
        <div className="es-row"><Button size="sm" variant="ghost" onClick={() => void download('csv')}>Export AC CSV</Button><Button size="sm" variant="ghost" onClick={() => void download('json')}>Export AC evidence</Button></div>
      </section>}
    </Dialog></>;
}

export function AcPlot({data,channel,selected,phase}: {data:AcSweep;channel:number;selected:number;phase:boolean}) {
  const values=data.points.map((p) => phase?p.responses[channel]!.phase_deg:p.responses[channel]!.db);
  const finite=values.filter((v):v is number => v !== null && Number.isFinite(v));
  if(!finite.length)return <p>{phase?'Phase is undefined for all zero responses.':'All responses are zero: −∞ dB.'}</p>;
  const min=Math.log10(data.points[0]!.frequency_hz),max=Math.log10(data.points.at(-1)!.frequency_hz);
  const low=phase?-180:Math.min(...finite)-3,high=phase?180:Math.max(...finite)+3;
  const x=(f:number) => min === max ? 400 : 75+650*(Math.log10(f)-min)/(max-min);
  const y=(v:number) => 220-185*(v-low)/(high-low);
  let previous:number|null=null;
  const path=data.points.map((p,i) => {const v=values[i];if(v === null || v === undefined){previous=null;return '';}const command=previous === null || (phase && Math.abs(v-previous)>180)?'M':'L';previous=v;return `${command}${x(p.frequency_hz)},${y(v)}`;}).join(' ');
  const point=data.points[selected],v=values[selected];
  return <svg className="wb-sweep-plot" viewBox="0 0 800 275" role="img" aria-label={phase?'AC phase Bode plot':'AC magnitude Bode plot'}><title>{phase?'Wrapped phase; jumps over 180 degrees break the line.':'Magnitude in dB; zero responses break the line.'}</title>{[0,.25,.5,.75,1].map((t) => <g key={t}><line x1={75} x2={725} y1={35+185*t} y2={35+185*t} className="wb-sweep-grid"/><text x={65} y={40+185*t} textAnchor="end">{(high*(1-t)+low*t).toFixed(1)}{phase?'°':' dB'}</text><text x={75+650*t} y={243} textAnchor="middle">{min !== max || t === .5 ? formatSi(10**(min*(1-t)+max*t),'Hz'):''}</text></g>)}<path d={path} fill="none" stroke="var(--accent)" strokeWidth={2}/>{point && v !== null && v !== undefined && <circle cx={x(point.frequency_hz)} cy={y(v)} r={4} fill="var(--info)"/>}<text x={400} y={269} textAnchor="middle">Frequency · logarithmic axis · {phase?'phase relative to source':`dB referenced to 1 ${data.unit}`}</text></svg>;
}
