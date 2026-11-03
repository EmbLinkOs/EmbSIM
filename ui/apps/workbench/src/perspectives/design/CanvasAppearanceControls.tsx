// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Button } from '@embsim/design';
import { canvasBackground, type CanvasPalette, type GridStyle } from '@embsim/scene';
import { useId } from 'react';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { t as tr } from '../../i18n';
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
    <section className="wb-canvas-appearance" aria-label={tr('appearance.region')}>
      <div className="wb-canvas-appearance__heading"><span>{tr('appearance.drawing')}</span><Button size="sm" variant="ghost" onClick={reset}>{tr('appearance.reset')}</Button></div>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-wires`}>{tr('appearance.wireColor')}</label><select id={`${id}-wires`} value={appearance.wireColor} onChange={(e) => update({ wireColor: e.target.value as typeof appearance.wireColor })}>
        <option value="green">{tr('appearance.green')}</option><option value="blue">{tr('appearance.blueprint')}</option><option value="teal">{tr('appearance.teal')}</option><option value="violet">{tr('appearance.violet')}</option>
      </select></div>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-labels`}>{tr('appearance.labels')}</label><select id={`${id}-labels`} value={appearance.labels} onChange={(e) => update({ labels: e.target.value as typeof appearance.labels })}>
        <option value="detailed">{tr('appearance.refNameValue')}</option><option value="compact">{tr('appearance.refValue')}</option>
      </select></div>
      <p>{tr('appearance.wireHint')}</p>
      <fieldset><legend>{tr('appearance.pattern')}</legend><div className="wb-canvas-appearance__patterns">
        {styles.map((style) => <label key={style.id} className={appearance.style === style.id ? 'is-active' : ''}>
          <input className="es-sr-only" type="radio" name={`${id}-pattern`} value={style.id} checked={appearance.style === style.id} onChange={() => update({ style: style.id })} />
          <span className={`wb-canvas-appearance__sample es-scene--${appearance.palette}`} style={canvasBackground({ ...appearance, style: style.id, contrast: .3 }, { x: 0, y: 0, k: .8 })} aria-hidden="true" />
          <span>{style.name}</span>
        </label>)}
      </div></fieldset>
      <fieldset><legend>{tr('appearance.surface')}</legend><div className="wb-canvas-appearance__palettes">
        {palettes.map((palette) => <label key={palette.id} className={appearance.palette === palette.id ? 'is-active' : ''}>
          <input className="es-sr-only" type="radio" name={`${id}-palette`} value={palette.id} checked={appearance.palette === palette.id} onChange={() => update({ palette: palette.id })} />
          <span className={`wb-canvas-appearance__swatch es-scene--${palette.id}`} aria-hidden="true" /><span>{palette.name}</span>
        </label>)}
      </div></fieldset>
      <div className="wb-canvas-appearance__field"><label htmlFor={`${id}-spacing`}>{tr('appearance.spacing')}</label><select id={`${id}-spacing`} value={appearance.spacing} onChange={(e) => update({ spacing: Number(e.target.value) as 10 | 20 | 40 })}>
        <option value="10">{tr('appearance.fine')}</option><option value="20">{tr('appearance.standard')}</option><option value="40">{tr('appearance.relaxed')}</option>
      </select></div>
      <label className="wb-canvas-appearance__range" htmlFor={`${id}-weight`}><span>Line / dot size <output>{appearance.weight.toFixed(1)} px</output></span><input id={`${id}-weight`} aria-label={tr('appearance.lineSize')} type="range" min="0.5" max="2" step="0.1" value={appearance.weight} onChange={(e) => update({ weight: Number(e.target.value) })} /></label>
      <label className="wb-canvas-appearance__range" htmlFor={`${id}-contrast`}><span>Grid contrast <output>{Math.round(appearance.contrast * 100)}%</output></span><input id={`${id}-contrast`} aria-label={tr('appearance.contrast')} type="range" min="0.05" max="0.5" step="0.01" value={appearance.contrast} onChange={(e) => update({ contrast: Number(e.target.value) })} /></label>
      <p>{tr('appearance.saved')}</p>
    </section>
  );
}
