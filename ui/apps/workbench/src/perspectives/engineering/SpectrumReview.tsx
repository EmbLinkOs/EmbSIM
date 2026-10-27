// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, Input } from '@embsim/design';
import { formatSi, spectrumCsv, verifySpectrum, type FrequencySpectrum, type TransientCapture } from '@embsim/protocol';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useSession } from '../../stores/session';

export function SpectrumReview({capture}: {capture: TransientCapture}) {
  const supported = useSession((s) => s.capabilities.includes('analysis.spectrum'));
  const [target,setTarget] = useState(capture.request.probes[0] ?? ''), [window,setWindow] = useState<'hann' | 'rectangular'>('hann'), [removeMean,setRemoveMean] = useState(true);
  const [pending,setPending] = useState(false), [error,setError] = useState(''), [result,setResult] = useState<FrequencySpectrum | null>(null);
  const [bin,setBin] = useState(0), [first,setFirst] = useState(0), [last,setLast] = useState(0), [scale,setScale] = useState<'peak' | 'dbv'>('peak'), [floor,setFloor] = useState(-140);
  const n = capture.samples.length-1, ready = capture.ok && n >= 16 && n <= 4096 && !(n & (n-1));
  const analyze = async () => {
    if (pending || !ready || !supported) return;
    setPending(true);setError('');setResult(null);
    try {
      const data = await getClient().analysis.spectrum({capture,target,window,remove_mean:removeMean});
      await verifySpectrum(data,capture);
      setResult(data);setFirst(0);setLast(data.bins.length-1);
      const largest = data.bins.slice(1).reduce((best,b) => b.peak_v > best.peak_v ? b : best, data.bins[0]!);
      setBin(largest.index);
    } catch (e) {setError(errorMessage(e));} finally {setPending(false);}
  };
  const download = async (kind: 'json' | 'csv') => {
    if (!result) return;
    try {await (await platform()).saveTextFile(`frequency-spectrum.${kind}`,kind === 'csv' ? spectrumCsv(result) : JSON.stringify(result,null,2),kind === 'csv' ? 'text/csv' : 'application/json');}
    catch (e) {setError(errorMessage(e));}
  };
  const selected = result?.bins[bin];
  const revised = Boolean(result && (result.target !== target || result.window !== window || result.remove_mean !== removeMean));
  return <details className="wb-spectrum-review"><summary>Frequency spectrum</summary>
    <p>Analyze the retained capture without running the circuit again. The final endpoint is excluded to form a half-open FFT record.</p>
    {!supported && <p role="alert">Frequency spectra require an updated native engine.</p>}
    {!ready && <p role="status">FFT needs 17, 33, 65, …, 4097 complete samples. Use the 256-interval preset above and capture again; existing samples are never silently padded or interpolated.</p>}
    <div className="wb-transient-review"><label>Spectrum target<select aria-label="Spectrum target" value={target} disabled={pending} onChange={(e) => setTarget(e.target.value)}>{capture.request.probes.map((p) => <option key={p} value={p}>{p}</option>)}</select></label><label>Window<select aria-label="Spectrum window" value={window} disabled={pending} onChange={(e) => setWindow(e.target.value as 'hann' | 'rectangular')}><option value="hann">Periodic Hann</option><option value="rectangular">Rectangular</option></select></label><label className="wb-spectrum-check"><input type="checkbox" checked={removeMean} disabled={pending} onChange={(e) => setRemoveMean(e.target.checked)} />Remove sample mean</label><Button size="sm" disabled={!supported || !ready || pending} onClick={() => void analyze()}>Analyze captured spectrum</Button></div>
    {pending && <p role="status">Computing native spectrum…</p>}{error && <p role="alert">{error}</p>}
    {result && <section aria-label="Frequency spectrum results"><h3>{result.target} <Badge tone="accent">{result.used_samples} FFT samples</Badge></h3>
      {revised && <p role="status">Options changed. Recompute to apply them; the current plot/export retains the reviewed settings.</p>}
      <p>{formatSi(result.sample_rate_hz,'Hz')} sample rate · {formatSi(result.bin_width_hz,'Hz')} bin spacing · {formatSi(result.nyquist_hz,'Hz')} Nyquist</p>
      <div className="wb-transient-review"><label>Amplitude display<select aria-label="Spectrum amplitude display" value={scale} onChange={(e) => setScale(e.target.value as 'peak' | 'dbv')}><option value="peak">V peak</option><option value="dbv">dBV · RMS bin</option></select></label>{scale === 'dbv' && <label>Display floor (dBV)<Input aria-label="Spectrum display floor" type="number" value={floor} onChange={(e) => {const v=Number(e.target.value);if(Number.isFinite(v))setFloor(v);}} /></label>}<label>First bin<Input aria-label="Spectrum first bin" type="number" min={0} max={last} value={first} onChange={(e) => setFirst(Math.max(0,Math.min(last,Math.trunc(Number(e.target.value)))))} /></label><label>Last bin<Input aria-label="Spectrum last bin" type="number" min={first} max={result.bins.length-1} value={last} onChange={(e) => setLast(Math.max(first,Math.min(result.bins.length-1,Math.trunc(Number(e.target.value)))))} /></label></div>
      <SpectrumPlot data={result} first={first} last={last} selected={bin} scale={scale} floor={floor} />
      <label className="wb-spectrum-bin">Inspect bin {bin}<input aria-label="Inspect spectrum bin" type="range" min={0} max={result.bins.length-1} value={bin} onChange={(e) => setBin(Number(e.target.value))} /></label>
      {selected && <div className="wb-sweep-table"><table aria-label="Spectrum bin measurements"><thead><tr><th>Frequency</th><th>Peak amplitude</th><th>RMS bin amplitude</th><th>dBV · RMS bin</th><th>Phase</th></tr></thead><tbody><tr><td>{formatSi(selected.frequency_hz,'Hz',8)}</td><td>{formatSi(selected.peak_v,'V',8)}</td><td>{formatSi(selected.rms_v,'V',8)}</td><td>{selected.dbv_rms === null ? '−∞ (zero)' : `${selected.dbv_rms.toFixed(6)} dBV`}</td><td>{selected.phase_deg === null ? 'Undefined (zero)' : `${selected.phase_deg.toFixed(6)}°`}</td></tr></tbody></table></div>}
      <details><summary>Scaling and sampling limits</summary><p>Reviewed settings: {result.window}, sample mean {result.remove_mean ? 'removed' : 'retained'} ({formatSi(result.mean_v,'V')}). Coherent gain {result.coherent_gain.toPrecision(6)}; window ENBW {result.enbw_bins.toPrecision(6)} bins. Peak amplitudes correct for window coherent gain. Interior RMS bins divide peak by √2; DC/Nyquist use edge scaling. Phase is relative to the first retained sample; negligible-bin phases are unstable. dB floor clips only the display; zero bins are −∞.</p><p>Finite sampling can alias signals. Hann windowing spreads tone energy into neighboring bins; off-bin tones also have scalloping loss. These tone-scaled bins do not establish broadband RMS, noise density, distortion qualification or small-signal AC response.</p></details>
      <div className="es-row"><Button size="sm" variant="ghost" onClick={() => void download('csv')}>Export spectrum CSV</Button><Button size="sm" variant="ghost" onClick={() => void download('json')}>Export spectrum evidence</Button></div>
    </section>}
  </details>;
}

export function SpectrumPlot({data,first,last,selected,scale,floor}: {data: FrequencySpectrum;first:number;last:number;selected:number;scale:'peak'|'dbv';floor:number}) {
  const bins=data.bins.slice(first,last+1), values=bins.map((b) => scale === 'peak' ? b.peak_v : b.dbv_rms).filter((v):v is number => v !== null && Number.isFinite(v));
  if (!values.length) return <p>All displayed bins are zero: −∞ dBV. Use peak-amplitude view to inspect zeros.</p>;
  const low=scale === 'peak' ? 0 : floor, top=Math.max(...values), high=top > low ? top : low+1;
  const min=data.bins[first]!.frequency_hz,max=data.bins[last]!.frequency_hz;
  const x=(v:number) => min===max ? 400 : 75+650*(v-min)/(max-min);
  const y=(v:number) => 220-185*Math.max(0,Math.min(1,(v-low)/(high-low)));
  const path=bins.map((b) => {const v=scale === 'peak' ? b.peak_v : b.dbv_rms;return v===null || v<low ? '' : `M${x(b.frequency_hz)},220V${y(v)}`;}).join(' ');
  const b=data.bins[selected],value=b ? scale === 'peak' ? b.peak_v : b.dbv_rms : null;
  return <svg className="wb-sweep-plot" viewBox="0 0 800 275" role="img" aria-label={`Native frequency spectrum in ${scale === 'peak' ? 'peak volts' : 'RMS-bin dBV'}`}><title>Discrete one-sided FFT bins. Display clipping does not alter exported amplitudes.</title>{[0,.25,.5,.75,1].map((t) => <g key={t}><line x1={75} x2={725} y1={35+185*t} y2={35+185*t} className="wb-sweep-grid" /><text x={65} y={40+185*t} textAnchor="end">{scale === 'peak' ? formatSi(high*(1-t),'V') : `${(high*(1-t)+low*t).toFixed(1)} dBV`}</text><text x={75+650*t} y={243} textAnchor="middle">{min!==max || t===.5 ? formatSi(min*(1-t)+max*t,'Hz') : ''}</text></g>)}<path d={path} fill="none" stroke="var(--accent)" strokeWidth={1.5} />{b && selected>=first && selected<=last && value!==null && value>=low && <circle cx={x(b.frequency_hz)} cy={y(value)} r={4} fill="var(--info)" />}<text x={400} y={269} textAnchor="middle">Frequency · discrete bins · {scale==='peak' ? 'V peak' : 'dBV referenced to 1 V RMS bin'}</text></svg>;
}
