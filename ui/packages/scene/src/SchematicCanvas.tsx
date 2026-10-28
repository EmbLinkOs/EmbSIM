// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// SchematicCanvas: the SVG scene graph of SPEC-011 §3. One viewport group
// carries pan and zoom; a continuous CSS background sits behind wires, components, labels,
// selection and probes. Components outside the viewport are not mounted.
// Editing callbacks describe intents (move, wire, delete); the owner turns
// them into project transactions and feeds the new project back in.

import type { InputEntry, Layout, NetState, OutputValue, PinDescription, ProbeInfo, Project, SymbolDescription, Wire } from '@embsim/protocol';
import { memo, useCallback, useEffect, useImperativeHandle, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as ReactPointerEvent, type Ref } from 'react';
import { componentValue, placeComponentLabels } from './componentLabels';
import { MeasurementProbe } from './MeasurementProbe';
import { ComponentSymbol } from './ComponentSymbol';
import {
  GRID,
  magneticDrag,
  distanceToSegment,
  findJunctions,
  findAirwires,
  pinPosition,
  localToSheet,
  rectContainsRect,
  rectsIntersect,
  routeOrthogonal,
  extendWirePath,
  snap,
  snapPoint,
  symbolBounds,
  wireSegments,
  type Point,
  type Rect,
} from './geometry';
import { busText, busValue } from './bus';
import { routeConnection } from './autoLayout';
import { shiftWireSegment } from './wireEditing';
import { canvasBackground, DEFAULT_CANVAS_APPEARANCE, type CanvasAppearance } from './canvasAppearance';
import { liveElectricalIssues } from './electricalFeedback';

export interface SceneSymbol {
  /** Authoritative terminal names; symbols may omit hidden terminals. */
  terminals?: string[];
  /** Electrical metadata for declared component terminals (boards may omit it). */
  pins?: PinDescription[];
  name: string;
  symbol: SymbolDescription;
  svg: string;
  /** The parameter shown as the value label (e.g. "resistance"). */
  valueParam?: string;
  valueDefault?: unknown;
  valueUnit?: string;
  /** a part that only connects (a breadboard): not drawn, its connections shown between the parts on it */
  wiringAid?: boolean;
}

export type SceneCatalog = Record<string, SceneSymbol>;

export type CanvasMode = 'select' | 'wire' | 'probe' | 'interact';

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
  focusComponents(ids: string[]): void;
  focusNet(net: string): void;
  focusPoint(point: Point): void;
  focusWires(indices: number[]): void;
  zoomBy(factor: number): void;
  resetZoom(): void;
  focus(): void;
  getViewport(): Viewport;
}

export interface CanvasContext {
  x: number;
  y: number;
  point: Point;
  selection: Selection;
  net?: string;
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
  outputs?: Record<string, OutputValue>;
  inputs?: InputEntry[];
  onInput?: (target: string, input: string, value: unknown) => void;
  /** Voltage range for the analog gradient; default [0, 3.3]. */
  analogRange?: [number, number];
  probes?: ProbeInfo[];
  onMoveProbe?: (id: string, net: string) => Promise<void>;
  voltageComparison?: { a?: string; b?: string; aPoint?: Point; bPoint?: Point };
  onCompareNet?: (side: 'a' | 'b', net: string, at?: Point) => void;
  readOnly?: boolean;
  showGrid?: boolean;
  appearance?: CanvasAppearance;
  showAirwires?: boolean;
  showMinimap?: boolean;
  smartWires?: boolean;
  alignmentGuides?: boolean;
  highlightNet?: string | null;
  onHighlightNet?: (net: string) => void;
  issueMarker?: { point: Point; label: string; severity: 'error' | 'warning' | 'info' };
  onReshapeWire?: (index: number, segment: number, offset: number, source: Project) => void;
  onCanvasContext?: (context: CanvasContext) => void;
  onMove?: (ids: string[], dx: number, dy: number) => void;
  onRotate?: (ids: string[]) => void;
  onMirror?: (ids: string[]) => void;
  /** L on one selected wire: label its net there. */
  onLabelWire?: (wire: number) => void;
  onDelete?: (selection: Selection) => void;
  /** Manual paths must bypass automatic rerouting on completion. */
  onWire?: (from: PinRef, to: PinRef | NetRef, points: [number, number][], manual?: boolean) => void;
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

export type NetVisual = { kind: 'low' | 'high' | 'z' | 'x' | 'analog' | 'idle'; color?: string; text: string };

export function describeNetState(state: NetState | undefined, range: [number, number]): NetVisual {
  if (!state) return { kind: 'idle', text: 'no state' };
  if (state.contention) return { kind: 'x', text: `conflicting drivers${state.voltage !== undefined ? ` · ${state.voltage.toFixed(3)} V` : ''}` };
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

interface PlacedComponent {
  id: string;
  componentId: string;
  layout: Layout;
  symbol: SceneSymbol | undefined;
  bounds: Rect;
  pins: { name: string; point: Point; net: string | undefined; label: { x: number; y: number; anchor: 'start' | 'middle' | 'end' } }[];
  value: string;
  emitterColor: string;
}

type HoverFn = (text: string, sub: string, clientX: number, clientY: number) => void;
const IDLE: NetVisual = { kind: 'idle', text: '' };

interface PartViewProps {
  c: PlacedComponent;
  selected: boolean;
  offset: Point | null;
  showPins: boolean;
  live: boolean;
  /** Each pin's net state, and a key of them all for the memo. */
  pinVisuals: NetVisual[];
  pinKey: string;
  activePin: string | null;
  highlightNet: string | null;
  outputs: Record<string, OutputValue>;
  outputKey: string;
  inputs: InputEntry[];
  inputKey: string;
  interactive: boolean;
  onInput?: SchematicCanvasProps['onInput'];
  onHover: HoverFn;
  onLeave: () => void;
}

/** One placed part: re-rendered only when it, its selection or its pins' nets change. */
const PartView = memo(
  function PartView({ c, selected, offset, showPins, live, pinVisuals, activePin, highlightNet, outputs, inputs, interactive, onInput, onHover, onLeave }: PartViewProps) {
    const sym = c.symbol;
    const desc = `${c.id}, ${sym?.name ?? c.componentId}${c.value ? `, ${c.value}` : ''}${selected ? ', selected' : ''}`;
    return (
      <g
        data-component={c.id}
        className={`es-component${c.layout.locked ? ' es-component--locked' : ''}${selected ? ' es-component--selected' : ''}`}
        role="group"
        aria-label={c.id}
        transform={offset ? `translate(${offset.x} ${offset.y})` : undefined}
      >
        <rect className="es-component__hit" role="button" tabIndex={0} aria-label={desc} aria-pressed={selected} x={c.bounds.x} y={c.bounds.y} width={c.bounds.width} height={c.bounds.height} rx={2} />
        {sym ? (
          <ComponentSymbol id={c.id} svg={sym.svg} symbol={sym.symbol} layout={c.layout} selected={selected} fallbackLabel={c.id} emitterColor={c.emitterColor} outputs={live ? outputs : undefined} inputs={inputs} interactive={interactive} onInput={onInput} />
        ) : (
          <g className="es-symbol__fallback">
            <rect x={c.layout.x} y={c.layout.y} width={60} height={20} rx={2} />
            <text x={c.layout.x + 30} y={c.layout.y + 13} textAnchor="middle">
              {c.componentId}
            </text>
          </g>
        )}
        {showPins &&
          c.pins.map((pin, i) => {
            const vis = pinVisuals[i] ?? IDLE;
            return (
              <g
                key={pin.name}
                data-pin={`${c.id}.${pin.name}`}
                className={`es-pin${pin.net ? ' es-pin--connected' : ''} es-wire--${vis.kind}${activePin === pin.name ? ' es-pin--active' : ''}${pin.net && pin.net === highlightNet ? ' es-pin--spotlight' : ''}${selected ? ' es-pin--labelled' : ''}`}
                tabIndex={0}
                role="button"
                aria-label={`Pin ${c.id}.${pin.name}${pin.net ? `, net ${pin.net}` : ', unconnected'}${live && pin.net ? `, ${vis.text}` : ''}`}
                transform={`translate(${pin.point.x} ${pin.point.y})`}
                onPointerEnter={(e) => onHover(`${c.id}.${pin.name}`, pin.net ? (live ? `${pin.net} · ${vis.text}` : `net ${pin.net}`) : 'unconnected', e.clientX, e.clientY)}
                onPointerLeave={onLeave}
                onFocus={(e) => { const r = e.currentTarget.getBoundingClientRect(); onHover(`${c.id}.${pin.name}`, pin.net ? `${pin.net}${live ? ` · ${vis.text}` : ''}` : 'unconnected', r.x, r.y); }}
                onBlur={onLeave}
              >
                <circle className="es-pin__hit" r={7} />
                <circle className="es-pin__dot" r={3.2} style={vis.color ? { stroke: vis.color } : undefined} />
                <circle className="es-pin__halo" r={6} />
                <text className="es-pin__label" x={pin.label.x} y={pin.label.y} textAnchor={pin.label.anchor} pointerEvents="none">
                  {pin.name}{live && ['low', 'high', 'x', 'z'].includes(vis.kind) ? ` · ${vis.kind === 'low' ? '0' : vis.kind === 'high' ? '1' : vis.kind.toUpperCase()}` : ''}
                </text>
              </g>
            );
          })}
      </g>
    );
  },
  (a, b) =>
    a.c === b.c && a.selected === b.selected && a.offset === b.offset && a.showPins === b.showPins && a.live === b.live && a.pinKey === b.pinKey && a.activePin === b.activePin && a.highlightNet === b.highlightNet && a.outputKey === b.outputKey && a.inputKey === b.inputKey && a.interactive === b.interactive && a.onInput === b.onInput && a.onHover === b.onHover,
);

interface WireViewProps {
  selectable: boolean;
  index: number;
  wire: Wire;
  vis: NetVisual;
  selected: boolean;
  sub: string;
  onHover: HoverFn;
  onLeave: () => void;
}

/** One drawn wire: re-rendered only when its points, selection or net state change. */
const WireView = memo(
  function WireView({ selectable, index, wire, vis, selected, sub, onHover, onLeave }: WireViewProps) {
    const pts = wire.points.map(([x, y]) => `${x},${y}`).join(' ');
    return (
      <g data-wire={index} role={selectable ? 'button' : 'img'} tabIndex={selectable ? 0 : undefined} aria-label={`Track ${index + 1}, net ${wire.net}${selected ? ' selected' : ''}`} className={`es-wire es-wire--${vis.kind}${selected ? ' es-wire--selected' : ''}`}>
        {selected && <polyline className="es-wire__selection" points={pts} />}
        <polyline className="es-wire__hit" points={pts} />
        <polyline className="es-wire__line" points={pts} style={vis.color && !selected ? { stroke: vis.color } : undefined} onPointerEnter={(e) => onHover(wire.net, sub, e.clientX, e.clientY)} onPointerLeave={onLeave} />
      </g>
    );
  },
  (a, b) => a.selectable === b.selectable && a.wire === b.wire && a.index === b.index && a.selected === b.selected && a.sub === b.sub && a.vis.kind === b.vis.kind && a.vis.color === b.vis.color && a.onHover === b.onHover,
);

export function SchematicCanvas(props: SchematicCanvasProps) {
  const {
    project,
    catalog,
    mode,
    selection,
    onSelectionChange,
    live,
    netStates,
    outputs = {},
    inputs = [],
    onInput,
    analogRange = [0, 3.3],
    probes,
    onMoveProbe,
    voltageComparison,
    onCompareNet,
    readOnly,
    showGrid = true,
    appearance = DEFAULT_CANVAS_APPEARANCE,
    showAirwires = true,
    showMinimap = false,
    smartWires = false,
    alignmentGuides = true,
    highlightNet: requestedHighlightNet,
    onHighlightNet,
    issueMarker,
    onMove,
    onReshapeWire,
    onCanvasContext,
    onRotate,
    onMirror,
    onLabelWire,
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

  const backgroundRef = useRef<HTMLDivElement>(null);
  const appearanceRef = useRef({ appearance, showGrid });
  appearanceRef.current = { appearance, showGrid };
  const svgRef = useRef<SVGSVGElement>(null);
  const sceneRef = useRef<SVGGElement>(null);
  const vpRef = useRef<Viewport>({ x: 40, y: 40, k: 1 });
  const [viewport, setViewportState] = useState<Viewport>(vpRef.current);
  const [size, setSize] = useState({ width: 800, height: 600 });
  const [hover, setHover] = useState<{ text: string; sub?: string; x: number; y: number } | null>(null);
  const [dragOffset, setDragOffset] = useState<Point | null>(null);
  const [guides, setGuides] = useState<{ x?: number; y?: number }>({});
  const [marquee, setMarquee] = useState<Rect | null>(null);
  const [wiring, setWiring] = useState<{ from: PinRef; cursor: Point; horizontalFirst: boolean; legs: [number, number][][]; manual: boolean } | null>(null);
  const [dropHint, setDropHint] = useState<Point | null>(null);
  const [announcement, setAnnouncement] = useState('');
  const rafRef = useRef<number | null>(null);

  const [wirePreview, setWirePreview] = useState<{ source: Project; wires: Wire[] } | null>(null);
  const wires = useMemo(() => wirePreview?.source === project ? wirePreview.wires : project.layout?.wires ?? [], [project, wirePreview]);
  const labels = useMemo(() => project.layout?.labels ?? [], [project.layout?.labels]);
  const buses = useMemo(() => project.layout?.buses ?? [], [project.layout?.buses]);

  const pinNet = useMemo(() => {
    const map = new Map<string, string>();
    for (const [net, pins] of Object.entries(project.nets)) for (const p of pins) map.set(p, net);
    return map;
  }, [project.nets]);

  const placed = useMemo<PlacedComponent[]>(() => {
    // boards are drawn as blocks of their ports, beside the components
    const boards = (project.boards ?? []).map((b) => ({ id: b.id, component: b.board, params: undefined, layout: b.layout }));
    return [...project.components.filter((c) => !catalog[c.component]?.wiringAid), ...boards].map((c) => {
      const symbol = catalog[c.component];
      const layout = c.layout ?? { x: 0, y: 0 };
      const sym = symbol?.symbol ?? { svg: '', width: 60, height: 20, pins: {} };
      const pins = Object.entries(sym.pins).map(([name, anchor]) => {
        const point = pinPosition(layout, sym, name) ?? { x: layout.x, y: layout.y };
        const side = anchor.side ?? (anchor.x <= sym.width / 2 ? 'left' : 'right');
        const outside = localToSheet({ x: anchor.x + (side === 'left' ? -10 : side === 'right' ? 10 : 0), y: anchor.y + (side === 'top' ? -10 : side === 'bottom' ? 10 : 0) }, layout, sym);
        const dx = Math.round(outside.x - point.x), dy = Math.round(outside.y - point.y);
        const label = { x: dx, y: dy + (dx ? -5 : dy > 0 ? 8 : 0), anchor: (dx < 0 ? 'end' : dx > 0 ? 'start' : 'middle') as 'start' | 'end' | 'middle' };
        return { name, point, net: pinNet.get(`${c.id}.${name}`), label };
      });
      const valueParam = symbol?.valueParam;
      const value = valueParam === 'dc' && typeof c.params?.kind === 'string' && c.params.kind !== 'dc' ? c.params.kind : valueParam ? componentValue(c.params?.[valueParam] ?? symbol?.valueDefault, symbol?.valueUnit) : '';
      const colors: Record<string, string> = { red: '#ff453a', green: '#30d158', blue: '#0a84ff', yellow: '#ffd60a', white: '#f5f5ff' };
      const emitterColor = colors[String(c.params?.color ?? symbol?.valueDefault ?? 'red')] ?? '#ff453a';
      return { emitterColor, id: c.id, componentId: c.component, layout, symbol, bounds: symbolBounds(layout, sym, 4), pins, value };
    });
  }, [project.components, project.boards, catalog, pinNet]);

  const [probePoints, setProbePoints] = useState<{ project: Project | null; points: Record<string, Point> }>({ project: null, points: {} });
  const componentLabels = useMemo(() => {
    const headers = (project.layout?.groups ?? []).flatMap((group) => {
      const members = placed.filter((part) => group.members.includes(part.id));
      if (!members.length) return [];
      return [{ x: Math.min(...members.map((part) => part.bounds.x)) - 14, y: Math.min(...members.map((part) => part.bounds.y)) - 39, width: Math.max(100, group.name.length * 8 + 24), height: 24 }];
    });
    for (const probe of probes ?? []) {
      const net = probe.target.startsWith('net:') ? probe.target.slice(4) : pinNet.get(probe.target.slice(4));
      if (!net) continue;
      const wire = wires.find((w) => w.net === net);
      const point = (probePoints.project === project ? probePoints.points[`${probe.probe}:${net}`] : undefined) ?? (wire ? { x: wire.points[0]![0], y: wire.points[0]![1] } : placed.flatMap((p) => p.pins).find((p) => p.net === net)?.point);
      if (point) headers.push({ x: point.x + 8, y: point.y - 24, width: Math.max(36, String(probe.probe).length * 6 + 22), height: 24 });
    }
    return placeComponentLabels(placed.map((part) => ({ id: part.id, bounds: part.bounds, pins: part.pins.map((pin) => pin.point), name: part.symbol?.name ?? part.componentId.split('/').at(-1) ?? part.componentId, value: part.value })), wires, appearance.labels !== 'compact', headers);
  }, [placed, wires, appearance.labels, project, probes, pinNet, probePoints]);

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
      for (const line of findAirwires(points, netWires, labels.filter((l) => l.net === net))) lines.push({ net, ...line });
    }
    return lines;
  }, [pinsByNet, wires, labels]);

  // --- viewport -----------------------------------------------------------

  const setViewport = useCallback((next: Viewport, commit = true) => {
    vpRef.current = next;
    if (backgroundRef.current) Object.assign(backgroundRef.current.style, canvasBackground(appearanceRef.current.appearance, next, appearanceRef.current.showGrid));
    sceneRef.current?.setAttribute('transform', `translate(${next.x} ${next.y}) scale(${next.k})`);
    if (commit) {
      // the transform above moves the scene at once; React (culling, the
      // HUD) catches up when the wheel or the drag pauses, not every frame,
      // and the small text and pin dots are left out while it moves
      svgRef.current?.classList.add('es-scene__svg--moving');
      if (rafRef.current !== null) clearTimeout(rafRef.current);
      rafRef.current = window.setTimeout(() => {
        rafRef.current = null;
        svgRef.current?.classList.remove('es-scene__svg--moving');
        setViewportState(vpRef.current);
      }, 90);
    }
  }, []);

  useEffect(() => () => { if (rafRef.current !== null) window.clearTimeout(rafRef.current); }, []);

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

  const focusBounds = useCallback((rects: Rect[]) => {
    if (!rects.length) return;
    // An imperative focus can run before ResizeObserver's first delivery when
    // a tab mounts. Use the actual canvas dimensions instead of its fallback.
    const width = svgRef.current?.clientWidth || size.width;
    const height = svgRef.current?.clientHeight || size.height;
    const minX = Math.min(...rects.map((r) => r.x)) - 50;
    const minY = Math.min(...rects.map((r) => r.y)) - 50;
    const maxX = Math.max(...rects.map((r) => r.x + r.width)) + 50;
    const maxY = Math.max(...rects.map((r) => r.y + r.height)) + 50;
    const k = Math.max(MIN_ZOOM, Math.min(2, Math.min(width / (maxX - minX), height / (maxY - minY))));
    setViewport({ x: (width - (maxX - minX) * k) / 2 - minX * k, y: (height - (maxY - minY) * k) / 2 - minY * k, k });
  }, [size, setViewport]);

  const zoomToFit = useCallback(() => {
    const rects = [...placed.map((p) => p.bounds), ...componentLabels.values()];
    for (const w of wires) for (const [x, y] of w.points) rects.push({ x, y, width: 0, height: 0 });
    if (!rects.length) setViewport({ x: 40, y: 40, k: 1 });
    else focusBounds(rects);
  }, [placed, componentLabels, wires, focusBounds, setViewport]);

  useImperativeHandle(handle, () => ({
    zoomToFit,
    zoomBy: (f) => zoomAt(f),
    resetZoom: () => setViewport({ x: 40, y: 40, k: 1 }),
    focus: () => svgRef.current?.focus(),
    getViewport: () => vpRef.current,
    focusWires: (indices) => focusBounds(wires.flatMap((w, i) => indices.includes(i) ? w.points.map(([x, y]) => ({ x, y, width: 0, height: 0 })) : [])),
    focusPoint: (point) => focusBounds([{ x: point.x - 80, y: point.y - 60, width: 160, height: 120 }]),
    focusComponents: (ids) => focusBounds(placed.filter((p) => ids.includes(p.id)).map((p) => p.bounds)),
    focusNet: (net) => focusBounds([
      ...(pinsByNet[net] ?? []).map((p) => ({ ...p, width: 0, height: 0 })),
      ...wires.filter((w) => w.net === net).flatMap((w) => w.points.map(([x, y]) => ({ x, y, width: 0, height: 0 }))),
    ]),
  }), [zoomToFit, zoomAt, setViewport, focusBounds, placed, pinsByNet, wires]);

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
    setWiring({ from, cursor: from.point, horizontalFirst: true, legs: [], manual: false });
    setAnnouncement(`Wiring from ${from.component}.${from.pin}. Choose a destination pin, or press Escape.`);
  }, []);

  const addWireBend = useCallback((point: Point) => {
    if (!wiring) return;
    const end = snapPoint(point);
    const last = wiring.legs.at(-1)?.at(-1);
    const start = last ? { x: last[0], y: last[1] } : wiring.from.point;
    if (end.x === start.x && end.y === start.y) return;
    const leg = !wiring.manual && smartWires ? routeConnection(project, catalog, wiring.from, { point: end }) : null;
    setWiring({ ...wiring, cursor: end, manual: true, legs: [...wiring.legs, leg ?? routeOrthogonal(start, end, wiring.horizontalFirst)] });
    setAnnouncement('Bend placed. Click another bend or a destination. Backspace removes the last bend.');
  }, [wiring, smartWires, project, catalog]);

  const finishWire = useCallback(
    (to: PinRef | NetRef) => {
      if (!wiring) return;
      const points = extendWirePath(wiring.from.point, wiring.legs, to.point, wiring.horizontalFirst);
      setWiring(null);
      if ('component' in to && to.component === wiring.from.component && to.pin === wiring.from.pin) return;
      onWire?.(wiring.from, to, points, wiring.manual);
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
    | { kind: 'reshape'; index: number; segment: number; start: Point; source: Project; horizontal: boolean; offset: number }
    | { kind: 'pan'; startX: number; startY: number; vp: Viewport; moved: boolean }
    | { kind: 'move'; ids: string[]; start: Point; moved: boolean }
    | { kind: 'marquee'; start: Point; additive: boolean }
    | { kind: 'wire'; moved: boolean }
    | null
  >(null);

  useEffect(() => {
    gesture.current = null;
    svgRef.current?.removeAttribute('data-dragging');
    setWirePreview(null);
    setDragOffset(null);
    setMarquee(null);
    setGuides({});
    setWiring(null);
  }, [project]);

  const onPointerDown = (e: ReactPointerEvent<SVGSVGElement>) => {
    if (e.button !== 0 && e.button !== 1) return;
    const target = e.target as Element;
    const svg = svgRef.current;
    if (!svg) return;
    const world = toWorld(e.clientX, e.clientY);
    svg.focus({ preventScroll: true });

    const segmentEl = target.closest('[data-wire-segment]');
    if (segmentEl && e.button === 0 && !readOnly && onReshapeWire && !wiring) {
      const index = Number(segmentEl.getAttribute('data-wire-index'));
      const segment = Number(segmentEl.getAttribute('data-wire-segment'));
      const edge = wireSegments(wires[index]!)[segment];
      if (!edge) return;
      e.preventDefault();
      svg.setPointerCapture(e.pointerId);
      gesture.current = { kind: 'reshape', index, segment, source: project, start: world, horizontal: edge[0].y === edge[1].y, offset: 0 };
      return;
    }

    const pinEl = target.closest('[data-pin]');
    const compEl = target.closest('[data-component]');
    const wireEl = target.closest('[data-wire]');

    if (pinEl && e.button === 0 && mode === 'probe') { const pin = findPinAt(world, 12); if (pin?.net) onNetClick?.(pin.net, pin.point); return; }

    if (pinEl && e.button === 0 && !readOnly && mode !== 'probe' && mode !== 'interact') {
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
        addWireBend(world);
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
        setWiring({ from, cursor: from.point, horizontalFirst: true, legs: [], manual: false });
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
      if (!readOnly && mode === 'select' && ids.includes(id) && !ids.some((partId) => placedById.get(partId)?.layout.locked)) {
        svg.setPointerCapture(e.pointerId);
        gesture.current = { kind: 'move', ids, start: world, moved: false };
        svg.setAttribute('data-dragging', 'component');
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
    svg.setAttribute('data-dragging', 'pan');
    gesture.current = { kind: 'pan', startX: e.clientX, startY: e.clientY, vp: vpRef.current, moved: false };
  };

  const moveDelta = (ids: string[], start: Point, world: Point): { delta: Point; x?: number; y?: number } => {
    const delta = { x: snap(world.x - start.x), y: snap(world.y - start.y) };
    return alignmentGuides ? magneticDrag(placed.filter((p) => ids.includes(p.id)).map((p) => p.bounds), placed.filter((p) => !ids.includes(p.id)).map((p) => p.bounds), delta, 12 / vpRef.current.k) : { delta };
  };

  const onPointerMove = (e: ReactPointerEvent<SVGSVGElement>) => {
    const g = gesture.current;
    const world = toWorld(e.clientX, e.clientY);
    if (wiring) {
      const pin = findPinAt(world, 10);
      setWiring((w) => (w ? { ...w, cursor: pin ? pin.point : snapPoint(world) } : w));
    }
    if (!g) return;
    if (g.kind === 'reshape') {
      const offset = snap(g.horizontal ? world.y - g.start.y : world.x - g.start.x);
      g.offset = offset;
      const result = shiftWireSegment(g.source.layout?.wires ?? [], g.index, g.segment, offset, pinsByNet);
      setWirePreview(result ? { source: g.source, wires: result } : null);
    } else if (g.kind === 'pan') {
      const dx = e.clientX - g.startX;
      const dy = e.clientY - g.startY;
      if (!g.moved && Math.hypot(dx, dy) < 3) return;
      g.moved = true;
      setViewport({ x: g.vp.x + dx, y: g.vp.y + dy, k: g.vp.k }, false);
    } else if (g.kind === 'move') {
      const aligned = moveDelta(g.ids, g.start, world);
      const { x: dx, y: dy } = aligned.delta;
      setGuides({ x: aligned.x, y: aligned.y });
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
    svgRef.current?.removeAttribute('data-dragging');
    const svg = svgRef.current;
    if (svg?.hasPointerCapture(e.pointerId)) svg.releasePointerCapture(e.pointerId);
    const world = toWorld(e.clientX, e.clientY);
    if (!g) return;
    if (g.kind === 'reshape') {
      setWirePreview(null);
      if (!readOnly && project === g.source && g.offset) onReshapeWire?.(g.index, g.segment, g.offset, g.source);
      return;
    }
    if (g.kind === 'pan') {
      if (g.moved) setViewportState(vpRef.current);
      else if (mode !== 'probe') onSelectionChange(EMPTY_SELECTION);
      return;
    }
    if (g.kind === 'move') {
      const { x: dx, y: dy } = moveDelta(g.ids, g.start, world).delta;
      setGuides({});
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

  const cancelGesture = () => {
    gesture.current = null;
    svgRef.current?.removeAttribute('data-dragging');
    setWirePreview(null);
    setDragOffset(null);
    setGuides({});
    setMarquee(null);
  };

  const openContext = (target: Element, x: number, y: number) => {
    if (!onCanvasContext) return;
    cancelGesture();
    setWiring(null);
    const component = target.closest('[data-component]')?.getAttribute('data-component');
    const wireIndex = target.closest('[data-wire]')?.getAttribute('data-wire') ?? target.closest('[data-wire-index]')?.getAttribute('data-wire-index');
    const index = wireIndex === undefined ? -1 : Number(wireIndex);
    const contextSelection = component ? (selection.components.includes(component) ? selection : { components: [component], wires: [] }) :
      index >= 0 ? (selection.wires.includes(index) ? selection : { components: [], wires: [index] }) : selection;
    onSelectionChange(contextSelection);
    onCanvasContext({ x, y, point: toWorld(x, y), selection: contextSelection, net: index >= 0 ? wires[index]?.net : undefined });
  };

  const onDoubleClick = (e: React.MouseEvent<SVGSVGElement>) => {
    const target = e.target as Element;
    // the press captured the pointer, so the event may target the svg itself:
    // find the part under the pointer by its bounds
    const world = toWorld(e.clientX, e.clientY);
    const hit = target.closest('[data-component]')?.getAttribute('data-component') ?? [...placed].reverse().find((p) => rectContainsRect(p.bounds, { x: world.x, y: world.y, width: 0, height: 0 }))?.id;
    if (hit) onOpenComponent?.(hit);
    const labelEl = target.closest('[data-net-label]');
    if (labelEl && !readOnly) onRenameNet?.(labelEl.getAttribute('data-net-label')!);
  };

  // --- keyboard ------------------------------------------------------------------

  const onKeyDown = (e: KeyboardEvent<SVGSVGElement>) => {
    const target = e.target as Element;
    const onPin = target.closest('[data-pin]');
    if (e.key === 'ContextMenu' || (e.shiftKey && e.key === 'F10')) {
      e.preventDefault();
      const box = target.getBoundingClientRect();
      openContext(target, box.x + box.width / 2, box.y + box.height / 2);
      return;
    }
    const segmentEl = target.closest('[data-wire-segment]');
    if (segmentEl && !readOnly && onReshapeWire && e.key.startsWith('Arrow')) {
      e.preventDefault();
      const index = Number(segmentEl.getAttribute('data-wire-index'));
      const segment = Number(segmentEl.getAttribute('data-wire-segment'));
      const edge = wireSegments(wires[index]!)[segment];
      if (!edge) return;
      const horizontal = edge[0].y === edge[1].y;
      const delta = horizontal ? (e.key === 'ArrowDown' ? 1 : e.key === 'ArrowUp' ? -1 : 0) : (e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0);
      if (delta) onReshapeWire(index, segment, delta * GRID * (e.shiftKey ? 5 : 1), project);
      return;
    }
    const onComponent = target.closest('[data-component]');
    if (wiring && ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(e.key)) {
      e.preventDefault();
      const step = e.shiftKey ? GRID * 5 : GRID;
      setWiring({ ...wiring, cursor: {
        x: wiring.cursor.x + (e.key === 'ArrowRight' ? step : e.key === 'ArrowLeft' ? -step : 0),
        y: wiring.cursor.y + (e.key === 'ArrowDown' ? step : e.key === 'ArrowUp' ? -step : 0),
      } });
      svgRef.current?.focus({ preventScroll: true });
      return;
    }
    if (wiring && e.key === 'Enter' && target === svgRef.current) {
      e.preventDefault();
      const pin = findPinAt(wiring.cursor, .1);
      const wire = pin ? null : findWireAt(wiring.cursor, .1);
      if (pin) finishWire(pin);
      else if (wire) finishWire({ net: wire.wire.net, point: wire.point });
      else addWireBend(wiring.cursor);
      return;
    }
    const wireEl = target.closest('[data-wire]');
    if (wireEl && !wiring && (e.key === 'Enter' || e.key === ' ')) {
      e.preventDefault();
      const index = Number(wireEl.getAttribute('data-wire'));
      if (mode === 'probe') { const wire = wires[index]; if (wire) onNetClick?.(wire.net, {x:wire.points[0]![0],y:wire.points[0]![1]}); return; }
      onSelectionChange(e.shiftKey ? { components: selection.components, wires: selection.wires.includes(index) ? selection.wires.filter((i) => i !== index) : [...selection.wires, index] } : { components: [], wires: [index] });
      return;
    }
    if (e.key === 'Enter' || (e.key === ' ' && !wiring)) {
      if (onPin) {
        e.preventDefault();
        const [component, pin] = (onPin.getAttribute('data-pin') ?? '').split('.');
        const c = placedById.get(component ?? '');
        const p = c?.pins.find((x) => x.name === pin);
        if (c && p) {
          if (mode === 'probe' && p.net) onNetClick?.(p.net, p.point);
          else if (mode === 'interact') { onSelectionChange({ components: [c.id], wires: [] }); if (p.net) onHighlightNet?.(p.net); }
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
    if (wiring && (e.key === 'Backspace' || e.key === 'Delete' || e.key === ' ')) {
      e.preventDefault();
      if (e.key === ' ') {
        setWiring({ ...wiring, horizontalFirst: !wiring.horizontalFirst, manual: true });
        setAnnouncement('Wire elbow flipped.');
      } else {
        setWiring({ ...wiring, legs: wiring.legs.slice(0, -1) });
        setAnnouncement('Last bend removed.');
      }
      return;
    }
    if (e.key === 'Escape') {
      cancelGesture();
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
      case 'l':
      case 'L':
        if (selection.wires.length === 1 && !readOnly) {
          e.preventDefault();
          onLabelWire?.(selection.wires[0]!);
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
      case 'f':
      case 'F':
        e.preventDefault();
        focusBounds([...placed.filter((p) => selection.components.includes(p.id)).map((p) => p.bounds), ...wires.flatMap((w, i) => selection.wires.includes(i) ? w.points.map(([x, y]) => ({ x, y, width: 0, height: 0 })) : [])]);
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
      const net = p.target.startsWith('net:') ? p.target.slice(4) : p.target.startsWith('pin:') ? pinNet.get(p.target.slice(4)) : undefined;
      if (net) (map.get(net) ?? map.set(net, []).get(net)!).push(p);
    }
    return map;
  }, [probes, pinNet]);

  const showHover = useCallback<HoverFn>((text, sub, clientX, clientY) => {
    const rect = svgRef.current?.getBoundingClientRect();
    setHover({ text, sub, x: clientX - (rect?.left ?? 0), y: clientY - (rect?.top ?? 0) });
  }, []);
  const hideHover = useCallback(() => setHover(null), []);

  const netVisual = (net: string): NetVisual => (live ? describeNetState(netStates?.[net], analogRange) : { kind: 'idle', text: '' });
  const selectedNets = new Set(selection.wires.map((i) => wires[i]?.net).filter((n): n is string => !!n));
  const highlightNet = requestedHighlightNet ?? (selectedNets.size === 1 ? [...selectedNets][0]! : null);
  const electricalIssues = useMemo(() => live ? liveElectricalIssues(project, catalog, netStates ?? {}) : [], [live, project, catalog, netStates]);
  const [showElectricalIssues, setShowElectricalIssues] = useState(false);

  const wireTarget = wiring ? findPinAt(wiring.cursor, .1) : null;
  const smartPreview = wiring && !wiring.manual && smartWires ? routeConnection(project, catalog, wiring.from, wireTarget ?? { point: wiring.cursor, net: findWireAt(wiring.cursor, .1)?.wire.net }) : null;
  const routeBlocked = !!wiring && !wiring.manual && smartWires && !smartPreview;
  const wiringPreview = wiring ? smartPreview ?? extendWirePath(wiring.from.point, wiring.legs, wiring.cursor, wiring.horizontalFirst) : null;
  const showPins = !readOnly || mode === 'probe' || mode === 'interact';
  const cursorClass = mode === 'wire' || wiring ? 'es-scene--wire' : mode === 'probe' ? 'es-scene--probe' : '';

  const mapRects = [...placed.map((p) => p.bounds), visibleWorld];
  const mapX = Math.min(0, ...mapRects.map((r) => r.x)) - 40;
  const mapY = Math.min(0, ...mapRects.map((r) => r.y)) - 40;
  const mapW = Math.max(100, ...mapRects.map((r) => r.x + r.width)) - mapX + 40;
  const mapH = Math.max(100, ...mapRects.map((r) => r.y + r.height)) - mapY + 40;

  return (
    <div className={`es-scene es-scene--${appearance.palette} es-scene--wire-${appearance.wireColor ?? 'green'} ${cursorClass} ${readOnly ? 'es-scene--readonly' : ''} ${className ?? ''}`.trim()}>
      <div ref={backgroundRef} className="es-scene__background" style={canvasBackground(appearance, viewport, showGrid)} aria-hidden="true" />
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
        onPointerCancel={() => { cancelGesture(); setWiring(null); }}
        onContextMenu={(e) => { if (onCanvasContext) { e.preventDefault(); openContext(e.target as Element, e.clientX, e.clientY); } }}
        onDoubleClick={onDoubleClick}
        onKeyDown={onKeyDown}
        onDragOver={onDragOver}
        onDragLeave={() => setDropHint(null)}
        onDrop={onDrop}
        onPointerLeave={() => setHover(null)}
      >
        <g ref={sceneRef} transform={`translate(${viewport.x} ${viewport.y}) scale(${viewport.k})`}>
          <g className="es-scene__layer es-scene__layer--wires">
            {showAirwires && ratsnest.map((r, i) => (
              <line key={`rat-${i}`} className="es-scene__ratsnest" x1={r.a.x} y1={r.a.y} x2={r.b.x} y2={r.b.y} />
            ))}
            {wires.map((w, i) => {
              const vis = netVisual(w.net);
              return <g key={`w-${i}`} opacity={highlightNet && highlightNet !== w.net ? 0.18 : 1}><WireView selectable={!readOnly || mode === 'probe' || mode === 'select' || mode === 'interact'} index={i} wire={w} vis={vis} selected={selection.wires.includes(i)} sub={live ? vis.text : `${project.nets[w.net]?.length ?? 0} pins`} onHover={showHover} onLeave={hideHover} /></g>;
            })}
            {buses.map((b, i) => {
              const pts = b.points.map(([x, y]) => `${x},${y}`).join(' ');
              const [x0, y0] = b.points[0]!;
              const value = live ? busText(busValue(b.nets, netStates, analogRange), b.nets.length) : '';
              return (
                <g key={`b-${i}`} data-bus={b.name} className="es-bus" aria-label={`Bus ${b.name}: ${b.nets.join(', ')}${value ? `, ${value}` : ''}`} role="img">
                  <polyline className="es-bus__line" points={pts} />
                  <text className="es-bus__name" x={x0 + 4} y={y0 - 5}>
                    {b.name}
                    {value && <tspan className="es-bus__value">{` = ${value}`}</tspan>}
                  </text>
                </g>
              );
            })}
            {junctions.map((j, i) => {
              const vis = netVisual(j.net);
              return <circle key={`j-${i}`} className={`es-junction es-wire--${vis.kind}`} cx={j.point.x} cy={j.point.y} r={highlightNet === j.net ? 3.5 : 2.4} opacity={highlightNet && highlightNet !== j.net ? .18 : 1} style={vis.color ? { fill: vis.color } : undefined} />;
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--groups">
            {(project.layout?.groups ?? []).map((group) => {
              const members = group.members.flatMap((id) => { const part = placedById.get(id); if (!part) return []; const offset = selection.components.includes(id) ? dragOffset : null; return [{ ...part.bounds, x: part.bounds.x + (offset?.x ?? 0), y: part.bounds.y + (offset?.y ?? 0) }]; });
              if (!members.length) return null;
              const x = Math.min(...members.map((r) => r.x)) - 22, y = Math.min(...members.map((r) => r.y)) - 42;
              const width = Math.max(...members.map((r) => r.x + r.width)) - x + 22;
              const height = Math.max(...members.map((r) => r.y + r.height)) - y + 22;
              return <g key={group.id} data-group={group.id} className="es-schematic-group">
                <rect x={x} y={y} width={width} height={height} rx={12} pointerEvents="none" />
                <g role="button" tabIndex={0} aria-label={`Select group ${group.name}`} onPointerDown={(e) => {
                  if (e.button !== 0) return;
                  e.stopPropagation(); e.currentTarget.focus();
                  onSelectionChange({ components: group.members, wires: [] });
                  if (!readOnly && mode === 'select' && !group.members.some((id) => placedById.get(id)?.layout.locked)) {
                    svgRef.current?.setPointerCapture(e.pointerId);
                    svgRef.current?.setAttribute('data-dragging', 'component');
                    gesture.current = { kind: 'move', ids: group.members, start: toWorld(e.clientX, e.clientY), moved: false };
                  }
                }} onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); onSelectionChange({ components: group.members, wires: [] }); } }}>
                  <rect x={x + 8} y={y + 3} width={Math.max(100, group.name.length * 8 + 24)} height={24} rx={6} />
                  <text x={x + 18} y={y + 20}>{group.name}</text>
                </g>
              </g>;
            })}
          </g>
          <g className="es-scene__layer es-scene__layer--components">
            {visible.filter((c) => c.layout.locked).map((c) => <text key={`lock-${c.id}`} className="es-placement-lock" x={c.bounds.x + c.bounds.width - 8} y={c.bounds.y - 3} pointerEvents="none" aria-label={`${c.id} placement locked`}>▣</text>)}
            {visible.map((c) => {
              const selected = selectedSet.has(c.id);
              const ownOutputs = Object.fromEntries((c.symbol?.symbol.states ?? []).map((s) => [s.output, outputs[`${c.id}.${s.output}`]]).filter(([, value]) => value !== undefined)) as Record<string, OutputValue>;
              const ownInputs = inputs.filter((i) => i.target === c.id || i.target.startsWith(`${c.id}.`));
              const pinVisuals = showPins ? c.pins.map((pin) => (pin.net ? netVisual(pin.net) : IDLE)) : [];
              return (
                <PartView
                  key={c.id}
                  c={c}
                  selected={selected}
                  offset={selected && dragOffset ? dragOffset : null}
                  showPins={showPins}
                  live={Boolean(live)}
                  pinVisuals={pinVisuals}
                  pinKey={pinVisuals.map((v) => `${v.kind}|${v.color ?? ''}|${v.text}`).join(';')}
                  activePin={wiring && wiring.from.component === c.id ? wiring.from.pin : null}
                  highlightNet={highlightNet}
                  outputs={ownOutputs}
                  outputKey={JSON.stringify(ownOutputs)}
                  inputs={ownInputs}
                  inputKey={JSON.stringify(ownInputs)}
                  interactive={mode === 'interact' && !!onInput}
                  onInput={onInput}
                  onHover={showHover}
                  onLeave={hideHover}
                />
              );
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--labels">
            {labels.map((l, i) => {
              const vis = netVisual(l.net);
              return (
                <g key={`l-${i}`} data-net-label={l.net} opacity={highlightNet && highlightNet !== l.net ? .25 : 1} className={`es-net-label es-wire--${vis.kind}`} transform={`translate(${l.x} ${l.y}) rotate(${l.rotation ?? 0})`}>
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
            {[...probeNets.entries()].flatMap(([net, list]) => list.map((probe) => {
              const w = wires.find((x) => x.net === net);
              const key = `${probe.probe}:${net}`;
              const at = (probePoints.project === project ? probePoints.points[key] : undefined) ?? (w ? { x: w.points[0]![0], y: w.points[0]![1] } : pinsByNet[net]?.[0]);
              if (!at) return null;
              return <MeasurementProbe key={String(probe.probe)} label={String(probe.probe)} side="b" net={net} at={at} onDrop={onMoveProbe && ['analog', 'digital'].includes(probe.kind) ? (_, client) => {
                const world = toWorld(client.x, client.y), pin = findPinAt(world, 12), wireHit = findWireAt(world, 10), hit = pin?.net ?? wireHit?.wire.net;
                if (!hit) { setAnnouncement('No connected wire or pin at the probe drop; connection preserved.'); return; }
                const point = pin?.net ? pin.point : wireHit!.point;
                void onMoveProbe(String(probe.probe), hit).then(() => { setProbePoints((old) => ({ project, points: { ...(old.project === project ? old.points : {}), [`${probe.probe}:${hit}`]: point } })); setAnnouncement(hit === net ? `Waveform probe ${probe.probe} repositioned on ${hit}; capture preserved.` : `Waveform probe ${probe.probe} moved to ${hit}; new capture started.`); }).catch(() => setAnnouncement('Probe move rejected; connection preserved.'));
              } : undefined} />;
            }))}
          </g>

          <g className="es-scene__layer es-scene__layer--comparison">
            {(['a', 'b'] as const).map((side) => {
              const net = voltageComparison?.[side]; if (!net || !project.nets[net]) return null;
              const wire = wires.find((w) => w.net === net);
              const at = (side === 'a' ? voltageComparison?.aPoint : voltageComparison?.bPoint) ?? (wire ? { x: wire.points[0]![0], y: wire.points[0]![1] } : pinsByNet[net]?.[0]);
              return at ? <MeasurementProbe key={`${side}-${net}`} side={side} net={net} at={at} onDrop={onCompareNet ? (lead, client) => {
                const world = toWorld(client.x, client.y), pin = findPinAt(world, 12), wireHit = findWireAt(world, 10), hit = pin?.net ?? wireHit?.wire.net;
                if (hit) { onCompareNet(lead, hit, pin?.net ? pin.point : wireHit!.point); setAnnouncement(`Voltage probe ${lead.toUpperCase()} moved to ${hit}.`); }
                else setAnnouncement('No connected wire or pin at the probe drop; connection preserved.');
              } : undefined} /> : null;
            })}
          </g>

          <g className="es-scene__layer es-scene__layer--component-labels" pointerEvents="none">
            {visible.map((part) => {
              const label = componentLabels.get(part.id);
              if (!label) return null;
              const offset = selection.components.includes(part.id) ? dragOffset : null;
              return <g key={part.id} data-component-label={part.id} transform={`translate(${label.x + (offset?.x ?? 0)} ${label.y + (offset?.y ?? 0)})`}>
                <title>{`${part.id} · ${part.symbol?.name ?? part.componentId}${part.value ? ` · ${part.value}` : ''}`}</title>
                <text className="es-component__ref" x={0} y={11}>{part.id}</text>
                {label.name && <text className="es-component__name" x={0} y={23}>{label.name}</text>}
                {part.value && <text className="es-component__value" x={0} y={label.name ? 36 : 24}>{part.value}</text>}
              </g>;
            })}
          </g>
          <g className="es-scene__layer es-scene__layer--selection">
            {guides.x !== undefined && <line className="es-scene__alignment-guide" x1={guides.x} x2={guides.x} y1={visibleWorld.y} y2={visibleWorld.y + visibleWorld.height} />}
            {guides.y !== undefined && <line className="es-scene__alignment-guide" y1={guides.y} y2={guides.y} x1={visibleWorld.x} x2={visibleWorld.x + visibleWorld.width} />}
            {marquee && <rect className="es-scene__marquee" x={marquee.x} y={marquee.y} width={marquee.width} height={marquee.height} />}
            {issueMarker && <g className={`es-scene__issue-marker es-scene__issue-marker--${issueMarker.severity}`} pointerEvents="none" role="img" aria-label={issueMarker.label}>
              <circle cx={issueMarker.point.x} cy={issueMarker.point.y} r={12 / viewport.k} />
              <circle cx={issueMarker.point.x} cy={issueMarker.point.y} r={3 / viewport.k} />
            </g>}
            {!readOnly && !wiring && !wirePreview && mode === 'select' && onReshapeWire && selection.wires.length === 1 && (() => {
              const index = selection.wires[0]!;
              const wire = wires[index];
              return wire && wireSegments(wire).map(([a, b], segment) => a.x === b.x || a.y === b.y ? (
                <circle key={`${index}:${segment}`} data-wire-index={index} data-wire-segment={segment}
                  className="es-scene__segment-handle" cx={(a.x + b.x) / 2} cy={(a.y + b.y) / 2} r={5 / viewport.k}
                  role="button" tabIndex={0} aria-label={`Move ${wire.net} segment ${segment + 1} ${a.y === b.y ? 'vertically' : 'horizontally'}`}
                  style={{ cursor: a.y === b.y ? 'ns-resize' : 'ew-resize' }}>
                  <title>Drag to reshape · arrows move by 10 · Shift moves by 50 · Escape cancels</title>
                </circle>
              ) : null);
            })()}
            {wiring?.legs.map((leg, i) => { const p = leg.at(-1)!; return <circle className="es-scene__wire-waypoint" key={i} cx={p[0]} cy={p[1]} r={3 / viewport.k} fill="var(--accent)" pointerEvents="none" />; })}
            {wiringPreview && <polyline className={`es-scene__wire-preview${routeBlocked ? ' is-blocked' : ''}`} points={wiringPreview.map(([x, y]) => `${x},${y}`).join(' ')} />}
            {dropHint && <rect className="es-scene__drop-hint" x={dropHint.x - 5} y={dropHint.y - 5} width={10} height={10} />}
          </g>
        </g>
      </svg>
      {highlightNet && <div className="es-net-spotlight" role="status"><strong>{highlightNet}</strong><span>{project.nets[highlightNet]?.length ?? 0} terminals{live ? ` · ${netVisual(highlightNet).text}` : ''}</span></div>}
      {electricalIssues.length > 0 && <div className="es-electrical-feedback" aria-label="Live electrical warnings">
        <button type="button" aria-expanded={showElectricalIssues} onClick={() => setShowElectricalIssues((v) => !v)}>
          <span aria-hidden="true">⚠</span> {electricalIssues.length} electrical warning{electricalIssues.length === 1 ? '' : 's'} <span>{showElectricalIssues ? 'Hide' : 'Review'}</span>
        </button>
        {showElectricalIssues && <ul>{electricalIssues.map((issue) => <li key={`${issue.code}:${issue.net}`}><button type="button" onClick={() => {
          onSelectionChange({ components: [], wires: wires.flatMap((w, i) => w.net === issue.net ? [i] : []) });
          onHighlightNet?.(issue.net);
          const points = pinsByNet[issue.net] ?? [];
          focusBounds([...points.map((p) => ({ ...p, width: 0, height: 0 })), ...wires.filter((w) => w.net === issue.net).flatMap((w) => w.points.map(([x, y]) => ({ x, y, width: 0, height: 0 })))]);
        }}><strong>{issue.title}</strong><small>{issue.detail}</small></button></li>)}</ul>}
      </div>}
      {/* a narrow canvas keeps its sheet: the navigator would cover a third of it */}
      {showMinimap && size.width >= 640 && (
        <div className="es-scene__navigator" aria-label="Sheet navigator">
          <span>Navigator</span>
          <svg viewBox={`${mapX} ${mapY} ${mapW} ${mapH}`} preserveAspectRatio="none" role="img" aria-label="Schematic overview. Click to navigate." onClick={(e) => {
            const r = e.currentTarget.getBoundingClientRect();
            const x = mapX + (e.clientX - r.left) / r.width * mapW;
            const y = mapY + (e.clientY - r.top) / r.height * mapH;
            setViewport({ ...vpRef.current, x: size.width / 2 - x * vpRef.current.k, y: size.height / 2 - y * vpRef.current.k });
          }}>
            {wires.map((w, i) => <polyline key={i} points={w.points.map((p) => p.join(',')).join(' ')} className="es-scene__map-wire" />)}
            {placed.map((p) => <rect key={p.id} {...p.bounds} className={selectedSet.has(p.id) ? 'es-scene__map-part is-selected' : 'es-scene__map-part'} />)}
            <rect {...visibleWorld} className="es-scene__map-viewport" />
          </svg>
          <button type="button" onClick={zoomToFit}>Fit sheet <kbd>0</kbd></button>
        </div>
      )}
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
        {ratsnest.length > 0 && <span>{ratsnest.length} undrawn connection{ratsnest.length === 1 ? '' : 's'}</span>}
        {routeBlocked && <span>No clear route · move the cursor or rearrange overlapping parts</span>}
        {wiring && <span>wiring from {wiring.from.component ? `${wiring.from.component}.${wiring.from.pin}` : wiring.from.net} · click to place bends · Space flips elbow · Backspace removes bend · Esc cancels</span>}
        {mode === 'wire' && !wiring && <span>click a pin to start a wire</span>}
        {mode === 'probe' && <span>click a wire or pin to add a probe</span>}
      </div>
    </div>
  );
}

export const MemoSchematicCanvas = memo(SchematicCanvas);
