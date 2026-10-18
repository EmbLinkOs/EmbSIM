// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A part's artwork (SPEC-005 §8) as the top face of its body on the 3D
// bench: the SVG with its states applied (an LCD's characters, a board's
// LEDs), drawn into a canvas texture again when an output it shows
// changes; its hotspots found by where the pointer meets the face.

import type { ArtworkState, Hotspot, OutputValue } from '@embsim/protocol';
import { applyState } from '@embsim/scene';
import * as THREE from 'three';

/** pixels per millimetre of the texture: a 0.1 in pitch is 25 px */
const PX_PER_MM = 10;
const MAX_PX = 2048;

export class ArtworkSurface {
  readonly texture: THREE.CanvasTexture;
  private readonly canvas: HTMLCanvasElement;
  private readonly root: SVGSVGElement | null;
  private readonly states: ArtworkState[];
  private readonly hotspots: Hotspot[];
  private readonly viewBox: [number, number, number, number];
  private last = '';
  private drawing = false;
  private dirty = false;
  private boxes: { hotspot: Hotspot; box: [number, number, number, number] }[] | null = null;
  /** called when a new picture is in the texture */
  onDraw?: () => void;

  constructor(svg: string, widthMm: number, heightMm: number, states: ArtworkState[] = [], hotspots: Hotspot[] = [], vars?: Record<string, string>) {
    this.states = states;
    this.hotspots = hotspots;
    this.canvas = document.createElement('canvas');
    const scale = Math.min(PX_PER_MM, MAX_PX / Math.max(widthMm, heightMm));
    this.canvas.width = Math.max(2, Math.round(widthMm * scale));
    this.canvas.height = Math.max(2, Math.round(heightMm * scale));
    this.texture = new THREE.CanvasTexture(this.canvas);
    this.texture.colorSpace = THREE.SRGBColorSpace;
    this.texture.anisotropy = 8;
    const doc = new DOMParser().parseFromString(svg, 'image/svg+xml');
    const root = doc.documentElement as unknown as SVGSVGElement;
    this.root = root && root.nodeName.toLowerCase() === 'svg' ? root : null;
    const vb = this.root?.getAttribute('viewBox')?.trim().split(/[\s,]+/).map(Number);
    this.viewBox = vb && vb.length === 4 && vb.every(Number.isFinite) ? [vb[0]!, vb[1]!, vb[2]!, vb[3]!] : [0, 0, widthMm, heightMm];
    if (this.root) {
      this.root.setAttribute('width', String(this.canvas.width));
      this.root.setAttribute('height', String(this.canvas.height));
      this.root.setAttribute('preserveAspectRatio', 'none');
      for (const [k, v] of Object.entries(vars ?? {})) this.root.style.setProperty(k, v);
    }
    this.draw();
  }

  /** applies the outputs' states; draws again when the picture changed */
  update(outputs: Record<string, OutputValue>): void {
    if (!this.root || this.states.length === 0) return;
    const key = JSON.stringify(this.states.map((s) => outputs[s.output] ?? null));
    if (key === this.last) return;
    this.last = key;
    for (const s of this.states) applyState(this.root, s, outputs[s.output]);
    this.draw();
  }

  private draw(): void {
    if (!this.root || typeof Image === 'undefined') return;
    if (this.drawing) {
      this.dirty = true;
      return;
    }
    this.drawing = true;
    const xml = new XMLSerializer().serializeToString(this.root);
    const img = new Image();
    img.onload = () => {
      const g = this.canvas.getContext('2d');
      if (g) {
        g.clearRect(0, 0, this.canvas.width, this.canvas.height);
        g.drawImage(img, 0, 0, this.canvas.width, this.canvas.height);
        this.texture.needsUpdate = true;
        this.onDraw?.();
      }
      this.finish();
    };
    img.onerror = () => this.finish();
    img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(xml)}`;
  }

  private finish(): void {
    this.drawing = false;
    if (this.dirty) {
      this.dirty = false;
      this.draw();
    }
  }

  /** the hotspot under a point of the face, u and v from its top-left corner (0..1) */
  hotspotAt(u: number, v: number): Hotspot | undefined {
    const boxes = this.measure();
    const x = this.viewBox[0] + u * this.viewBox[2];
    const y = this.viewBox[1] + v * this.viewBox[3];
    return boxes.find(({ box: [x0, y0, x1, y1] }) => x >= x0 && x <= x1 && y >= y0 && y <= y1)?.hotspot;
  }

  /** the hotspots' boxes in the artwork's units, measured once in a hidden copy */
  private measure(): { hotspot: Hotspot; box: [number, number, number, number] }[] {
    if (this.boxes) return this.boxes;
    this.boxes = [];
    if (!this.root || this.hotspots.length === 0 || typeof document === 'undefined') return this.boxes;
    const host = document.createElement('div');
    host.style.cssText = `position:fixed;left:-100000px;top:0;visibility:hidden;width:${this.viewBox[2]}px;height:${this.viewBox[3]}px`;
    const copy = this.root.cloneNode(true) as SVGSVGElement;
    copy.setAttribute('width', String(this.viewBox[2]));
    copy.setAttribute('height', String(this.viewBox[3]));
    host.appendChild(copy);
    document.body.appendChild(host);
    try {
      const origin = copy.getBoundingClientRect();
      for (const h of this.hotspots) {
        const el = copy.querySelector(h.selector);
        if (!el) continue;
        const r = el.getBoundingClientRect();
        if (!r.width && !r.height) continue;
        const x0 = this.viewBox[0] + (r.left - origin.left);
        const y0 = this.viewBox[1] + (r.top - origin.top);
        this.boxes.push({ hotspot: h, box: [x0, y0, x0 + r.width, y0 + r.height] });
      }
    } finally {
      host.remove();
    }
    return this.boxes;
  }

  dispose(): void {
    this.texture.dispose();
  }
}
