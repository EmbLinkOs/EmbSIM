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
