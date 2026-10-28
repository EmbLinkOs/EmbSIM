// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import type { ArtworkState, OutputValue } from '@embsim/protocol';

/** Sets an attribute, or the element's text for the pseudo-attribute "text"; an empty value removes the attribute. */
function setValue(el: Element, attr: string, text: string): void {
  if (!['text', 'fill', 'stroke', 'opacity', 'fill-opacity', 'stroke-opacity', 'stroke-width', 'transform', 'd', 'x', 'y', 'width', 'height', 'visibility', 'display', 'r', 'cx', 'cy', 'points', 'textLength'].includes(attr)) return;
  if (/url\s*\(|javascript:/i.test(text)) return;
  if (attr === 'text') el.textContent = text;
  else if (text === '') { el.removeAttribute(attr); (el as SVGElement).style.removeProperty(attr); }
  else {
    el.setAttribute(attr, text);
    // Inline presentation must take precedence over a catalog class rule.
    if (['fill', 'stroke', 'opacity', 'fill-opacity', 'stroke-opacity', 'stroke-width'].includes(attr)) (el as SVGElement).style.setProperty(attr, text);
  }
}

/**
 * Applies one artwork state (SPEC-005 §8): a class toggled by truth; an
 * enum or integer looked up in `map`; a value put into the `format`
 * template ("rotate({} 30 30)", "{}" for a colour, a path or text), after
 * `scale` for numbers; a number scaled into the attribute; a number as
 * opacity; otherwise `true`/`false` strings.
 */
export function applyState(root: Element, state: ArtworkState, value: OutputValue | undefined): void {
  let el: Element | undefined;
  try { el = root.querySelectorAll(state.selector)[0]; } catch { return; }
  if (!el) return;
  if (state.class) {
    if (/\s/.test(state.class)) return;
    el.classList.toggle(state.class, Boolean(value));
    return;
  }
  const attr = state.attr ?? 'opacity';
  if (state.map && value !== undefined) {
    const m = state.map[String(value)];
    if (m !== undefined) {
      setValue(el, attr, m);
      return;
    }
  }
  let v: OutputValue | undefined = value;
  if (typeof value === 'number' && state.scale) {
    const [lo, hi] = Array.isArray(state.scale) ? state.scale : state.scale.from;
    const [a, b] = Array.isArray(state.scale) ? [0, 1] : (state.scale.to ?? [0, 1]);
    const t = hi === lo ? 0 : Math.max(0, Math.min(1, (value - lo) / (hi - lo)));
    v = a + t * (b - a);
    if (state.format === undefined) {
      setValue(el, attr, attr === 'opacity' ? v.toFixed(3) : String(v));
      return;
    }
  }
  if (state.format !== undefined) {
    const text = typeof v === 'number' && Number.isFinite(v) && state.precision !== undefined ? v.toFixed(Math.max(0, Math.min(6, state.precision))) : String(v);
    setValue(el, attr, value === undefined ? '' : state.format.split('{}').join(text));
    return;
  }
  if (typeof value === 'number' && attr === 'opacity') {
    el.setAttribute(attr, Math.max(0, Math.min(1, value)).toFixed(3));
    return;
  }
  setValue(el, attr, value ? state.true ?? '1' : state.false ?? '');
}
