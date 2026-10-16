// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// SchematicCanvas: the SVG scene graph of SPEC-011 §3. One viewport group
// carries pan and zoom; layers are grid, wires, components, labels,
// selection and probes. Components outside the viewport are not mounted.
// Editing callbacks describe intents (move, wire, delete); the owner turns
// them into project transactions and feeds the new project back in.

import type { Layout, NetState, ProbeInfo, Project, SymbolDescription, Wire } from '@embsim/protocol';
import { memo, useCallback, useEffect, useImperativeHandle, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as ReactPointerEvent, type Ref } from 'react';
import { ComponentSymbol } from './ComponentSymbol';
import {
  GRID,
  distanceToSegment,
  findJunctions,
  nearestWirePoint,
  pinPosition,
  rectContainsRect,
  rectsIntersect,
  routeOrthogonal,
  snap,
  snapPoint,
  symbolBounds,
  wireSegments,
  type Point,
  type Rect,
} from './geometry';

export interface SceneSymbol {
  name: string;
  symbol: SymbolDescription;
  svg: string;
  /** The parameter shown as the value label (e.g. "resistance"). */
  valueParam?: string;
}

export type SceneCatalog = Record<string, SceneSymbol>;

export type CanvasMode = 'select' | 'wire' | 'probe';

export interface Selection {
  components: string[];
  wires: number[];
}

export const EMPTY_SELECTION: Selection = { components: [], wires: [] };

export interface PinRef {
  component: string;
  pin: string;
  net?: string;
  point: Point;
}

export interface NetRef {
  net: string;
  point: Point;
}

export interface Viewport {
  x: number;
  y: number;
  k: number;
}

export interface SchematicCanvasHandle {
  zoomToFit(): void;
  zoomBy(factor: number): void;
  resetZoom(): void;
  focus(): void;
  getViewport(): Viewport;
}

export interface SchematicCanvasProps {
  project: Project;
  catalog: SceneCatalog;
  mode: CanvasMode;
  selection: Selection;
  onSelectionChange: (selection: Selection) => void;
  /** Colour nets by state and show voltages (Run perspective). */
  live?: boolean;
  netStates?: Record<string, NetState>;
  /** Voltage range for the analog gradient; default [0, 3.3]. */
  analogRange?: [number, number];
  probes?: ProbeInfo[];
  readOnly?: boolean;
  onMove?: (ids: string[], dx: number, dy: number) => void;
  onRotate?: (ids: string[]) => void;
  onMirror?: (ids: string[]) => void;
  onDelete?: (selection: Selection) => void;
  onWire?: (from: PinRef, to: PinRef | NetRef, points: [number, number][]) => void;
  onPlace?: (componentId: string, at: Point) => void;
  /** A net was clicked in probe mode (or with Alt): add a probe there. */
  onNetClick?: (net: string, at: Point) => void;
  onRenameNet?: (net: string) => void;
  onOpenComponent?: (id: string) => void;
  handle?: Ref<SchematicCanvasHandle>;
  className?: string;
  /** Accessible name of the canvas. */
  label?: string;
}

const MIN_ZOOM = 0.15;
const MAX_ZOOM = 10;
const DEFAULT_SHEET = { width: 1600, height: 1000 };

export type NetVisual = { kind: 'low' | 'high' | 'z' | 'x' | 'analog' | 'idle'; color?: string; text: string };

export function describeNetState(state: NetState | undefined, range: [number, number]): NetVisual {
  if (!state) return { kind: 'idle', text: 'no state' };
  if (state.contention) return { kind: 'x', text: 'contention' };
  if (state.logic === 'X') return { kind: 'x', text: 'X (unknown)' };
  if (state.logic === 'Z') return { kind: 'z', text: 'Z (floating)' };
  if (state.logic === '0' || state.logic === '1') {
    const kind = state.logic === '0' ? 'low' : 'high';
    const v = state.voltage !== undefined ? ` · ${state.voltage.toFixed(3)} V` : '';
    return { kind, text: `${kind} (${state.logic})${v}` };
  }
  if (state.voltage !== undefined) {
    return { kind: 'analog', color: analogColor(state.voltage, range), text: `${state.voltage.toFixed(3)} V` };
  }
  return { kind: 'idle', text: 'no state' };
}

/** Interpolates the analog gradient colour for a voltage with CSS color-mix so it follows the theme. */
export function analogColor(v: number, [min, max]: [number, number]): string {
  const t = max === min ? 0 : Math.max(0, Math.min(1, (v - min) / (max - min)));
  return `color-mix(in oklab, var(--signal-analog-min), var(--signal-analog-max) ${Math.round(t * 100)}%)`;
}

export function AnalogLegend({ range = [0, 3.3] }: { range?: [number, number] }) {
  return (
    <div className="es-scene-legend" aria-label={`Analog voltage legend, ${range[0]} to ${range[1]} volts`}>
      <span className="es-scene-legend__value">{range[0].toFixed(1)} V</span>
      <span className="es-scene-legend__bar" aria-hidden="true" />
      <span className="es-scene-legend__value">{range[1].toFixed(1)} V</span>
      <span className="es-scene-legend__swatch es-scene-legend__swatch--low">0</span>
      <span className="es-scene-legend__swatch es-scene-legend__swatch--high">1</span>
      <span className="es-scene-legend__swatch es-scene-legend__swatch--z">Z</span>
      <span className="es-scene-legend__swatch es-scene-legend__swatch--x">X</span>
    </div>
  );
}

function formatValue(v: unknown): string {
  if (typeof v === 'number') {
    if (Math.abs(v) >= 1e6) return `${v / 1e6}M`;
    if (Math.abs(v) >= 1e3) return `${v / 1e3}k`;
    if (Math.abs(v) < 1e-3 && v !== 0) return `${(v * 1e6).toPrecision(3)}µ`;
    return String(v);
  }
  return String(v ?? '');
}

interface PlacedComponent {
  id: string;
  componentId: string;
  layout: Layout;
  symbol: SceneSymbol | undefined;
  bounds: Rect;
  pins: { name: string; point: Point; net: string | undefined }[];
  value: string;
}

export function SchematicCanvas(props: SchematicCanvasProps) {
  const {
    project,
    catalog,
    mode,
    selection,
    onSelectionChange,
    live,
    netStates,
    analogRange = [0, 3.3],
    probes,
    readOnly,
    onMove,
    onRotate,
    onMirror,
    onDelete,
    onWire,
    onPlace,
    onNetClick,
    onRenameNet,
    onOpenComponent,
    handle,
    className,
    label = 'Schematic',
  } = props;

  const svgRef = useRef<SVGSVGElement>(null);
  const sceneRef = useRef<SVGGElement>(null);
  const vpRef = useRef<Viewport>({ x: 40, y: 40, k: 1 });
  const [viewport, setViewportState] = useState<Viewport>(vpRef.current);
  const [size, setSize] = useState({ width: 800, height: 600 });
  const [hover, setHover] = useState<{ text: string; sub?: string; x: number; y: number } | null>(null);
  const [dragOffset, setDragOffset] = useState<Point | null>(null);
  const [marquee, setMarquee] = useState<Rect | null>(null);
  const [wiring, setWiring] = useState<{ from: PinRef; cursor: Point; horizontalFirst: boolean } | null>(null);
  const [dropHint, setDropHint] = useState<Point | null>(null);
  const [announcement, setAnnouncement] = useState('');
  const rafRef = useRef<number | null>(null);

  const sheet = project.layout?.sheet ?? DEFAULT_SHEET;
  const wires = useMemo(() => project.layout?.wires ?? [], [project.layout?.wires]);
  const labels = useMemo(() => project.layout?.labels ?? [], [project.layout?.labels]);

  const pinNet = useMemo(() => {
    const map = new Map<string, string>();
    for (const [net, pins] of Object.entries(project.nets)) for (const p of pins) map.set(p, net);
    return map;
  }, [project.nets]);

  const placed = useMemo<PlacedComponent[]>(() => {
    // boards are drawn as blocks of their ports, beside the components
    const boards = (project.boards ?? []).map((b) => ({ id: b.id, component: b.board, params: undefined, layout: b.layout }));
    return [...project.components, ...boards].map((c) => {
      const symbol = catalog[c.component];
      const layout = c.layout ?? { x: 0, y: 0 };
      const sym = symbol?.symbol ?? { svg: '', width: 60, height: 20, pins: {} };
      const pins = Object.keys(sym.pins).map((name) => ({
        name,
        point: pinPosition(layout, sym, name) ?? { x: layout.x, y: layout.y },
        net: pinNet.get(`${c.id}.${name}`),
      }));
      const valueParam = symbol?.valueParam;
      const value = valueParam && c.params?.[valueParam] !== undefined ? formatValue(c.params[valueParam]) : '';
      return { id: c.id, componentId: c.component, layout, symbol, bounds: symbolBounds(layout, sym, 4), pins, value };
    });
  }, [project.components, project.boards, catalog, pinNet]);

  const placedById = useMemo(() => new Map(placed.map((p) => [p.id, p])), [placed]);

  const pinsByNet = useMemo(() => {
    const out: Record<string, Point[]> = {};
    for (const p of placed) for (const pin of p.pins) if (pin.net) (out[pin.net] ??= []).push(pin.point);
    return out;
  }, [placed]);

  const junctions = useMemo(() => findJunctions(wires, pinsByNet), [wires, pinsByNet]);

  const ratsnest = useMemo(() => {
    const lines: { net: string; a: Point; b: Point }[] = [];
    for (const [net, points] of Object.entries(pinsByNet)) {
      const netWires = wires.filter((w) => w.net === net);
      for (const p of points) {
        const touched = netWires.some((w) => wireSegments(w).some(([a, b]) => distanceToSegment(p, a, b) < 0.01));
        if (touched) continue;
        const target = netWires.length ? nearestWirePoint(p, netWires) : points.find((q) => q !== p) ?? null;
        if (target && (target.x !== p.x || target.y !== p.y)) lines.push({ net, a: p, b: target });
      }
    }
    return lines;
  }, [pinsByNet, wires]);

  // --- viewport -----------------------------------------------------------

  const setViewport = useCallback((next: Viewport, commit = true) => {
    vpRef.current = next;
    sceneRef.current?.setAttribute('transform', `translate(${next.x} ${next.y}) scale(${next.k})`);
    if (commit) {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
      rafRef.current = requestAnimationFrame(() => {
        rafRef.current = null;
        setViewportState(vpRef.current);
      });
    }
  }, []);

  const toWorld = useCallback((clientX: number, clientY: number): Point => {
    const rect = svgRef.current?.getBoundingClientRect();
    const vp = vpRef.current;
    return { x: (clientX - (rect?.left ?? 0) - vp.x) / vp.k, y: (clientY - (rect?.top ?? 0) - vp.y) / vp.k };
  }, []);

  const zoomAt = useCallback(
    (factor: number, anchor?: Point) => {
      const vp = vpRef.current;
      const k = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, vp.k * factor));
      const a = anchor ?? { x: size.width / 2, y: size.height / 2 };
      const wx = (a.x - vp.x) / vp.k;
      const wy = (a.y - vp.y) / vp.k;
      setViewport({ x: a.x - wx * k, y: a.y - wy * k, k });
    },
    [setViewport, size],
  );

  const zoomToFit = useCallback(() => {
    const rects = placed.map((p) => p.bounds);
    for (const w of wires) for (const [x, y] of w.points) rects.push({ x, y, width: 0, height: 0 });
    if (rects.length === 0) {
      setViewport({ x: 40, y: 40, k: 1 });
      return;
    }
    const minX = Math.min(...rects.map((r) => r.x)) - 40;
    const minY = Math.min(...rects.map((r) => r.y)) - 40;
    const maxX = Math.max(...rects.map((r) => r.x + r.width)) + 40;
    const maxY = Math.max(...rects.map((r) => r.y + r.height)) + 40;
    const k = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.min(size.width / (maxX - minX), size.height / (maxY - minY))));
    setViewport({ x: (size.width - (maxX - minX) * k) / 2 - minX * k, y: (size.height - (maxY - minY) * k) / 2 - minY * k, k });
  }, [placed, wires, size, setViewport]);

  useImperativeHandle(
    handle,
    () => ({
      zoomToFit,
      zoomBy: (f) => zoomAt(f),
      resetZoom: () => setViewport({ x: 40, y: 40, k: 1 }),
      focus: () => svgRef.current?.focus(),
      getViewport: () => vpRef.current,
    }),
    [zoomToFit, zoomAt, setViewport],
  );

  useEffect(() => {
    const el = svgRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      const r = entries[0]?.contentRect;
      if (r && r.width > 0 && r.height > 0) setSize({ width: r.width, height: r.height });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const fittedRef = useRef(false);
  useEffect(() => {
    if (!fittedRef.current && placed.length > 0 && size.width > 0) {
      fittedRef.current = true;
      zoomToFit();
    }
  }, [placed.length, size.width, zoomToFit]);

  useEffect(() => {
    const el = svgRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      e.preventDefault();
      const rect = el.getBoundingClientRect();
      if (e.shiftKey && !e.ctrlKey) {
        const vp = vpRef.current;
        setViewport({ ...vp, x: vp.x - e.deltaY, y: vp.y - e.deltaX });
        return;
      }
      const factor = Math.exp(-e.deltaY * (e.ctrlKey ? 0.01 : 0.0015));
      zoomAt(factor, { x: e.clientX - rect.left, y: e.clientY - rect.top });
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [zoomAt, setViewport]);

  // --- hit testing ----------------------------------------------------------

  const findPinAt = useCallback(
    (p: Point, tolerance = 6): PinRef | null => {
      let best: PinRef | null = null;
      let bestD = tolerance;
      for (const c of placed) {
        for (const pin of c.pins) {
          const d = Math.hypot(pin.point.x - p.x, pin.point.y - p.y);
          if (d < bestD) {
            bestD = d;
            best = { component: c.id, pin: pin.name, net: pin.net, point: pin.point };
          }
        }
      }
      return best;
    },
    [placed],
  );

  const findWireAt = useCallback(
    (p: Point, tolerance = 5): { index: number; wire: Wire; point: Point } | null => {
      for (let i = 0; i < wires.length; i++) {
        const w = wires[i]!;
        for (const [a, b] of wireSegments(w)) {
          if (distanceToSegment(p, a, b) <= tolerance) {
            const point = a.x === b.x ? { x: a.x, y: snap(p.y) } : { x: snap(p.x), y: a.y };
            return { index: i, wire: w, point };
          }
        }
      }
      return null;
    },
    [wires],
  );

  // --- wiring -------------------------------------------------------------------

  const startWire = useCallback((from: PinRef) => {
    setWiring({ from, cursor: from.point, horizontalFirst: true });
    setAnnouncement(`Wiring from ${from.component}.${from.pin}. Choose a destination pin, or press Escape.`);
  }, []);

  const finishWire = useCallback(
    (to: PinRef | NetRef) => {
      if (!wiring) return;
      const points = routeOrthogonal(wiring.from.point, to.point, wiring.horizontalFirst);
      setWiring(null);
      if ('component' in to && to.component === wiring.from.component && to.pin === wiring.from.pin) return;
      onWire?.(wiring.from, to, points);
      setAnnouncement('component' in to ? `Wired ${wiring.from.component}.${wiring.from.pin} to ${to.component}.${to.pin}.` : `Wired ${wiring.from.component}.${wiring.from.pin} to net ${to.net}.`);
    },
    [wiring, onWire],
  );

  const onPinActivate = useCallback(
    (pin: PinRef) => {
      if (readOnly) return;
      if (wiring) finishWire(pin);
      else startWire(pin);
    },
    [readOnly, wiring, finishWire, startWire],
  );

  // --- pointer handling on the svg -----------------------------------------

  const gesture = useRef<
    | { kind: 'pan'; startX: number; startY: number; vp: Viewport; moved: boolean }
    | { kind: 'move'; ids: string[]; start: Point; moved: boolean }
    | { kind: 'marquee'; start: Point; additive: boolean }
    | { kind: 'wire'; moved: boolean }
    | null
  >(null);

  const onPointerDown = (e: ReactPointerEvent<SVGSVGElement>) => {
    if (e.button !== 0 && e.button !== 1) return;
    const target = e.target as Element;
    const svg = svgRef.current;
    if (!svg) return;
    const world = toWorld(e.clientX, e.clientY);
    svg.focus({ preventScroll: true });

    const pinEl = target.closest('[data-pin]');
    const compEl = target.closest('[data-component]');
    const wireEl = target.closest('[data-wire]');

    if (pinEl && e.button === 0 && !readOnly && mode !== 'probe') {
      const pin = findPinAt(world, 12);
      if (pin) {
        e.preventDefault();
        svg.setPointerCapture(e.pointerId);
        if (wiring) {
          finishWire(pin);
          gesture.current = null;
        } else {
          startWire(pin);
          gesture.current = { kind: 'wire', moved: false };
        }
        return;
      }
    }

    if (wiring && e.button === 0) {
      const pin = findPinAt(world, 10);
      const w = pin ? null : findWireAt(world, 6);
      if (pin) finishWire(pin);
      else if (w) finishWire({ net: w.wire.net, point: w.point });
      else {
        // Flip the elbow when clicking empty space.
        setWiring({ ...wiring, horizontalFirst: !wiring.horizontalFirst });
      }
      return;
    }

    if (wireEl && e.button === 0) {
      const index = Number(wireEl.getAttribute('data-wire'));
      const wire = wires[index];
      if (wire && (mode === 'probe' || e.altKey)) {
        const hit = findWireAt(world, 8);
        onNetClick?.(wire.net, hit?.point ?? world);
        return;
      }
      if (wire && mode === 'wire' && !readOnly) {
        // Start a branch from the wire: a net reference acts as the source pin.
        const hit = findWireAt(world, 8);
        const from: PinRef = { component: '', pin: '', net: wire.net, point: hit?.point ?? snapPoint(world) };
        setWiring({ from, cursor: from.point, horizontalFirst: true });
        return;
      }
      const additive = e.shiftKey || e.ctrlKey || e.metaKey;
      const already = selection.wires.includes(index);
      onSelectionChange(
        additive
          ? { components: selection.components, wires: already ? selection.wires.filter((i) => i !== index) : [...selection.wires, index] }
          : { components: [], wires: [index] },
      );
      return;
    }

    if (compEl && e.button === 0) {
      const id = compEl.getAttribute('data-component')!;
      const additive = e.shiftKey || e.ctrlKey || e.metaKey;
      let ids: string[];
      if (additive) {
        ids = selection.components.includes(id) ? selection.components.filter((c) => c !== id) : [...selection.components, id];
        onSelectionChange({ components: ids, wires: selection.wires });
      } else if (selection.components.includes(id)) {
        ids = selection.components;
      } else {
        ids = [id];
        onSelectionChange({ components: ids, wires: [] });
      }
      if (!readOnly && mode === 'select' && ids.includes(id)) {
        svg.setPointerCapture(e.pointerId);
        gesture.current = { kind: 'move', ids, start: world, moved: false };
      }
      return;
    }

    // Background.
    if (e.button === 0 && e.shiftKey && mode === 'select') {
      svg.setPointerCapture(e.pointerId);
      gesture.current = { kind: 'marquee', start: world, additive: e.ctrlKey || e.metaKey };
      setMarquee({ x: world.x, y: world.y, width: 0, height: 0 });
      return;
    }
    svg.setPointerCapture(e.pointerId);
    gesture.current = { kind: 'pan', startX: e.clientX, startY: e.clientY, vp: vpRef.current, moved: false };
  };

  const onPointerMove = (e: ReactPointerEvent<SVGSVGElement>) => {
    const g = gesture.current;
    const world = toWorld(e.clientX, e.clientY);
    if (wiring) {
      const pin = findPinAt(world, 10);
      setWiring((w) => (w ? { ...w, cursor: pin ? pin.point : snapPoint(world) } : w));
    }
    if (!g) return;
    if (g.kind === 'pan') {
      const dx = e.clientX - g.startX;
      const dy = e.clientY - g.startY;
      if (!g.moved && Math.hypot(dx, dy) < 3) return;
      g.moved = true;
      setViewport({ x: g.vp.x + dx, y: g.vp.y + dy, k: g.vp.k }, false);
    } else if (g.kind === 'move') {
      const dx = snap(world.x - g.start.x);
      const dy = snap(world.y - g.start.y);
      if (dx !== 0 || dy !== 0) g.moved = true;
      setDragOffset(g.moved ? { x: dx, y: dy } : null);
    } else if (g.kind === 'marquee') {
      setMarquee({
        x: Math.min(g.start.x, world.x),
        y: Math.min(g.start.y, world.y),
        width: Math.abs(world.x - g.start.x),
        height: Math.abs(world.y - g.start.y),
      });
    } else if (g.kind === 'wire') {
      g.moved = true;
    }
  };

  const onPointerUp = (e: ReactPointerEvent<SVGSVGElement>) => {
    const g = gesture.current;
    gesture.current = null;
    const svg = svgRef.current;
    if (svg?.hasPointerCapture(e.pointerId)) svg.releasePointerCapture(e.pointerId);
    const world = toWorld(e.clientX, e.clientY);
    if (!g) return;
    if (g.kind === 'pan') {
      if (g.moved) setViewportState(vpRef.current);
      else if (mode !== 'probe') onSelectionChange(EMPTY_SELECTION);
      return;
    }
    if (g.kind === 'move') {
      const dx = snap(world.x - g.start.x);
      const dy = snap(world.y - g.start.y);
      setDragOffset(null);
      if (g.moved && (dx !== 0 || dy !== 0)) onMove?.(g.ids, dx, dy);
      return;
    }
    if (g.kind === 'marquee') {
      const box = marquee;
      setMarquee(null);
      if (!box) return;
      const inside = placed.filter((p) => rectContainsRect(box, p.bounds)).map((p) => p.id);
      const wireIdx = wires
        .map((w, i) => ({ w, i }))
        .filter(({ w }) => w.points.every(([x, y]) => x >= box.x && x <= box.x + box.width && y >= box.y && y <= box.y + box.height))
        .map(({ i }) => i);
      onSelectionChange(
        g.additive
          ? { components: [...new Set([...selection.components, ...inside])], wires: [...new Set([...selection.wires, ...wireIdx])] }
          : { components: inside, wires: wireIdx },
      );
      return;
    }
    if (g.kind === 'wire' && g.moved && wiring) {
      // Drag from pin to pin: finish on release when over a pin or wire.
      const pin = findPinAt(world, 10);
      const w = pin ? null : findWireAt(world, 6);
      if (pin && !(pin.component === wiring.from.component && pin.pin === wiring.from.pin)) finishWire(pin);
      else if (w && w.wire.net !== wiring.from.net) finishWire({ net: w.wire.net, point: w.point });
    }
  };

  const onDoubleClick = (e: React.MouseEvent<SVGSVGElement>) => {
    const target = e.target as Element;
    const compEl = target.closest('[data-component]');
    if (compEl) onOpenComponent?.(compEl.getAttribute('data-component')!);
    const labelEl = target.closest('[data-net-label]');
    if (labelEl && !readOnly) onRenameNet?.(labelEl.getAttribute('data-net-label')!);
  };

  // --- keyboard ------------------------------------------------------------------

  const onKeyDown = (e: KeyboardEvent<SVGSVGElement>) => {
    const target = e.target as Element;
    const onPin = target.closest('[data-pin]');
    const onComponent = target.closest('[data-component]');
    if (e.key === 'Enter' || e.key === ' ') {
      if (onPin) {
        e.preventDefault();
        const [component, pin] = (onPin.getAttribute('data-pin') ?? '').split('.');
        const c = placedById.get(component ?? '');
        const p = c?.pins.find((x) => x.name === pin);
        if (c && p) {
          if (mode === 'probe' && p.net) onNetClick?.(p.net, p.point);
          else onPinActivate({ component: c.id, pin: p.name, net: p.net, point: p.point });
        }
        return;
      }
      if (onComponent) {
        e.preventDefault();
        const id = onComponent.getAttribute('data-component')!;
        if (e.key === 'Enter') onOpenComponent?.(id);
        else onSelectionChange(selection.components.includes(id) ? { ...selection, components: selection.components.filter((c) => c !== id) } : { ...selection, components: [...selection.components, id] });
        return;
      }
    }
    if (e.key === 'Escape') {
      if (wiring) {
        setWiring(null);
        setAnnouncement('Wiring cancelled.');
      } else onSelectionChange(EMPTY_SELECTION);
      return;
    }
    if (e.target !== svgRef.current && !onComponent) return; // text inputs etc.
    const hasSelection = selection.components.length > 0 || selection.wires.length > 0;
    const stepPx = e.shiftKey ? GRID * 5 : GRID;
    switch (e.key) {
      case 'Delete':
      case 'Backspace':
        if (hasSelection && !readOnly) {
          e.preventDefault();
          onDelete?.(selection);
          setAnnouncement('Deleted selection.');
        }
        break;
      case 'r':
      case 'R':
        if (selection.components.length && !readOnly) {
          e.preventDefault();
          onRotate?.(selection.components);
        }
        break;
      case 'm':
      case 'M':
        if (selection.components.length && !readOnly) {
          e.preventDefault();
          onMirror?.(selection.components);
        }
        break;
      case 'ArrowLeft':
      case 'ArrowRight':
      case 'ArrowUp':
      case 'ArrowDown': {
        e.preventDefault();
        const dx = e.key === 'ArrowLeft' ? -stepPx : e.key === 'ArrowRight' ? stepPx : 0;
        const dy = e.key === 'ArrowUp' ? -stepPx : e.key === 'ArrowDown' ? stepPx : 0;
        if (selection.components.length && !readOnly) onMove?.(selection.components, dx, dy);
        else {
          const vp = vpRef.current;
          setViewport({ ...vp, x: vp.x - dx * 4, y: vp.y - dy * 4 });
        }
        break;
      }
      case '+':
      case '=':
        e.preventDefault();
        zoomAt(1.25);
        break;
      case '-':
        e.preventDefault();
        zoomAt(0.8);
        break;
      case '0':
        e.preventDefault();
        zoomToFit();
        break;
      case 'a':
      case 'A':
        if (e.ctrlKey || e.metaKey) {
          e.preventDefault();
          onSelectionChange({ components: placed.map((p) => p.id), wires: wires.map((_, i) => i) });
        }
        break;
      default:
        break;
    }
  };

  // --- drag and drop from the library -----------------------------------------

  const onDragOver = (e: React.DragEvent<SVGSVGElement>) => {
    if (readOnly || !e.dataTransfer.types.includes('text/x-embsim-component')) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
    setDropHint(snapPoint(toWorld(e.clientX, e.clientY)));
  };

  const onDrop = (e: React.DragEvent<SVGSVGElement>) => {
    const id = e.dataTransfer.getData('text/x-embsim-component');
    setDropHint(null);
    if (!id || readOnly) return;
    e.preventDefault();
    onPlace?.(id, snapPoint(toWorld(e.clientX, e.clientY)));
  };

  // --- rendering ------------------------------------------------------------------

  const visibleWorld: Rect = {
    x: -viewport.x / viewport.k,
    y: -viewport.y / viewport.k,
    width: size.width / viewport.k,
    height: size.height / viewport.k,
  };
  const visible = placed.filter((p) => rectsIntersect(p.bounds, { x: visibleWorld.x - 40, y: visibleWorld.y - 40, width: visibleWorld.width + 80, height: visibleWorld.height + 80 }));
  const selectedSet = new Set(selection.components);
  const probeNets = useMemo(() => {
    const map = new Map<string, ProbeInfo[]>();
    for (const p of probes ?? []) {
      const m = /^net:(.+)$/.exec(p.target);
      if (m) (map.get(m[1]!) ?? map.set(m[1]!, []).get(m[1]!)!).push(p);
    }
    return map;
  }, [probes]);

  const netVisual = (net: string): NetVisual => (live ? describeNetState(netStates?.[net], analogRange) : { kind: 'idle', text: '' });

  const wiringPreview = wiring ? routeOrthogonal(wiring.from.point, wiring.cursor, wiring.horizontalFirst) : null;
  const showPins = !readOnly || mode === 'probe';
  const cursorClass = mode === 'wire' ? 'es-scene--wire' : mode === 'probe' ? 'es-scene--probe' : '';

  return (
    <div className={`es-scene ${cursorClass} ${className ?? ''}`.trim()}>
      <svg
        ref={svgRef}
        className="es-scene__svg"
        role="application"
        aria-label={label}
        aria-roledescription="schematic canvas"
        tabIndex={0}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onDoubleClick={onDoubleClick}
        onKeyDown={onKeyDown}
        onDragOver={onDragOver}
        onDragLeave={() => setDropHint(null)}
        onDrop={onDrop}
        onPointerLeave={() => setHover(null)}
      >
        <defs>
          <pattern id="es-grid-minor" width={GRID} height={GRID} patternUnits="userSpaceOnUse">
            <circle cx={0} cy={0} r={0.6} className="es-scene__grid-dot" />
          </pattern>
          <pattern id="es-grid-major" width={GRID * 10} height={GRID * 10} patternUnits="userSpaceOnUse">
            <rect width={GRID * 10} height={GRID * 10} fill="url(#es-grid-minor)" />
            <circle cx={0} cy={0} r={1.1} className="es-scene__grid-dot es-scene__grid-dot--major" />
          </pattern>
        </defs>
        <g ref={sceneRef} transform={`translate(${viewport.x} ${viewport.y}) scale(${viewport.k})`}>
          <g className="es-scene__layer es-scene__layer--sheet">
            <rect className="es-scene__sheet" x={0} y={0} width={sheet.width} height={sheet.height} />
            {viewport.k > 0.35 && <rect x={0} y={0} width={sheet.width} height={sheet.height} fill="url(#es-grid-major)" pointerEvents="none" />}
          </g>

          <g className="es-scene__layer es-scene__layer--wires">
            {ratsnest.map((r, i) => (
              <line key={`rat-${i}`} className="es-scene__ratsnest" x1={r.a.x} y1={r.a.y} x2={r.b.x} y2={r.b.y} />
            ))}
            {wires.map((w, i) => {
              const vis = netVisual(w.net);
              const pts = w.points.map(([x, y]) => `${x},${y}`).join(' ');
              const selected = selection.wires.includes(i);
              return (
                <g key={`w-${i}`} data-wire={i} className={`es-wire es-wire--${vis.kind}${selected ? ' es-wire--selected' : ''}`}>
                  <polyline className="es-wire__hit" points={pts} />
                  <polyline
                    className="es-wire__line"
                    points={pts}
                    style={vis.color ? { stroke: vis.color } : undefined}
                    onPointerEnter={(e) => {
                      const rect = svgRef.current?.getBoundingClientRect();
                      setHover({
                        text: w.net,
                        sub: live ? vis.text : `${project.nets[w.net]?.length ?? 0} pins`,
                        x: e.clientX - (rect?.left ?? 0),
                        y: e.clientY - (rect?.top ?? 0),
                      });
                    }}
                    onPointerLeave={() => setHover(null)}
                  />
                </g>
              );
            })}
            {junctions.map((j, i) => {
              const vis = netVisual(j.net);
              return <circle key={`j-${i}`} className={`es-junction es-wire--${vis.kind}`} cx={j.point.x} cy={j.point.y} r={2.4} style={vis.color ? { fill: vis.color } : undefined} />;
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--components">
            {visible.map((c) => {
              const selected = selectedSet.has(c.id);
              const offset = selected && dragOffset ? dragOffset : null;
              const sym = c.symbol;
              const desc = `${c.id}, ${sym?.name ?? c.componentId}${c.value ? `, ${c.value}` : ''}${selected ? ', selected' : ''}`;
              return (
                <g
                  key={c.id}
                  data-component={c.id}
                  className={`es-component${selected ? ' es-component--selected' : ''}`}
                  role="group"
                  aria-label={c.id}
                  transform={offset ? `translate(${offset.x} ${offset.y})` : undefined}
                >
                  <rect
                    className="es-component__hit"
                    role="button"
                    tabIndex={0}
                    aria-label={desc}
                    aria-pressed={selected}
                    x={c.bounds.x}
                    y={c.bounds.y}
                    width={c.bounds.width}
                    height={c.bounds.height}
                    rx={2}
                  />
                  {sym ? (
                    <ComponentSymbol svg={sym.svg} symbol={sym.symbol} layout={c.layout} selected={selected} fallbackLabel={c.id} />
                  ) : (
                    <g className="es-symbol__fallback">
                      <rect x={c.layout.x} y={c.layout.y} width={60} height={20} rx={2} />
                      <text x={c.layout.x + 30} y={c.layout.y + 13} textAnchor="middle">
                        {c.componentId}
                      </text>
                    </g>
                  )}
                  <text className="es-component__ref" x={c.bounds.x} y={c.bounds.y - 3}>
                    {c.id}
                  </text>
                  {c.value && (
                    <text className="es-component__value" x={c.bounds.x + c.bounds.width + 3} y={c.bounds.y + c.bounds.height / 2 + 3}>
                      {c.value}
                    </text>
                  )}
                  {showPins &&
                    c.pins.map((pin) => {
                      const vis = pin.net ? netVisual(pin.net) : { kind: 'idle' as const, text: '' };
                      return (
                        <g
                          key={pin.name}
                          data-pin={`${c.id}.${pin.name}`}
                          className={`es-pin es-wire--${vis.kind}${wiring && wiring.from.component === c.id && wiring.from.pin === pin.name ? ' es-pin--active' : ''}`}
                          tabIndex={0}
                          role="button"
                          aria-label={`Pin ${c.id}.${pin.name}${pin.net ? `, net ${pin.net}` : ', unconnected'}${live && pin.net ? `, ${vis.text}` : ''}`}
                          transform={`translate(${pin.point.x} ${pin.point.y})`}
                          onPointerEnter={(e) => {
                            const rect = svgRef.current?.getBoundingClientRect();
                            setHover({
                              text: `${c.id}.${pin.name}`,
                              sub: pin.net ? (live ? `${pin.net} · ${vis.text}` : `net ${pin.net}`) : 'unconnected',
                              x: e.clientX - (rect?.left ?? 0),
                              y: e.clientY - (rect?.top ?? 0),
                            });
                          }}
                          onPointerLeave={() => setHover(null)}
                        >
                          <circle className="es-pin__hit" r={6} />
                          <circle className="es-pin__dot" r={pin.net ? 1.8 : 2.4} style={vis.color ? { fill: vis.color } : undefined} />
                        </g>
                      );
                    })}
                </g>
              );
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--labels">
            {labels.map((l, i) => {
              const vis = netVisual(l.net);
              return (
                <g key={`l-${i}`} data-net-label={l.net} className={`es-net-label es-wire--${vis.kind}`} transform={`translate(${l.x} ${l.y})`}>
                  <text style={vis.color ? { fill: vis.color } : undefined}>{l.net}</text>
                  {live && vis.text && (
                    <text className="es-net-label__state" y={11}>
                      {vis.text}
                    </text>
                  )}
                </g>
              );
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--probes">
            {[...probeNets.entries()].map(([net, list]) => {
              const w = wires.find((x) => x.net === net);
              const at = w ? { x: w.points[0]![0], y: w.points[0]![1] } : pinsByNet[net]?.[0];
              if (!at) return null;
              return (
                <g key={`p-${net}`} className="es-probe" transform={`translate(${at.x} ${at.y})`}>
                  <path d="M0 0 L8 -12 H30 V-4 H14 Z" />
                  <text x={11} y={-6.5}>
                    {list.map((p) => p.probe).join(',')}
                  </text>
                </g>
              );
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--selection">
            {marquee && <rect className="es-scene__marquee" x={marquee.x} y={marquee.y} width={marquee.width} height={marquee.height} />}
            {wiringPreview && <polyline className="es-scene__wire-preview" points={wiringPreview.map(([x, y]) => `${x},${y}`).join(' ')} />}
            {dropHint && <rect className="es-scene__drop-hint" x={dropHint.x - 5} y={dropHint.y - 5} width={10} height={10} />}
          </g>
        </g>
      </svg>
      {hover && (
        <div className="es-scene__tooltip" style={{ left: hover.x + 12, top: hover.y + 12 }} role="presentation">
          <span className="es-mono">{hover.text}</span>
          {hover.sub && <span className="es-scene__tooltip-sub">{hover.sub}</span>}
        </div>
      )}
      <div className="es-sr-only" aria-live="polite">
        {announcement}
      </div>
      <div className="es-scene__hud" aria-hidden="true">
        <span className="es-mono">{Math.round(viewport.k * 100)}%</span>
        {wiring && <span>wiring from {wiring.from.component ? `${wiring.from.component}.${wiring.from.pin}` : wiring.from.net} · Esc cancels</span>}
        {mode === 'wire' && !wiring && <span>click a pin to start a wire</span>}
        {mode === 'probe' && <span>click a wire or pin to add a probe</span>}
      </div>
    </div>
  );
}

export const MemoSchematicCanvas = memo(SchematicCanvas);
