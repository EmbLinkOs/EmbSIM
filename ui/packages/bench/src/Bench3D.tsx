// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The 3D bench (ADR-027, Q12: Three.js): the project's boards, breadboards
// and parts on a desk, wired with jumpers, lit as a desk is, seen through a
// camera the user orbits (right drag), pans (middle drag or shift) and
// zooms (wheel). In the select tool a part is dragged on the desk and
// snaps its legs into a breadboard's holes; R turns it, Delete removes it.
// In the wire tool a press on a hole or a pin starts a jumper and the
// release on another ends it. While the circuit runs, buttons press, switches
// flip and knobs turn under the pointer, and LEDs light from their
// luminance. Frames are drawn on demand (a change, a camera move), never
// in a loop while nothing moves.

import type { ArtworkState, BenchNote, BenchPlace, Hotspot, OutputValue, WorldBody } from '@embsim/protocol';
import { useCallback, useEffect, useImperativeHandle, useRef, useState, type KeyboardEvent, type Ref } from 'react';

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { CSS2DObject, CSS2DRenderer } from 'three/examples/jsm/renderers/CSS2DRenderer.js';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';
import { ArtworkSurface } from './artwork';
import { buildPart, indicatorLight, type Built, type PickData } from './parts3d';
import { updateEmission } from './emission';
import type { BenchShape } from './shape';
import { pickTerminal, type ProjectedTerminal } from './picking';
import type { InsertionPreview } from './layout';
import type { Beam } from './world';
import { ALL_LAYERS, aboveHeight, describeMeasurement, layerOf, measure, sectionPlane, snapPoint, viewPose, type BenchLayers, type Measurement, type Section, type StandardView, type Vec3 } from './inspect';
import { OutlineRenderer, QualityRenderer, type RenderQuality } from './render';



export interface BenchPartView {
  id: string;
  label: string;
  component: string;
  params?: Record<string, unknown>;
  shape: BenchShape;
  place: Required<BenchPlace>;
  /** the height of the surface it stands on (a breadboard's top, or the desk) */
  elevation: number;
  artwork?: { svg: string; states?: ArtworkState[]; hotspots?: Hotspot[]; vars?: Record<string, string> };
  inputs?: Record<string, unknown>;
  outputs?: Record<string, OutputValue>;
}

export type Point3 = [number, number, number];

export interface JumperView {
  index: number;
  a: Point3;
  b: Point3;
  color: string;
  /** the ends sit in a breadboard (a bare pin) or on a header (a Dupont housing) */
  housings?: [boolean, boolean];
  points?: Point3[];
  rise_mm?: number;
  traced?: boolean;
}

export interface LinkView {
  a: Point3;
  b: Point3;
  color: string;
}

export interface SnapResult {
  x: number;
  y: number;
  /** the holes the legs would be plugged into */
  holes: Point3[];
  elevation: number;
  insertion?: InsertionPreview;
  carried?: { id: string; x: number; y: number; rotation: number }[];
}

export interface TerminalHit {
  ref: string;
  at: Point3;
  occupiedBy?: string[];
}

export type BenchTool = 'select' | 'wire' | 'measure';

export type Projection = 'perspective' | 'orthographic';

export interface Bench3DHandle {
  /** frames the camera on everything on the bench (or the selection) from a standard view */
  frame(view?: StandardView, selectedOnly?: boolean): void;
  /** the canvas, for screenshots */
  canvas(): HTMLCanvasElement | null;
  /** a still of the view at `scale` times its resolution, as PNG */
  exportImage(scale: number): Promise<Blob | null>;
  /** the bench as glTF binary, in metres, each part a named node */
  exportGlb(): Promise<Blob>;
  /** removes the measurement drawn in the scene */
  clearMeasurement(): void;
}

export interface Bench3DProps {
  parts: BenchPartView[];
  jumpers: JumperView[];
  links: LinkView[];
  notes: BenchNote[];
  /** the world's bodies (ADR-026) and the beams of the sensors that see them */
  bodies?: WorldBody[];
  beams?: Beam[];
  onMoveBody?(id: string, at: [number, number, number]): void;
  tool: BenchTool;
  /** inputs act (the circuit is running) */
  live: boolean;
  /** edits allowed (moves, wires) */
  editable: boolean;
  wireColor: string;
  selected?: { part?: string; jumper?: number };
  snap(id: string, x: number, y: number, rotation: number): SnapResult;
  terminals: TerminalHit[];
  traceTerminals?: string[];
  onRoute?(index: number, points: Point3[]): void;
  surface?: 'mat' | 'paper' | 'graphite';
  labels?: 'adaptive' | 'all' | 'selected';
  showNotes?: boolean;
  insertionPreview?: InsertionPreview;
  onRendererError?(): void;
  onMove(id: string, place: Required<BenchPlace>): void;
  onWire(from: string, to: string): void;
  onSelect(sel: { part?: string; jumper?: number }): void;
  onDelete(sel: { part?: string; jumper?: number }): void;
  onInput(target: string, input: string, value: unknown): void;
  label?: string;
  handle?: Ref<Bench3DHandle>;
  /** inspection (docs/engineering/bench-3d.md, stage 1): presentation only */
  projection?: Projection;
  quality?: RenderQuality;
  /** quality could not keep the frame budget and dropped to draft */
  onQualityFallback?(): void;
  section?: Section | null;
  /** a plane this many mm above the desk; the parts through it are reported */
  heightPlane?: number | null;
  onHeightReport?(above: { id: string; top: number }[]): void;
  layers?: BenchLayers;
  /** the measure tool's result, or null when cleared */
  onMeasure?(m: Measurement | null): void;
  /** 0 to 1: lifts each part plugged into a breadboard off it (an exploded view) */
  explode?: number;
}

interface PartEntry {
  key: string;
  built: Built;
  surface?: ArtworkSurface;
  label: CSS2DObject;
  view: BenchPartView;
}

const UP = 6; // a jumper rises this far from its ends before it arcs

function v3([x, y, z]: Point3): THREE.Vector3 {
  return new THREE.Vector3(x, y, z);
}

/** how far an exploded view lifts a plugged part off its breadboard, mm */
export const EXPLODE_LIFT = 25;

/** a wire's radius and how far it lies above what it rests on */
const WIRE_R = 0.8;
/** a span at least this long drapes down onto the desk rather than arcing over it */
const DRAPE_SPAN = 70;

/**
 * A jumper's path. Between two holes of one surface it lies flat, as a
 * pre-cut breadboard jumper does: up 1.5 mm, along, down. Anywhere else it
 * is a flexible Dupont wire: up from each end, then an arc that rises with
 * its length; a long one (a supply's lead across the desk) falls under its
 * own weight and lies on the desk, or on whatever `floor` says is under it
 * (the height of the bench's top at a point, so it never passes through a
 * board), between its ends.
 */
export function jumperCurve(a: Point3, b: Point3, points?: Point3[], rise?: number, floor?: (x: number, z: number) => number): THREE.Curve<THREE.Vector3> {
  if (points?.length) {
    const path = new THREE.CurvePath<THREE.Vector3>();
    const route = [a, ...points, b].map(v3);
    for (let i = 1; i < route.length; i++) if (route[i - 1]!.distanceToSquared(route[i]!) > 1e-12) path.add(new THREE.LineCurve3(route[i - 1]!, route[i]!));
    if (path.curves.length) return path;
    return new THREE.LineCurve3(v3(a), v3(b));
  }
  const A = v3(a);
  const B = v3(b);
  if (rise !== undefined) {
    const top = Math.max(A.y, B.y) + rise;
    const route = [A, A.clone().setY(top), B.clone().setY(top), B];
    const path = new THREE.CurvePath<THREE.Vector3>();
    for (let i = 1; i < route.length; i++) if (route[i - 1]!.distanceToSquared(route[i]!) > 1e-12) path.add(new THREE.LineCurve3(route[i - 1]!, route[i]!));
    return path.curves.length ? path : new THREE.LineCurve3(A, B);
  }
  const len = A.distanceTo(B);
  if (rise === undefined && Math.abs(A.y - B.y) < 0.01 && len < 60) {
    const h = A.y + 1.5;
    const path = new THREE.CurvePath<THREE.Vector3>();
    const pts = [A, A.clone().setY(h - 0.6), A.clone().setY(h).lerp(B.clone().setY(h), Math.min(0.5, 0.8 / Math.max(len, 0.1))), B.clone().setY(h).lerp(A.clone().setY(h), Math.min(0.5, 0.8 / Math.max(len, 0.1))), B.clone().setY(h - 0.6), B];
    for (let i = 0; i < pts.length - 1; i++) path.add(new THREE.LineCurve3(pts[i]!, pts[i + 1]!));
    return path;
  }
  const span = Math.hypot(B.x - A.x, B.z - A.z);
  if (span >= DRAPE_SPAN) {
    const under = (t: number) => {
      const p = A.clone().lerp(B, t);
      return Math.max(0, floor?.(p.x, p.z) ?? 0) + WIRE_R;
    };
    // out of each socket, then falling to what is below, resting there between
    const at = (t: number, y: number) => A.clone().lerp(B, t).setY(Math.max(y, under(t)));
    const lowA = Math.min(A.y, B.y) * 0.35;
    const route = [
      A,
      A.clone().setY(A.y + UP),
      at(0.12, Math.max(A.y * 0.55, lowA)),
      at(0.28, 0),
      at(0.5, 0),
      at(0.72, 0),
      at(0.88, Math.max(B.y * 0.55, lowA)),
      B.clone().setY(B.y + UP),
      B,
    ];
    // a spline overshoots between its points: clamp a dense sampling of it above what is below
    const rough = new THREE.CatmullRomCurve3(route, false, 'centripetal');
    const span2 = Math.max(span, 1e-6);
    const dense = rough.getPoints(96).map((p, i, all) => {
      if (i === 0 || i === all.length - 1) return p;
      const t = Math.min(1, Math.max(0, ((p.x - A.x) * (B.x - A.x) + (p.z - A.z) * (B.z - A.z)) / (span2 * span2)));
      const lowest = Math.max(0, floor?.(p.x, p.z) ?? 0) + WIRE_R;
      return t > 0.04 && t < 0.96 ? p.setY(Math.max(p.y, lowest)) : p;
    });
    return new THREE.CatmullRomCurve3(dense, false, 'centripetal');
  }
  const lift = rise ?? Math.min(18, 5 + len * 0.1);
  const top = Math.max(A.y, B.y) + lift;
  const mid = A.clone().lerp(B, 0.5);
  mid.y = top;
  return new THREE.CatmullRomCurve3([A, A.clone().setY(A.y + UP), A.clone().lerp(B, 0.2).setY(top - lift * 0.25), mid, B.clone().lerp(A, 0.2).setY(top - lift * 0.25), B.clone().setY(B.y + UP), B], false, 'centripetal');
}

function deskTexture(surface: 'mat' | 'paper' | 'graphite' = 'mat'): THREE.CanvasTexture | undefined {
  if (typeof document === 'undefined') return undefined;
  const c = document.createElement('canvas');
  c.width = c.height = 512;
  const g = c.getContext('2d');
  if (!g) return undefined;
  // an anti-static mat: matte blue-grey with a 10 mm grid
  g.fillStyle = { mat: '#2f4f5f', paper: '#e8e7e3', graphite: '#30343b' }[surface];
  g.fillRect(0, 0, 512, 512);
  g.strokeStyle = surface === 'paper' ? 'rgba(30,40,50,0.07)' : 'rgba(255,255,255,0.07)';
  g.lineWidth = 2;
  for (let i = 0; i <= 512; i += 51.2) {
    g.beginPath();
    g.moveTo(i, 0);
    g.lineTo(i, 512);
    g.moveTo(0, i);
    g.lineTo(512, i);
    g.stroke();
  }
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(100, 100);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function partKey(v: BenchPartView): string {
  return JSON.stringify([v.component, v.params ?? {}, v.shape, v.artwork?.svg ?? '']);
}

function labelElement(text: string): HTMLDivElement {
  const el = document.createElement('div');
  el.className = 'eb-label';
  el.textContent = text;
  return el;
}

/** the CSS transform that turns the view cube as the camera sees the scene */
export function cubeTransform(view: THREE.Matrix4): string {
  const e = view.elements;
  const f = (v: number) => (Math.abs(v) < 1e-10 ? 0 : v).toFixed(6);
  // the rotation only, with y flipped for CSS's y-down axis (as CSS3DRenderer does)
  return `matrix3d(${f(e[0]!)},${f(-e[1]!)},${f(e[2]!)},0,${f(-e[4]!)},${f(e[5]!)},${f(-e[6]!)},0,${f(e[8]!)},${f(-e[9]!)},${f(e[10]!)},0,0,0,0,1)`;
}

/** the view cube's faces: the view each one frames, and where it sits on the cube */
const CUBE_FACES: { view: StandardView; label: string; transform: string }[] = [
  { view: 'front', label: 'Front', transform: 'translateZ(24px)' },
  { view: 'back', label: 'Back', transform: 'rotateY(180deg) translateZ(24px)' },
  { view: 'right', label: 'Right', transform: 'rotateY(90deg) translateZ(24px)' },
  { view: 'left', label: 'Left', transform: 'rotateY(-90deg) translateZ(24px)' },
  { view: 'top', label: 'Top', transform: 'rotateX(90deg) translateZ(24px)' },
  { view: 'bottom', label: 'Bottom', transform: 'rotateX(-90deg) translateZ(24px)' },
];

/** an orthographic camera's frustum, `half` mm above and below its centre */
function setOrthoFrustum(cam: THREE.OrthographicCamera, half: number, aspect: number): void {
  cam.top = half;
  cam.bottom = -half;
  cam.left = -half * aspect;
  cam.right = half * aspect;
}

/** the height of the bench's top under a point: what a draped wire rests on (the parts, not the wires) */
function floorOf(parts: Iterable<{ built: Built }>, camera: THREE.Camera): (x: number, z: number) => number {
  const objects = [...parts].map((e) => e.built.group).filter((g) => g.visible);
  const ray = new THREE.Raycaster();
  // sprites (an LED's glow) need a camera to be tested, though they are then passed over
  ray.camera = camera;
  const down = new THREE.Vector3(0, -1, 0);
  const from = new THREE.Vector3();
  return (x, z) => {
    ray.set(from.set(x, 500, z), down);
    const hit = ray.intersectObjects(objects, true).find((h) => (h.object as THREE.Mesh).isMesh && !(h.object instanceof THREE.Sprite));
    return hit ? hit.point.y : 0;
  };
}

/** a part's body: its meshes, not the glow sprites, lights and labels round it */
function bodyBounds(group: THREE.Object3D): THREE.Box3 {
  const box = new THREE.Box3();
  group.updateWorldMatrix(true, true);
  group.traverse((o) => {
    const m = o as THREE.Mesh;
    if (!m.isMesh || !m.visible || o instanceof THREE.Sprite) return;
    m.geometry.computeBoundingBox();
    if (m.geometry.boundingBox) box.union(m.geometry.boundingBox.clone().applyMatrix4(m.matrixWorld));
  });
  return box;
}

function disposeTree(o: THREE.Object3D): void {
  o.traverse((x) => {
    if (x instanceof CSS2DObject) x.element.remove();
    const m = x as THREE.Mesh;
    m.geometry?.dispose();
    const mat = m.material;
    if (Array.isArray(mat)) mat.forEach((v) => v.dispose());
    else (mat as THREE.Material | undefined)?.dispose();
  });
}

export function Bench3D(props: Bench3DProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const cubeRef = useRef<HTMLDivElement>(null);
  const cancelGesture = useRef<(() => void) | null>(null);
  const [feedback, setFeedback] = useState<{ text: string; x?: number; y?: number; ref?: string; blocked?: boolean }>({ text: '' });
  const propsRef = useRef(props);
  propsRef.current = props;
  useEffect(() => { cancelGesture.current?.(); }, [props.tool, props.editable, props.live]);
  const state = useRef<{
    renderer: THREE.WebGLRenderer;
    desk: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshStandardMaterial>;
    labels: CSS2DRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera | THREE.OrthographicCamera;
    perspective: THREE.PerspectiveCamera;
    orthographic: THREE.OrthographicCamera;
    controls: OrbitControls;
    /** quality mode's passes, when chosen */
    quality: QualityRenderer | null;
    /** the draft render with outlines, made when something is first outlined */
    outline: OutlineRenderer | null;
    /** the part under the pointer, outlined as the selection is */
    hover: string | null;
    /** the measurement, the height plane and its marks */
    inspect: THREE.Group;
    measureFrom: Vec3 | null;
    parts: Map<string, PartEntry>;
    jumpers: THREE.Group;
    links: THREE.Group;
    notes: THREE.Group;
    world: THREE.Group;
    hints: THREE.Group;
    preview: THREE.Group;
    raf: number;
    framed: boolean;
  } | null>(null);

  const invalidate = useCallback(() => {
    const s = state.current;
    if (!s || s.raf) return;
    s.raf = requestAnimationFrame(() => {
      s.raf = 0;
      s.controls.update();
      // the selected part, and the part under the pointer, are outlined
      const outlined = [...new Set([propsRef.current.selected?.part, s.hover])]
        .map((id) => (id ? s.parts.get(id)?.built.group : undefined))
        .filter((g): g is THREE.Group => Boolean(g?.visible));
      if (s.quality) {
        s.quality.outline.selectedObjects = outlined;
        if (!s.quality.draw()) {
          s.quality.dispose();
          s.quality = null;
          propsRef.current.onQualityFallback?.();
        }
      } else if (outlined.length) {
        const host = hostRef.current;
        if (!s.outline && host) {
          s.outline = new OutlineRenderer(s.renderer, s.scene, s.camera, host.clientWidth || 1, host.clientHeight || 1);
          s.outline.setSize(host.clientWidth || 1, host.clientHeight || 1, s.renderer.getPixelRatio());
        }
        if (s.outline) {
          s.outline.setCamera(s.camera);
          s.outline.outline.selectedObjects = outlined;
          s.outline.composer.render();
        } else s.renderer.render(s.scene, s.camera);
      } else s.renderer.render(s.scene, s.camera);
      s.labels.render(s.scene, s.camera);
      // the view cube turns with the camera: the scene's rotation seen from the camera, y down as CSS has it
      if (cubeRef.current) cubeRef.current.style.transform = cubeTransform(s.camera.matrixWorldInverse);
    });
  }, []);

  const frame = useCallback((view?: StandardView, selectedOnly = false) => {
    const s = state.current;
    if (!s) return;
    const toward = s.camera.position.clone().sub(s.controls.target).normalize();
    const mode: StandardView = view ?? (toward.y > 0.99 ? 'top' : 'perspective');
    const box = new THREE.Box3();
    const chosen = selectedOnly && propsRef.current.selected?.part;
    for (const e of s.parts.values()) if (e.built.group.visible && (!chosen || e.view.id === chosen)) box.expandByObject(e.built.group);
    if (!selectedOnly && propsRef.current.showNotes !== false)
      for (const n of propsRef.current.notes) box.expandByPoint(new THREE.Vector3(n.x, 30, n.y));
    // the world's bodies near the bench, and where the beams meet them; what is far
    // (a wall across the room) stays in the distance, as it does from a chair at the desk
    if (!selectedOnly && !box.isEmpty()) {
      const near = box.clone().expandByScalar(400);
      const extra: THREE.Vector3[] = [];
      for (const b of propsRef.current.bodies ?? []) extra.push(new THREE.Vector3(b.at[0], b.at[2], b.at[1]));
      for (const beam of propsRef.current.beams ?? []) if (beam.distance >= 0) extra.push(new THREE.Vector3(beam.to[0], beam.to[2], beam.to[1]));
      for (const v of extra) if (near.containsPoint(v)) box.expandByPoint(v);
    }
    if (box.isEmpty()) box.set(new THREE.Vector3(-100, 0, -60), new THREE.Vector3(100, 20, 60));
    const pose = viewPose({ min: box.min.toArray() as Vec3, max: box.max.toArray() as Vec3 }, mode, s.perspective.aspect, s.perspective.fov);
    s.controls.target.set(...pose.target);
    // the controls keep +y up: a view along y is nudged towards the person at the desk, so its far side is up
    const nudge = mode === 'top' || mode === 'bottom' ? pose.distance * 1e-4 : 0;
    s.camera.position.set(pose.position[0], pose.position[1], pose.position[2] + nudge);
    // only the bottom view looks up from under the desk
    s.controls.maxPolarAngle = mode === 'bottom' ? Math.PI : Math.PI * 0.48;
    for (const cam of [s.perspective, s.orthographic]) {
      cam.near = Math.max(0.5, pose.distance / 500);
      cam.far = pose.distance * 20;
    }
    s.orthographic.zoom = 1;
    setOrthoFrustum(s.orthographic, pose.halfHeight, s.perspective.aspect);
    if (s.camera !== s.perspective) s.perspective.position.copy(s.camera.position);
    s.controls.update();
    s.perspective.updateProjectionMatrix();
    s.orthographic.updateProjectionMatrix();
    invalidate();
  }, [invalidate]);

  const clearMeasurement = useCallback(() => {
    const s = state.current;
    if (!s) return;
    s.measureFrom = null;
    for (const child of [...s.inspect.children]) if (child.userData.measurement) {
      s.inspect.remove(child);
      disposeTree(child);
    }
    invalidate();
  }, [invalidate]);

  const exportImage = useCallback(async (scale: number) => {
    const s = state.current;
    if (!s) return null;
    const host = hostRef.current!;
    const w = host.clientWidth || 1;
    const h = host.clientHeight || 1;
    const ratio = s.renderer.getPixelRatio();
    s.renderer.setPixelRatio(scale);
    s.renderer.setSize(w, h, false);
    s.quality?.setSize(w, h, scale);
    s.controls.update();
    if (s.quality) s.quality.still();
    else s.renderer.render(s.scene, s.camera);
    const blob = await new Promise<Blob | null>((resolve) => s.renderer.domElement.toBlob(resolve, 'image/png'));
    s.renderer.setPixelRatio(ratio);
    s.renderer.setSize(w, h, false);
    s.quality?.setSize(w, h, ratio);
    invalidate();
    return blob;
  }, [invalidate]);

  const exportGlb = useCallback(async () => {
    const s = state.current;
    if (!s) throw new Error('the bench is not drawn');
    // glTF is in metres; the bench in millimetres. Each part is a node named by its id;
    // the desk, labels, hints and lights are not part of the assembly.
    const root = new THREE.Group();
    root.name = 'bench';
    root.scale.setScalar(0.001);
    for (const e of s.parts.values()) {
      if (!e.built.group.visible) continue;
      const copy = e.built.group.clone(true);
      copy.name = e.view.id;
      root.add(copy);
    }
    if (s.jumpers.visible) { const j = s.jumpers.clone(true); j.name = 'jumpers'; root.add(j); }
    if (s.world.visible) { const w = s.world.clone(true); w.name = 'world'; root.add(w); }
    const drop: THREE.Object3D[] = [];
    root.traverse((o) => { if (o instanceof THREE.Sprite || o instanceof THREE.Light || o instanceof THREE.Line || o instanceof CSS2DObject || o.userData.routePoint !== undefined) drop.push(o); });
    for (const o of drop) o.removeFromParent();
    const out = await new GLTFExporter().parseAsync(root, { binary: true, onlyVisible: true });
    return new Blob([out as ArrayBuffer], { type: 'model/gltf-binary' });
  }, []);

  useImperativeHandle(props.handle, () => ({ frame, canvas: () => state.current?.renderer.domElement ?? null, exportImage, exportGlb, clearMeasurement }), [frame, exportImage, exportGlb, clearMeasurement]);

  // the scene, once
  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;
    let renderer: THREE.WebGLRenderer;
    try {
      renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    } catch {
      host.dataset.webgl = 'unavailable';
      propsRef.current.onRendererError?.();
      return;
    }
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    host.appendChild(renderer.domElement);
    const contextLost = (event: Event) => {
      event.preventDefault();
      host.dataset.webgl = 'lost';
      propsRef.current.onRendererError?.();
    };
    renderer.domElement.addEventListener('webglcontextlost', contextLost);
    renderer.domElement.setAttribute('aria-hidden', 'true');
    const labels = new CSS2DRenderer();
    labels.domElement.className = 'eb-labels';
    host.appendChild(labels.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color('#1c2a33');
    const pmrem = new THREE.PMREMGenerator(renderer);
    scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    scene.environmentIntensity = 0.55;
    const camera = new THREE.PerspectiveCamera(38, 1, 1, 5000);
    camera.position.set(0, 260, 240);
    const orthographic = new THREE.OrthographicCamera(-100, 100, 100, -100, 1, 5000);

    scene.add(new THREE.HemisphereLight(0xdfe9f3, 0x1d262c, 0.65));
    const sun = new THREE.DirectionalLight(0xfff4e5, 2.1);
    sun.position.set(-160, 420, 260);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    sun.shadow.camera.left = -400;
    sun.shadow.camera.right = 400;
    sun.shadow.camera.top = 400;
    sun.shadow.camera.bottom = -400;
    sun.shadow.camera.far = 1500;
    sun.shadow.bias = -0.0004;
    sun.shadow.radius = 4;
    scene.add(sun);
    scene.add(sun.target);

    const mat = deskTexture();
    const desk = new THREE.Mesh(new THREE.PlaneGeometry(10000, 10000), new THREE.MeshStandardMaterial({ map: mat ?? null, color: mat ? 0xffffff : 0x2f4f5f, roughness: 0.92 }));
    desk.rotation.x = -Math.PI / 2;
    desk.receiveShadow = true;
    desk.name = 'desk';
    scene.add(desk);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = false;
    controls.mouseButtons = { LEFT: null as unknown as THREE.MOUSE, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.ROTATE };
    controls.maxPolarAngle = Math.PI * 0.48;
    controls.minDistance = 20;
    controls.maxDistance = 2500;
    controls.zoomToCursor = true;

    const jumpers = new THREE.Group();
    const links = new THREE.Group();
    const notes = new THREE.Group();
    const world = new THREE.Group();
    const hints = new THREE.Group();
    const preview = new THREE.Group();
    const inspect = new THREE.Group();
    scene.add(jumpers, links, notes, world, hints, preview, inspect);
    state.current = {
      renderer, labels, scene, camera, perspective: camera, orthographic, controls, desk, parts: new Map(), jumpers, links, notes, world, hints, preview,
      inspect, quality: null, outline: null, hover: null, measureFrom: null, raf: 0, framed: false,
    };

    controls.addEventListener('change', invalidate);

    const resize = () => {
      const w = host.clientWidth || 1;
      const h = host.clientHeight || 1;
      renderer.setSize(w, h, false);
      renderer.domElement.style.width = `${w}px`;
      renderer.domElement.style.height = `${h}px`;
      labels.setSize(w, h);
      const half = (orthographic.top - orthographic.bottom) / 2;
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      setOrthoFrustum(orthographic, half, w / h);
      orthographic.updateProjectionMatrix();
      state.current?.quality?.setSize(w, h, renderer.getPixelRatio());
      state.current?.outline?.setSize(w, h, renderer.getPixelRatio());
      invalidate();
    };
    const ro = new ResizeObserver(resize);
    ro.observe(host);
    resize();
    return () => {
      ro.disconnect();
      controls.dispose();
      for (const e of state.current?.parts.values() ?? []) e.surface?.dispose();
      if (state.current?.raf) cancelAnimationFrame(state.current.raf);
      state.current?.quality?.dispose();
      state.current?.outline?.dispose();
      if (state.current) disposeTree(state.current.inspect);
      renderer.domElement.removeEventListener('webglcontextlost', contextLost);
      desk.material.map?.dispose();
      desk.material.dispose();
      desk.geometry.dispose();
      renderer.dispose();
      pmrem.dispose();
      renderer.domElement.remove();
      labels.domElement.remove();
      state.current = null;
    };
  }, [invalidate]);

  // Presentation preferences affect rendering only, never the project or nets.
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    s.desk.material.map?.dispose();
    s.desk.material.map = deskTexture(props.surface) ?? null;
    s.desk.material.needsUpdate = true;
    s.scene.background = new THREE.Color(props.surface === 'paper' ? '#dedfdf' : props.surface === 'graphite' ? '#24272c' : '#1c2a33');
    s.notes.visible = props.showNotes !== false;
    invalidate();
  }, [props.surface, props.showNotes, invalidate]);

  // the parts: built when new or changed, moved when they move
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const seen = new Set<string>();
    for (const v of props.parts) {
      seen.add(v.id);
      let e = s.parts.get(v.id);
      const key = partKey(v);
      if (!e || e.key !== key) {
        if (e) {
          s.scene.remove(e.built.group);
          e.surface?.dispose();
        }
        const surface = v.artwork && v.shape.textured ? new ArtworkSurface(v.artwork.svg, v.shape.size[0], v.shape.size[1], v.artwork.states, v.artwork.hotspots, v.artwork.vars) : undefined;
        if (surface) surface.onDraw = invalidate;
        const built = buildPart(v.id, v.shape, { component: v.component, params: v.params, texture: surface?.texture });
        const label = new CSS2DObject(labelElement(v.label));
        const top = new THREE.Box3().setFromObject(built.group).max.y;
        const labelGap = Math.min(14, Math.max(1.5, Math.max(v.shape.size[0], v.shape.size[1]) * 0.15));
        label.position.set(0, top + labelGap, -v.shape.size[1] / 2);
        built.group.add(label);
        s.scene.add(built.group);
        e = { key, built, surface, label, view: v };
        s.parts.set(v.id, e);
      }
      e.view = v;
      e.built.group.name = v.id;
      e.built.group.visible = (props.layers ?? ALL_LAYERS)[layerOf(v.shape.package)];
      // exploded: a part plugged into a breadboard rises off it, its legs out of the holes
      const lift = v.elevation > 0 ? Math.max(0, Math.min(1, props.explode ?? 0)) * EXPLODE_LIFT : 0;
      e.built.group.position.set(v.place.x, v.elevation + lift, v.place.y);
      e.built.group.rotation.y = -(v.place.rotation * Math.PI) / 180;
      // the id on the bench, the whole name when selected
      const chosen = props.selected?.part === v.id;
      e.label.element.textContent = chosen || v.shape.package === 'board' ? v.label : v.id;
      e.label.element.title = v.label;
      e.label.element.classList.toggle('is-selected', chosen);
      // a small part on a breadboard is named when selected: the labels would cover the board
      const small = v.elevation > 0 && !['module', 'board', 'box'].includes(v.shape.package);
      e.label.visible = chosen || (props.labels !== 'selected' && (props.labels === 'all' || !small));
      // live state: the artwork, an LED's light, a control's position
      e.surface?.update(v.outputs ?? {});
      updateEmission(e.built, v.outputs);
      for (const ind of e.built.indicators ?? []) {
        const light = indicatorLight(v.outputs?.[ind.output]);
        const on = light.level;
        if (light.color) {
          ind.material.emissive.set(light.color);
          (ind.halo?.material as THREE.SpriteMaterial | undefined)?.color.set(light.color);
        }
        ind.material.emissiveIntensity = on * 3;
        if (ind.halo) {
          ind.halo.visible = on > 0.02;
          (ind.halo.material as THREE.SpriteMaterial).opacity = Math.min(0.9, on);
        }
      }
      const ctl = e.built.control;
      if (ctl) {
        const d = ctl.userData as PickData;
        if (d.gesture === 'hold') {
          // a plunger travels 0.25 mm (a 6 mm tactile switch's stroke), from where it rests
          ctl.userData.rest ??= ctl.position.y;
          ctl.position.y = (ctl.userData.rest as number) - (v.inputs?.[d.input!] ? 0.25 : 0);
        }
      }
      // a board's button caps, pressed while held
      e.built.group.traverse((o) => {
        const d = o.userData as PickData;
        if (o.name !== 'cap' || !d.target || !d.input) return;
        o.userData.rest ??= o.position.y;
        o.position.y = (o.userData.rest as number) - (v.inputs?.[`${d.target}.${d.input}`] ? 0.5 : 0);
      });
      if (ctl) {
        const d = ctl.userData as PickData;
        if (d.gesture === 'toggle') ctl.rotation.z = v.inputs?.[d.input!] ? -0.35 : 0.35;
        if (d.gesture === 'turn') ctl.rotation.y = -((Number(v.inputs?.[d.input!] ?? 0.5) - 0.5) * 1.5 * Math.PI);
      }
    }
    for (const [id, e] of s.parts) if (!seen.has(id)) {
      s.scene.remove(e.built.group);
      e.surface?.dispose();
      e.label.element.remove();
      s.parts.delete(id);
    }
    if (!s.framed && props.parts.length) {
      s.framed = true;
      frame();
    }
    invalidate();
  }, [props.parts, props.selected, props.labels, props.layers, props.explode, invalidate, frame]);

  // the projection: the orthographic camera takes the perspective one's place, framing the same width
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const want = props.projection === 'orthographic' ? s.orthographic : s.perspective;
    if (s.camera === want) return;
    const distance = s.camera.position.distanceTo(s.controls.target);
    if (want === s.orthographic) {
      const half = distance * Math.tan((s.perspective.fov * Math.PI) / 360);
      s.orthographic.zoom = 1;
      setOrthoFrustum(s.orthographic, half, s.perspective.aspect);
    } else {
      // keep the apparent size: the distance at which the perspective camera sees the ortho frustum
      const half = (s.orthographic.top - s.orthographic.bottom) / 2 / s.orthographic.zoom;
      const dir = s.camera.position.clone().sub(s.controls.target).normalize();
      s.perspective.position.copy(s.controls.target).addScaledVector(dir, half / Math.tan((s.perspective.fov * Math.PI) / 360));
    }
    if (want === s.orthographic) want.position.copy(s.camera.position);
    want.near = s.camera.near;
    want.far = s.camera.far;
    want.updateProjectionMatrix();
    s.camera = want;
    s.controls.object = want;
    s.quality?.setCamera(want);
    s.controls.update();
    invalidate();
  }, [props.projection, invalidate]);

  // the render quality
  useEffect(() => {
    const s = state.current;
    const host = hostRef.current;
    if (!s || !host) return;
    if (props.quality === 'quality' && !s.quality) {
      s.quality = new QualityRenderer(s.renderer, s.scene, s.camera, host.clientWidth || 1, host.clientHeight || 1);
      s.quality.setSize(host.clientWidth || 1, host.clientHeight || 1, s.renderer.getPixelRatio());
      // softer, finer shadows to go with the occlusion
      s.scene.traverse((o) => { if (o instanceof THREE.DirectionalLight && o.castShadow) { o.shadow.mapSize.set(4096, 4096); o.shadow.map?.dispose(); o.shadow.map = null; } });
    } else if (props.quality !== 'quality' && s.quality) {
      s.quality.dispose();
      s.quality = null;
    }
    invalidate();
  }, [props.quality, invalidate]);

  // the layers a user hides (the parts' own are set as they are built)
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const layers = props.layers ?? ALL_LAYERS;
    s.jumpers.visible = layers.jumpers;
    s.links.visible = layers.jumpers;
    s.world.visible = layers.world;
    s.labels.domElement.style.display = layers.labels ? '' : 'none';
    invalidate();
  }, [props.layers, invalidate]);

  // the height plane, and a box around each part that rises through it
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const height = props.heightPlane;
    const added: THREE.Object3D[] = [];
    if (typeof height === 'number' && Number.isFinite(height)) {
      const all = new THREE.Box3();
      const bounds = [...s.parts.values()].filter((e) => e.built.group.visible).map((e) => {
        const b = bodyBounds(e.built.group);
        all.union(b);
        return { id: e.view.id, box: b };
      });
      if (all.isEmpty()) all.set(new THREE.Vector3(-100, 0, -60), new THREE.Vector3(100, 20, 60));
      const size = all.getSize(new THREE.Vector3());
      const centre = all.getCenter(new THREE.Vector3());
      const plane = new THREE.Mesh(
        new THREE.PlaneGeometry(size.x + 40, size.z + 40),
        new THREE.MeshBasicMaterial({ color: 0xf59e0b, transparent: true, opacity: 0.16, side: THREE.DoubleSide, depthWrite: false }),
      );
      plane.rotation.x = -Math.PI / 2;
      plane.position.set(centre.x, height, centre.z);
      plane.renderOrder = 5;
      const edge = new THREE.LineSegments(new THREE.EdgesGeometry(plane.geometry), new THREE.LineBasicMaterial({ color: 0xf59e0b }));
      edge.rotation.copy(plane.rotation);
      edge.position.copy(plane.position);
      added.push(plane, edge);
      const above = aboveHeight(bounds.map((b) => ({ id: b.id, bounds: { min: b.box.min.toArray() as Vec3, max: b.box.max.toArray() as Vec3 } })), height);
      for (const a of above) {
        const b = bounds.find((x) => x.id === a.id)!.box;
        const helper = new THREE.Box3Helper(b, new THREE.Color(0xef4444));
        added.push(helper);
      }
      propsRef.current.onHeightReport?.(above);
    } else propsRef.current.onHeightReport?.([]);
    for (const o of added) s.inspect.add(o);
    invalidate();
    return () => {
      for (const o of added) {
        s.inspect.remove(o);
        disposeTree(o);
      }
    };
  }, [props.heightPlane, props.parts, props.layers, invalidate]);

  // the jumpers, the connections still to build, the notes
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const clear = (g: THREE.Group) => {
      for (const c of [...g.children]) {
        c.traverse((o) => {
          if (o instanceof CSS2DObject) o.element.remove();
          if (o instanceof THREE.Mesh || o instanceof THREE.Line) o.geometry.dispose();
        });
        g.remove(c);
      }
    };
    clear(s.jumpers);
    clear(s.links);
    clear(s.notes);
    // what a long jumper drapes over: the parts as they stand now
    s.scene.updateMatrixWorld(true);
    const floor = floorOf(s.parts.values(), s.camera);
    const pinMat = new THREE.MeshStandardMaterial({ color: 0xc9ccd1, metalness: 0.9, roughness: 0.3 });
    const housingMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.5 });
    for (const j of props.jumpers) {
      const g = new THREE.Group();
      const sel = props.selected?.jumper === j.index || j.traced;
      const insul = new THREE.MeshStandardMaterial({ color: new THREE.Color(j.color), roughness: 0.45, emissive: sel ? new THREE.Color(j.color) : new THREE.Color(0), emissiveIntensity: sel ? 0.5 : 0 });
      const tube = new THREE.Mesh(new THREE.TubeGeometry(jumperCurve(j.a, j.b, j.points, j.rise_mm, floor), 96, 0.8, 10, false), insul);
      tube.castShadow = true;
      tube.userData = { jumper: j.index };
      g.add(tube);
      const flat = Math.abs(j.a[1] - j.b[1]) < 0.01 && Math.hypot(j.a[0] - j.b[0], j.a[2] - j.b[2]) < 60;
      [j.a, j.b].forEach((end, k) => {
        const housing = j.housings?.[k];
        if (flat && !housing) {
          // a pre-cut jumper: its bare end goes straight into the hole
          const tip = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 3, 8), pinMat);
          tip.position.set(end[0], end[1] - 0.6, end[2]);
          g.add(tip);
          return;
        }
        const tip = new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.32, 4, 8), pinMat);
        tip.position.set(end[0], end[1] - 1, end[2]);
        g.add(tip);
        const sleeve = new THREE.Mesh(housing ? new THREE.BoxGeometry(2.5, 14, 2.5) : new THREE.CylinderGeometry(1, 1, 5, 12), housing ? housingMat : insul);
        sleeve.position.set(end[0], end[1] + (housing ? 7 : 3.5), end[2]);
        sleeve.castShadow = true;
        sleeve.userData = { jumper: j.index };
        g.add(sleeve);
      });
      g.userData.jumperGroup = j.index;
      if (props.selected?.jumper === j.index && props.editable) {
        const points = j.points?.length ? j.points : [jumperCurve(j.a, j.b, undefined, j.rise_mm).getPoint(0.5).toArray() as Point3];
        points.forEach((point, index) => {
          const handle = new THREE.Mesh(new THREE.SphereGeometry(1.6, 12, 8), new THREE.MeshBasicMaterial({ color: 0xffffff, depthTest: false }));
          handle.position.copy(v3(point));
          handle.renderOrder = 20;
          handle.userData = { jumper: j.index, routePoint: index };
          const tag = document.createElement('span');
          tag.className = 'eb-route-point';
          tag.textContent = String(index + 1);
          tag.setAttribute('aria-label', `Jumper ${j.index + 1} route point ${index + 1}`);
          const label = new CSS2DObject(tag);
          handle.add(label);
          g.add(handle);
        });
      }
      s.jumpers.add(g);
    }
    for (const l of props.links) {
      const geom = new THREE.BufferGeometry().setFromPoints(jumperCurve(l.a, l.b).getPoints(40));
      const line = new THREE.Line(geom, new THREE.LineDashedMaterial({ color: new THREE.Color(l.color), dashSize: 2.5, gapSize: 2, transparent: true, opacity: 0.85 }));
      line.computeLineDistances();
      s.links.add(line);
    }
    props.notes.forEach((n, i) => {
      const el = document.createElement('div');
      el.className = 'eb-note';
      el.textContent = n.text;
      el.dataset.note = String(i);
      const o = new CSS2DObject(el);
      o.position.set(n.x, 30, n.y);
      s.notes.add(o);
      if (n.to) {
        const geom = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(n.x, 28, n.y), new THREE.Vector3(n.to[0], 4, n.to[1])]);
        s.notes.add(new THREE.Line(geom, new THREE.LineBasicMaterial({ color: 0xf6e58d })));
      }
    });
    invalidate();
  }, [props.jumpers, props.links, props.notes, props.selected, props.editable, invalidate]);

  // the world: its bodies, and the beams that meet them
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    for (const c of [...s.world.children]) {
      c.traverse((o) => {
        if (o instanceof CSS2DObject) o.element.remove();
        if (o instanceof THREE.Mesh || o instanceof THREE.Line) o.geometry.dispose();
      });
      s.world.remove(c);
    }
    for (const b of props.bodies ?? []) {
      // bench (x, y, z up) to the scene (x, z up, y towards the viewer)
      const geom = b.shape === 'sphere' ? new THREE.SphereGeometry(b.size[0] / 2, 32, 16) : new THREE.BoxGeometry(b.size[0], b.size[2], b.size[1]);
      const m = new THREE.Mesh(geom, new THREE.MeshStandardMaterial({ color: new THREE.Color(b.color ?? '#b08d5b'), roughness: 0.85 }));
      m.position.set(b.at[0], b.at[2], b.at[1]);
      m.castShadow = true;
      m.receiveShadow = true;
      m.userData = { body: b.id };
      const label = new CSS2DObject(labelElement(b.name ?? b.id));
      label.position.set(0, (b.shape === 'sphere' ? b.size[0] : b.size[2]) / 2 + 8, 0);
      m.add(label);
      s.world.add(m);
    }
    for (const beam of props.beams ?? []) {
      const pts = [new THREE.Vector3(beam.from[0], beam.from[2], beam.from[1]), new THREE.Vector3(beam.to[0], beam.to[2], beam.to[1])];
      const hit = beam.distance >= 0;
      const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineDashedMaterial({ color: hit ? 0x2ecc71 : 0x95a5a6, dashSize: 4, gapSize: 3, transparent: true, opacity: hit ? 0.9 : 0.45 }));
      line.computeLineDistances();
      s.world.add(line);
      if (hit) {
        const dot = new THREE.Mesh(new THREE.SphereGeometry(1.8, 12, 8), new THREE.MeshBasicMaterial({ color: 0x2ecc71 }));
        dot.position.copy(pts[1]!);
        const tag = new CSS2DObject(labelElement(`${(beam.distance / 10).toFixed(1)} cm`));
        tag.position.set(0, 6, 0);
        dot.add(tag);
        s.world.add(dot);
      }
    }
    invalidate();
  }, [props.bodies, props.beams, invalidate]);

  // Keyboard insertion uses the same per-leg proposal as pointer placement.
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const meshes: THREE.Mesh[] = [];
    for (const contact of props.insertionPreview?.contacts ?? []) {
      if (!contact.hole) continue;
      const marker = new THREE.Mesh(new THREE.TorusGeometry(0.9, 0.18, 6, 16), new THREE.MeshBasicMaterial({ color: contact.occupiedBy.length ? 0xe74c3c : 0x159a67, depthTest: false }));
      marker.rotation.x = -Math.PI / 2;
      marker.position.set(contact.at[0], (props.insertionPreview?.elevation ?? 0) + 0.5, contact.at[1]);
      marker.renderOrder = 10;
      s.hints.add(marker);
      meshes.push(marker);
    }
    invalidate();
    return () => {
      for (const marker of meshes) {
        s.hints.remove(marker);
        marker.geometry.dispose();
        (marker.material as THREE.Material).dispose();
      }
    };
  }, [props.insertionPreview, invalidate]);

  // A trace includes free contacts on the same breadboard strip.
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const markers: THREE.Mesh[] = [];
    const refs = new Set(props.traceTerminals ?? []);
    for (const terminal of props.terminals) if (refs.has(terminal.ref)) {
      const marker = new THREE.Mesh(new THREE.TorusGeometry(1.25, 0.25, 6, 16), new THREE.MeshBasicMaterial({ color: 0x38bdf8, depthTest: false }));
      marker.rotation.x = -Math.PI / 2;
      marker.position.copy(v3(terminal.at)).add(new THREE.Vector3(0, 0.4, 0));
      marker.renderOrder = 12;
      s.hints.add(marker);
      markers.push(marker);
    }
    invalidate();
    return () => { for (const marker of markers) { s.hints.remove(marker); marker.geometry.dispose(); (marker.material as THREE.Material).dispose(); } };
  }, [props.traceTerminals, props.terminals, invalidate]);

  // the section plane (after the effects that build what it cuts): the bench's objects on one side of it are cut away; the desk stays whole
  useEffect(() => {
    const s = state.current;
    if (!s) return;
    const cut = props.section;
    let planes: THREE.Plane[] | null = null;
    if (cut) {
      const { normal, constant } = sectionPlane(cut);
      planes = [new THREE.Plane(new THREE.Vector3(...normal), constant)];
    }
    s.renderer.localClippingEnabled = planes !== null;
    const clip = (o: THREE.Object3D) => o.traverse((x) => {
      const mat = (x as THREE.Mesh).material;
      for (const m of Array.isArray(mat) ? mat : mat ? [mat] : []) {
        if (m.clippingPlanes !== planes) { m.clippingPlanes = planes; m.clipShadows = planes !== null; m.needsUpdate = true; }
      }
    });
    for (const e of s.parts.values()) clip(e.built.group);
    clip(s.jumpers);
    clip(s.links);
    clip(s.world);
    invalidate();
  }, [props.section, props.parts, props.jumpers, props.bodies, invalidate]);

  // pointer gestures
  useEffect(() => {
    const s = state.current;
    const host = hostRef.current;
    if (!s || !host) return;
    const el = s.renderer.domElement;
    const ray = new THREE.Raycaster();
    const ndc = new THREE.Vector2();
    const desk = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
    const cast = (ev: PointerEvent) => {
      const r = el.getBoundingClientRect();
      ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(ndc, s.camera);
    };
    const onDesk = (): THREE.Vector3 | null => ray.ray.intersectPlane(desk, new THREE.Vector3());
    // a hidden layer is not under the pointer
    const hitParts = () => ray.intersectObjects([...[...s.parts.values()].filter((e) => e.built.group.visible).map((e) => e.built.group), ...(s.jumpers.visible ? [s.jumpers] : []), ...(s.world.visible ? s.world.children.filter((object) => object.userData.body) : [])], true)
      .filter((h) => !(h.object instanceof THREE.Sprite) && !(h.object instanceof THREE.Line));
    // the measure tool: a first point, then a second; the measurement stays until the next one or Esc
    const measureMark = (at: Vec3, color: number) => {
      const m = new THREE.Mesh(new THREE.SphereGeometry(0.6, 16, 8), new THREE.MeshBasicMaterial({ color, depthTest: false }));
      m.position.set(...at);
      m.renderOrder = 20;
      m.userData.measurement = true;
      s.inspect.add(m);
    };
    const measureAt = () => {
      const p = propsRef.current;
      const hit = hitParts()[0];
      const raw = hit?.point ?? onDesk();
      if (!raw) return;
      const { at, snapped } = snapPoint(raw.toArray() as Vec3, p.terminals.map((t) => t.at), 1.5);
      const done = s.inspect.children.some((c) => c.userData.measurement === 'line');
      if (!s.measureFrom || done) {
        for (const child of [...s.inspect.children]) if (child.userData.measurement) { s.inspect.remove(child); disposeTree(child); }
        s.measureFrom = at;
        measureMark(at, 0x38bdf8);
        p.onMeasure?.(null);
        setFeedback({ text: `From ${at.map((v) => Math.round(v * 100) / 100).join(', ')} mm${snapped ? ' (snapped to a contact)' : ''} · click the second point · Esc clears` });
      } else {
        const m = measure(s.measureFrom, at);
        measureMark(at, 0x38bdf8);
        const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(...m.a), new THREE.Vector3(...m.b)]), new THREE.LineBasicMaterial({ color: 0x38bdf8, depthTest: false }));
        line.renderOrder = 20;
        line.userData.measurement = 'line';
        const el = document.createElement('div');
        el.className = 'eb-measure';
        el.textContent = describeMeasurement(m);
        const label = new CSS2DObject(el);
        label.position.set((m.a[0] + m.b[0]) / 2, (m.a[1] + m.b[1]) / 2 + 2, (m.a[2] + m.b[2]) / 2);
        label.userData.measurement = true;
        s.inspect.add(line, label);
        s.measureFrom = null;
        p.onMeasure?.(m);
        setFeedback({ text: describeMeasurement(m) });
      }
      invalidate();
    };
    let routeDrag: { jumper: JumperView; points: Point3[]; index: number; start: Point3[]; moved: boolean } | null = null;
    const routePreview = (j: JumperView, points: Point3[]) => {
      const group = s.jumpers.children.find((g) => g.userData.jumperGroup === j.index);
      const tube = group?.children[0] as THREE.Mesh<THREE.TubeGeometry> | undefined;
      if (!tube) return;
      tube.geometry.dispose();
      tube.geometry = new THREE.TubeGeometry(jumperCurve(j.a, j.b, points, j.rise_mm, floorOf(s.parts.values(), s.camera)), 96, 0.8, 10, false);
      for (const child of group!.children) if (typeof child.userData.routePoint === 'number') {
        const point = points[child.userData.routePoint];
        if (point) child.position.copy(v3(point));
      }
      invalidate();
    };
    let dragBody: { id: string; mesh: THREE.Object3D; dx: number; dz: number; moved: boolean; start: THREE.Vector3 } | null = null;
    let activePointer: number | null = null;

    let drag: { id: string; dx: number; dz: number; moved: boolean; start: Required<BenchPlace>; rotation: number } | null = null;
    let wire: { from: TerminalHit; line: THREE.Mesh | null } | null = null;
    let held: { target: string; input: string } | null = null;
    let turning: { target: string; input: string; x: number; v: number } | null = null;
    const holeMarks: THREE.Mesh[] = [];
    const markMat = new THREE.MeshBasicMaterial({ color: 0x2ecc71 });
    const blockedMat = new THREE.MeshBasicMaterial({ color: 0xe74c3c });
    const clearMarks = () => {
      for (const m of holeMarks) {
        s.preview.remove(m);
        m.geometry.dispose();
      }
      holeMarks.length = 0;
    };
    const showMarks = (pts: Point3[], blocked: Point3[] = []) => {
      clearMarks();
      for (const [p, material] of [...pts.map((p) => [p, markMat] as const), ...blocked.map((p) => [p, blockedMat] as const)]) {
        const m = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.9, 0.3, 12), material);
        m.position.set(p[0], p[1] + 0.2, p[2]);
        s.preview.add(m);
        holeMarks.push(m);
      }
    };
    const terminalAtPointer = (ev: PointerEvent) => {
      const r = el.getBoundingClientRect();
      const projected: ProjectedTerminal[] = [];
      const occlusionRay = new THREE.Raycaster();
      for (const terminal of propsRef.current.terminals) {
        const world = v3(terminal.at);
        const screen = world.clone().project(s.camera);
        const x = (screen.x + 1) * r.width / 2;
        const y = (1 - screen.y) * r.height / 2;
        if (Math.hypot(x - (ev.clientX - r.left), y - (ev.clientY - r.top)) > 11 || screen.z < -1 || screen.z > 1) continue;
        // A hit area cannot see through another component. Generic lead
        // anchors can sit inside their own lead geometry, so the owning body
        // is excluded; this is not a mechanical accessibility check.
        occlusionRay.setFromCamera(new THREE.Vector2(screen.x, screen.y), s.camera);
        const blocker = occlusionRay.intersectObjects([...[...s.parts.values()].map((e) => e.built.group), ...s.world.children.filter((object) => object.userData.body)], true)
          .find((hit) => (hit.object.userData as PickData).part !== terminal.ref.split('.')[0] && hit.distance + 0.5 < s.camera.position.distanceTo(world));
        if (!blocker) projected.push({ terminal, x, y, depth: screen.z });
      }
      return pickTerminal(projected, ev.clientX - r.left, ev.clientY - r.top);
    };
    const feedbackAt = (ev: PointerEvent, from?: TerminalHit) => {
      const pick = terminalAtPointer(ev);
      const terminal = pick.kind === 'terminal' ? pick.terminal : undefined;
      const blocked = Boolean(terminal?.occupiedBy?.length);
      const point = terminal ? v3(terminal.at).project(s.camera) : undefined;
      const r = el.getBoundingClientRect();
      const text = blocked ? `${terminal!.ref} · occupied by ${terminal!.occupiedBy!.join(', ')} · choose another hole on the strip`
        : pick.kind === 'ambiguous' ? 'Contacts overlap — zoom in to choose a pin'
        : terminal ? from ? terminal.ref === from.ref ? `${from.ref} · choose another terminal` : `${from.ref} → ${terminal.ref} · release to connect` : `${terminal.ref} · drag to connect`
        : from ? `${from.ref} → choose a terminal · Esc cancels` : 'Point at a hole or pin to start a jumper';
      setFeedback({ text, ref: terminal?.ref, blocked, x: point ? (point.x + 1) * r.width / 2 : undefined, y: point ? (1 - point.y) * r.height / 2 : undefined });
      el.style.cursor = blocked ? 'not-allowed' : terminal ? 'var(--draw-cursor, crosshair)' : 'default';
      return blocked ? undefined : terminal;
    };
    const down = (ev: PointerEvent) => {
      if (ev.button !== 0 || activePointer !== null) return;
      host.focus({ preventScroll: true });
      const p = propsRef.current;
      cast(ev);
      const hits = hitParts();
      // Route handles are visible overlay controls, so their picking priority
      // matches their depth-independent display even over a component body.
      if (p.tool === 'measure') {
        measureAt();
        return;
      }
      const hit = p.editable && p.tool === 'select' ? hits.find((h) => h.object.userData.routePoint !== undefined) ?? hits[0] : hits[0];
      const data = (hit?.object.userData ?? {}) as PickData & { jumper?: number; routePoint?: number };
      if (data.routePoint !== undefined && data.jumper !== undefined && p.editable && p.tool === 'select' && p.onRoute) {
        const j = p.jumpers.find((wire) => wire.index === data.jumper);
        if (!j) return;
        const points = structuredClone(j.points?.length ? j.points : [jumperCurve(j.a, j.b, undefined, j.rise_mm).getPoint(0.5).toArray() as Point3]);
        routeDrag = { jumper: j, index: data.routePoint, points, start: structuredClone(points), moved: false };
        activePointer = ev.pointerId;
        el.setPointerCapture(ev.pointerId);
        el.style.cursor = 'grabbing';
        return;
      }
      // a control of a part, while the circuit runs
      if (p.live && p.tool !== 'wire' && data.part && data.input) {
        const view = s.parts.get(data.part)?.view;
        const target = data.target ? `${data.part}.${data.target}` : data.part;
        if (data.gesture === 'hold') {
          held = { target, input: data.input };
          p.onInput(target, data.input, true);
        } else if (data.gesture === 'toggle') {
          p.onInput(data.part, data.input, !view?.inputs?.[data.input]);
        } else if (data.gesture === 'turn') {
          turning = { target: data.part, input: data.input, x: ev.clientX, v: Number(view?.inputs?.[data.input] ?? 0.5) };
        }
        activePointer = ev.pointerId;
        el.setPointerCapture(ev.pointerId);
        return;
      }
      // a hotspot on a textured face (a keypad's key, a board's button)
      if (p.live && p.tool !== 'wire' && data.part && hit?.uv && hit.face && hit.face.normal.y > 0.9) {
        const surface = s.parts.get(data.part)?.surface;
        const h = surface?.hotspotAt(hit.uv.x, 1 - hit.uv.y);
        if (h) {
          const target = h.target ? `${data.part}.${h.target}` : data.part;
          if (h.gesture === 'toggle') p.onInput(target, h.input, !s.parts.get(data.part)?.view.inputs?.[h.target ? `${h.target}.${h.input}` : h.input]);
          else {
            held = { target, input: h.input };
            p.onInput(target, h.input, true);
          }
          activePointer = ev.pointerId;
          el.setPointerCapture(ev.pointerId);
          return;
        }
      }
      if (data.jumper !== undefined && p.tool !== 'wire') {
        p.onSelect({ jumper: data.jumper });
        return;
      }
      // a body of the world: dragged across the desk at its height
      const body = (hit?.object.userData as { body?: string } | undefined)?.body;
      if (body && p.tool === 'select' && p.editable && p.onMoveBody) {
        const at = onDesk();
        if (at && hit) {
          dragBody = { id: body, mesh: hit.object, dx: hit.object.position.x - at.x, dz: hit.object.position.z - at.z, moved: false, start: hit.object.position.clone() };
          activePointer = ev.pointerId;
          el.setPointerCapture(ev.pointerId);
        }
        return;
      }
      if (!p.editable) {
        if (data.part) p.onSelect({ part: data.part });
        return;
      }
      if (p.tool === 'wire') {
        const t = feedbackAt(ev);
        if (t) {
          wire = { from: t, line: null };
          showMarks([t.at]);
          activePointer = ev.pointerId;
          el.setPointerCapture(ev.pointerId);
          invalidate();
        }
        return;
      }
      if (data.part) {
        const e = s.parts.get(data.part)!;
        const at = onDesk();
        p.onSelect({ part: data.part });
        if (at) {
          drag = { id: data.part, dx: e.view.place.x - at.x, dz: e.view.place.y - at.z, moved: false, start: e.view.place, rotation: e.view.place.rotation };
          activePointer = ev.pointerId;
          el.setPointerCapture(ev.pointerId);
          el.style.cursor = 'grabbing';
        }
      } else p.onSelect({});
    };
    const move = (ev: PointerEvent) => {
      if (activePointer !== null && ev.pointerId !== activePointer) return;
      const p = propsRef.current;
      if (turning) {
        const v = Math.max(0, Math.min(1, turning.v + (ev.clientX - turning.x) / 200));
        p.onInput(turning.target, turning.input, Math.round(v * 1000) / 1000);
        return;
      }
      cast(ev);
      if (routeDrag) {
        const point = routeDrag.points[routeDrag.index]!;
        const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), -point[1]);
        const at = ray.ray.intersectPlane(plane, new THREE.Vector3());
        if (!at) return;
        point[0] = Math.round(at.x * 10) / 10;
        point[2] = Math.round(at.z * 10) / 10;
        routeDrag.moved = true;
        routePreview(routeDrag.jumper, routeDrag.points);
        setFeedback({ text: `Route point ${routeDrag.index + 1}: ${point.join(', ')} mm · Esc cancels` });
        return;
      }
      if (dragBody) {
        const at = onDesk();
        if (!at) return;
        dragBody.mesh.position.x = Math.round((at.x + dragBody.dx) * 10) / 10;
        dragBody.mesh.position.z = Math.round((at.z + dragBody.dz) * 10) / 10;
        dragBody.moved = true;
        invalidate();
        return;
      }
      if (drag) {
        const at = onDesk();
        if (!at) return;
        const e = s.parts.get(drag.id)!;
        const snapped = p.snap(drag.id, at.x + drag.dx, at.z + drag.dz, drag.rotation);
        drag.moved = drag.moved || Math.hypot(snapped.x - drag.start.x, snapped.y - drag.start.y) > 0.3;
        e.built.group.position.set(snapped.x, snapped.elevation, snapped.y);
        for (const rider of snapped.carried ?? []) {
          const entry = s.parts.get(rider.id);
          if (!entry) continue;
          entry.built.group.position.set(rider.x, entry.view.elevation, rider.y);
          entry.built.group.rotation.y = -(rider.rotation * Math.PI) / 180;
        }
        const contacts = snapped.insertion?.contacts ?? [];
        const free = contacts.filter((contact) => contact.hole && !contact.occupiedBy.length);
        const blocked = contacts.filter((contact) => contact.hole && contact.occupiedBy.length);
        const contactPoint = (contact: typeof contacts[number]): Point3 => [contact.at[0], snapped.elevation, contact.at[1]];
        showMarks(free.map(contactPoint), blocked.map(contactPoint));
        setFeedback({ text: contacts.map((contact) => `${contact.pin} → ${contact.hole ?? 'unseated'}${contact.occupiedBy.length ? ` (occupied: ${contact.occupiedBy.join(', ')})` : ''}`).join(' · ') || `Move ${drag.id}` });
        el.style.cursor = blocked.length ? 'not-allowed' : 'grabbing';
        invalidate();
        return;
      }
      if (wire) {
        const hit = hitParts().find((h) => (h.object.userData as { jumper?: number }).jumper === undefined);
        const t = feedbackAt(ev, wire.from);
        const at = hit?.point ?? onDesk();
        if (!at) return;
        const end: Point3 = t ? t.at : [at.x, at.y + 1, at.z];
        if (wire.line) {
          s.preview.remove(wire.line);
          wire.line.geometry.dispose();
          (wire.line.material as THREE.Material).dispose();
        }
        wire.line = new THREE.Mesh(new THREE.TubeGeometry(jumperCurve(wire.from.at, end), 48, 0.8, 8, false), new THREE.MeshStandardMaterial({ color: new THREE.Color(p.wireColor), roughness: 0.45, transparent: true, opacity: 0.85 }));
        s.preview.add(wire.line);
        showMarks(t ? [wire.from.at, t.at] : [wire.from.at]);
        invalidate();
      } else if (p.tool === 'wire' && p.editable) feedbackAt(ev);
      else if (p.tool === 'measure') el.style.cursor = 'crosshair';
      else {
        const hit = hitParts()[0];
        const data = hit?.object.userData as PickData | undefined;
        el.style.cursor = data?.input && p.live ? 'pointer' : data?.part && p.editable ? 'grab' : 'default';
        const over = data?.part ?? null;
        if (over !== s.hover) {
          s.hover = over;
          invalidate();
        }
      }
    };
    const up = (ev: PointerEvent) => {
      if (ev.pointerId !== activePointer) return;
      cast(ev);
      const p = propsRef.current;
      if (routeDrag) {
        const proposal = routeDrag;
        routeDrag = null;
        routePreview(proposal.jumper, proposal.jumper.points ?? []);
        if (proposal.moved) p.onRoute?.(proposal.jumper.index, proposal.points);
      }
      let rejection: string | undefined;
      if (held) {
        p.onInput(held.target, held.input, false);
        held = null;
      }
      turning = null;
      if (dragBody) {
        const m = dragBody.mesh.position;
        if (dragBody.moved) p.onMoveBody?.(dragBody.id, [m.x, m.z, m.y]);
        dragBody = null;
      }
      if (drag) {
        const at = onDesk();
        if (drag.moved && at) {
          const snapped = p.snap(drag.id, at.x + drag.dx, at.z + drag.dz, drag.rotation);
          if (snapped.insertion?.valid !== false)
            p.onMove(drag.id, { x: snapped.x, y: snapped.y, rotation: drag.rotation as 0 | 90 | 180 | 270 });
          else {
            rejection = snapped.insertion.contacts.filter((contact) => contact.occupiedBy.length).map((contact) => `${contact.hole} is occupied by ${contact.occupiedBy.join(', ')}`).join(' · ');
            const e = s.parts.get(drag.id);
            if (e) e.built.group.position.set(e.view.place.x, e.view.elevation, e.view.place.y);
          }
        } else {
          const e = s.parts.get(drag.id);
          if (e) e.built.group.position.set(e.view.place.x, e.view.elevation, e.view.place.y);
        }
        drag = null;
        clearMarks();
        invalidate();
      }
      if (wire) {
        cast(ev);
        const pick = terminalAtPointer(ev);
        const t = pick.kind === 'terminal' ? pick.terminal : undefined;
        if (t?.occupiedBy?.length) rejection = `${t.ref} is occupied by ${t.occupiedBy.join(', ')}; choose another hole on the strip`;
        if (wire.line) {
          s.preview.remove(wire.line);
          wire.line.geometry.dispose();
          (wire.line.material as THREE.Material).dispose();
        }
        if (t && !t.occupiedBy?.length && t.ref !== wire.from.ref) p.onWire(wire.from.ref, t.ref);
        wire = null;
        clearMarks();
        invalidate();
      }
      activePointer = null;
      setFeedback({ text: rejection ?? '' });
      if (el.hasPointerCapture(ev.pointerId)) el.releasePointerCapture(ev.pointerId);
    };
    const cancel = () => {
      if (routeDrag) {
        routePreview(routeDrag.jumper, routeDrag.jumper.points ?? []);
        // Restore handles too, including the automatic route's temporary midpoint.
        const group = s.jumpers.children.find((g) => g.userData.jumperGroup === routeDrag!.jumper.index);
        for (const child of group?.children ?? []) if (typeof child.userData.routePoint === 'number') child.position.copy(v3(routeDrag.start[child.userData.routePoint]!));
        // A cancelled route is immediately pickable at its restored position.
        // Otherwise CSS labels and raycast matrices can retain the last drag
        // until the next frame, making a quick second gesture hit stale data.
        s.scene.updateMatrixWorld(true);
        s.labels.render(s.scene, s.camera);
        routeDrag = null;
      }
      if (held) propsRef.current.onInput(held.target, held.input, false);
      held = null;
      turning = null;
      if (dragBody) dragBody.mesh.position.copy(dragBody.start);
      dragBody = null;
      if (drag) for (const e of s.parts.values()) {
        e.built.group.position.set(e.view.place.x, e.view.elevation, e.view.place.y);
        e.built.group.rotation.y = -(e.view.place.rotation * Math.PI) / 180;

      }
      drag = null;
      if (wire?.line) {
        s.preview.remove(wire.line);
        wire.line.geometry.dispose();
        (wire.line.material as THREE.Material).dispose();
      }
      wire = null;
      const pointer = activePointer;
      activePointer = null;
      if (pointer !== null && el.hasPointerCapture(pointer)) el.releasePointerCapture(pointer);
      clearMarks();
      setFeedback({ text: '' });
      el.style.cursor = '';
      invalidate();
    };
    const escape = (ev: globalThis.KeyboardEvent) => {
      if (ev.key !== 'Escape') return;
      ev.preventDefault();
      cancel();
      if (propsRef.current.tool === 'measure') {
        s.measureFrom = null;
        for (const child of [...s.inspect.children]) if (child.userData.measurement) { s.inspect.remove(child); disposeTree(child); }
        propsRef.current.onMeasure?.(null);
        invalidate();
      }
    };
    cancelGesture.current = cancel;
    const leave = () => {
      if (activePointer === null) setFeedback({ text: '' });
      if (s.hover) {
        s.hover = null;
        invalidate();
      }
    };
    const visibility = () => { if (document.hidden) cancel(); };
    const lostCapture = (event: PointerEvent) => { if (event.pointerId === activePointer) cancel(); };
    host.addEventListener('keydown', escape);
    window.addEventListener('blur', cancel);
    document.addEventListener('visibilitychange', visibility);
    el.addEventListener('lostpointercapture', lostCapture);
    el.addEventListener('pointerleave', leave);
    el.addEventListener('pointerdown', down);
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', cancel);
    return () => {
      el.removeEventListener('pointerdown', down);
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', up);
      el.removeEventListener('pointercancel', cancel);
      el.removeEventListener('lostpointercapture', lostCapture);
      el.removeEventListener('pointerleave', leave);
      host.removeEventListener('keydown', escape);
      window.removeEventListener('blur', cancel);
      document.removeEventListener('visibilitychange', visibility);
      cancel();
      cancelGesture.current = null;
      markMat.dispose();
      blockedMat.dispose();
    };
  }, [invalidate]);

  // the keyboard: turn, nudge, delete, frame
  const onKeyDown = (ev: KeyboardEvent) => {
    const p = propsRef.current;
    const sel = p.selected;
    if (ev.key === 'f' || ev.key === 'F') {
      frame();
      return;
    }
    // KiCad's 3D viewer keys: Z top (shift: bottom), Y front (back), X left (right), I isometric
    if (!ev.ctrlKey && !ev.metaKey && !ev.altKey) {
      const k = ev.key.toLowerCase();
      const views: Record<string, [StandardView, StandardView]> = { z: ['top', 'bottom'], y: ['front', 'back'], x: ['left', 'right'], i: ['iso', 'iso'] };
      if (views[k]) {
        ev.preventDefault();
        frame(views[k][ev.shiftKey ? 1 : 0]);
        return;
      }
    }
    if ((ev.key === 'Delete' || ev.key === 'Backspace') && p.editable && sel && (sel.part || sel.jumper !== undefined)) {
      ev.preventDefault();
      p.onDelete(sel);
      return;
    }
    const part = sel?.part ? p.parts.find((x) => x.id === sel.part) : undefined;
    if (!part || !p.editable) return;
    const steps: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
    if (ev.key === 'r' || ev.key === 'R') {
      ev.preventDefault();
      const rotation = ((part.place.rotation + (ev.shiftKey ? 270 : 90)) % 360) as 0 | 90 | 180 | 270;
      const snapped = p.snap(part.id, part.place.x, part.place.y, rotation);
      p.onMove(part.id, { x: snapped.x, y: snapped.y, rotation });
    } else if (steps[ev.key]) {
      ev.preventDefault();
      const [dx, dy] = steps[ev.key]!;
      const step = 2.54;
      const snapped = p.snap(part.id, part.place.x + dx * step, part.place.y + dy * step, part.place.rotation);
      p.onMove(part.id, { x: snapped.x, y: snapped.y, rotation: part.place.rotation });
    }
  };

  return (
    <div
      ref={hostRef}
      className={`eb-bench3d ${props.tool === 'wire' ? 'is-wiring' : ''}`}
      role="application"
      aria-label={props.label ?? 'Bench'}
      aria-roledescription="3D bench"
      tabIndex={0}
      onKeyDown={onKeyDown}
      onContextMenu={(e) => e.preventDefault()}
      data-terminal={feedback.ref}
      data-terminal-blocked={feedback.blocked || undefined}
    >
      {feedback.x !== undefined && feedback.y !== undefined && <span className="eb-terminal-target" style={{ left: feedback.x, top: feedback.y }} aria-hidden="true" />}
      <div className="eb-connection-feedback" role="status">{feedback.text}</div>
      {/* a pointer shortcut: its hidden faces cannot be seen, so it is out of the tab order;
          the View menu and the Z, Y, X and I keys give every view to the keyboard */}
      <div className="eb-viewcube" aria-hidden="true">
        <div className="eb-viewcube__cube" ref={cubeRef}>
          {CUBE_FACES.map((f) => (
            <button key={f.view} type="button" className="eb-viewcube__face" style={{ transform: f.transform }} tabIndex={-1} data-view={f.view} title={`${f.label} view`} onClick={() => frame(f.view)}>{f.label}</button>
          ))}
        </div>
      </div>
    </div>
  );
}
