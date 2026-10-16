// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Parses catalog SVG strings once and caches their inner markup and viewBox
// so symbols can be inlined into the scene graph as <g> elements.

export interface ParsedSvg {
  inner: string;
  viewBox: { x: number; y: number; width: number; height: number } | null;
  width: number | null;
  height: number | null;
}

const cache = new Map<string, ParsedSvg>();

const FORBIDDEN = /<\s*(script|foreignObject|iframe|object|embed)\b/i;

/** Parses an SVG document string; scripts and foreign content are rejected (SPEC-005 §8: no embedded raster or script). */
export function parseSvg(source: string): ParsedSvg {
  const hit = cache.get(source);
  if (hit) return hit;
  let parsed: ParsedSvg = { inner: '', viewBox: null, width: null, height: null };
  if (!FORBIDDEN.test(source) && typeof DOMParser !== 'undefined') {
    const doc = new DOMParser().parseFromString(source, 'image/svg+xml');
    const root = doc.documentElement;
    if (root && root.nodeName.toLowerCase() === 'svg' && !doc.querySelector('parsererror')) {
      const vb = root.getAttribute('viewBox');
      let viewBox: ParsedSvg['viewBox'] = null;
      if (vb) {
        const [x = 0, y = 0, w = 0, h = 0] = vb.split(/[\s,]+/).map(Number);
        viewBox = { x, y, width: w, height: h };
      }
      // Strip event handler attributes defensively.
      for (const el of Array.from(root.querySelectorAll('*'))) {
        for (const attr of Array.from(el.attributes)) {
          if (/^on/i.test(attr.name) || (attr.name === 'href' && /^\s*javascript:/i.test(attr.value))) el.removeAttribute(attr.name);
        }
      }
      parsed = {
        inner: root.innerHTML,
        viewBox,
        width: root.getAttribute('width') ? Number(root.getAttribute('width')) : null,
        height: root.getAttribute('height') ? Number(root.getAttribute('height')) : null,
      };
    }
  }
  cache.set(source, parsed);
  return parsed;
}

const scoped = new Map<string, string>();

/** Prefixes a stylesheet's selectors with `scope` (`.pcb{...}` becomes `[data-board-item="B1"] .pcb{...}`);
 * at-rules are kept as they are. */
export function scopeCss(css: string, scope: string): string {
  return css.replace(/([^{}]+)\{([^{}]*)\}/g, (all, sel: string, body: string) => {
    const s = sel.trim();
    if (!s || s.startsWith('@')) return all;
    const prefixed = s
      .split(',')
      .map((x) => `${scope} ${x.trim()}`)
      .join(', ');
    return `${prefixed}{${body}}`;
  });
}

/**
 * Inline SVG shares one document: two artworks with the same class names or
 * gradient ids would restyle each other. This scopes `inner` to the element
 * carrying `data-board-item="id"`: its <style> rules apply only inside it, and
 * ids that url(#..) or href="#.." references point at (gradients, clip paths)
 * are renamed. Other ids stay, since hotspots select them.
 */
export function scopeArtwork(inner: string, id: string): string {
  const key = `${id}\u0000${inner}`;
  const hit = scoped.get(key);
  if (hit !== undefined) return hit;
  const scope = `[data-board-item="${id.replace(/["\\]/g, '\\$&')}"]`;
  const prefix = `${id.replace(/[^A-Za-z0-9_-]/g, '_')}--`;
  const refs = new Set<string>();
  for (const m of inner.matchAll(/url\(\s*#([A-Za-z_][\w.-]*)\s*\)|href="#([A-Za-z_][\w.-]*)"/g)) refs.add((m[1] ?? m[2])!);
  let out = inner.replace(/(<style[^>]*>)([\s\S]*?)(<\/style>)/g, (_all, a: string, css: string, b: string) => a + scopeCss(css, scope) + b);
  for (const r of refs) {
    const esc = r.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    out = out
      .replace(new RegExp(`id="${esc}"`, 'g'), `id="${prefix}${r}"`)
      .replace(new RegExp(`url\\(\\s*#${esc}\\s*\\)`, 'g'), `url(#${prefix}${r})`)
      .replace(new RegExp(`href="#${esc}"`, 'g'), `href="#${prefix}${r}"`);
  }
  scoped.set(key, out);
  return out;
}
