// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { canvasBackground, type CanvasPalette, type GridStyle } from '@embsim/scene';
import { useId } from 'react';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
const styles: { id: GridStyle; name: string }[] = [
  { id: 'engineering', name: 'Engineering' }, { id: 'lines', name: 'Lines' },
  { id: 'dots', name: 'Dots' }, { id: 'crosses', name: 'Crosses' }, { id: 'blank', name: 'Plain' },
];
const palettes: { id: CanvasPalette; name: string }[] = [
  { id: 'paper', name: 'Paper' }, { id: 'warm', name: 'Warm' }, { id: 'graphite', name: 'Graphite' }, { id: 'blueprint', name: 'Blueprint' },
];
export function CanvasAppearanceControls() {
  const { appearance, update, reset } = useCanvasAppearance();
  const id = useId();
  return (
    <section className="wb-canvas-appearance" aria-label="Canvas appearance">
      <div className="wb-canvas-appearance__heading"><span>DRAWING & BACKGROUND</span><Button size="sm" variant="ghost" onClick={reset}>Reset appearance</Button></div>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-wires`}>Wire color</label><select id={`${id}-wires`} value={appearance.wireColor} onChange={(e) => update({ wireColor: e.target.value as typeof appearance.wireColor })}>
        <option value="green">Engineering green</option><option value="blue">Blueprint blue</option><option value="teal">Teal</option><option value="violet">Violet</option>
      </select></div>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-labels`}>Component labels</label><select id={`${id}-labels`} value={appearance.labels} onChange={(e) => update({ labels: e.target.value as typeof appearance.labels })}>
        <option value="detailed">Reference, name and value</option><option value="compact">Reference and value</option>
      </select></div>
      <p>Wire color applies while designing. Live signal colors show electrical state.</p>
      <fieldset><legend>Grid pattern</legend><div className="wb-canvas-appearance__patterns">
        {styles.map((style) => <label key={style.id} className={appearance.style === style.id ? 'is-active' : ''}>
          <input className="es-sr-only" type="radio" name={`${id}-pattern`} value={style.id} checked={appearance.style === style.id} onChange={() => update({ style: style.id })} />
          <span className={`wb-canvas-appearance__sample es-scene--${appearance.palette}`} style={canvasBackground({ ...appearance, style: style.id, contrast: .3 }, { x: 0, y: 0, k: .8 })} aria-hidden="true" />
          <span>{style.name}</span>
        </label>)}
      </div></fieldset>
      <fieldset><legend>Surface</legend><div className="wb-canvas-appearance__palettes">
        {palettes.map((palette) => <label key={palette.id} className={appearance.palette === palette.id ? 'is-active' : ''}>
          <input className="es-sr-only" type="radio" name={`${id}-palette`} value={palette.id} checked={appearance.palette === palette.id} onChange={() => update({ palette: palette.id })} />
          <span className={`wb-canvas-appearance__swatch es-scene--${palette.id}`} aria-hidden="true" /><span>{palette.name}</span>
        </label>)}
      </div></fieldset>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-spacing`}>Grid spacing</label><select id={`${id}-spacing`} value={appearance.spacing} onChange={(e) => update({ spacing: Number(e.target.value) as 10 | 20 | 40 })}>
        <option value="10">Fine · 10 units</option><option value="20">Standard · 20 units</option><option value="40">Relaxed · 40 units</option>
      </select></div>
      <label className="wb-canvas-appearance__range" htmlFor={`${id}-weight`}><span>Line / dot size <output>{appearance.weight.toFixed(1)} px</output></span><input id={`${id}-weight`} aria-label="Line / dot size" type="range" min="0.5" max="2" step="0.1" value={appearance.weight} onChange={(e) => update({ weight: Number(e.target.value) })} /></label>
      <label className="wb-canvas-appearance__range" htmlFor={`${id}-contrast`}><span>Grid contrast <output>{Math.round(appearance.contrast * 100)}%</output></span><input id={`${id}-contrast`} aria-label="Grid contrast" type="range" min="0.05" max="0.5" step="0.01" value={appearance.contrast} onChange={(e) => update({ contrast: Number(e.target.value) })} /></label>
      <p>Appearance is saved on this device. Editing snap stays at 10 units.</p>
    </section>
  );
}
