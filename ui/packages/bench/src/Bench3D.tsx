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
import { ArtworkSurface } from './artwork';
import { buildPart, type Built, type PickData } from './parts3d';
import type { BenchShape } from './shape';
import { pickTerminal, type ProjectedTerminal } from './picking';
import type { Beam } from './world';


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
}

export interface TerminalHit {
  ref: string;
  at: Point3;
}

export type BenchTool = 'select' | 'wire';

export interface Bench3DHandle {
  /** frames the camera on everything on the bench */
  frame(view?: 'perspective' | 'top', selectedOnly?: boolean): void;
  /** the canvas, for screenshots */
  canvas(): HTMLCanvasElement | null;
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
  surface?: 'mat' | 'paper' | 'graphite';
  labels?: 'adaptive' | 'all' | 'selected';
  showNotes?: boolean;
  onRendererError?(): void;
  onMove(id: string, place: Required<BenchPlace>): void;
  onWire(from: string, to: string): void;
  onSelect(sel: { part?: string; jumper?: number }): void;
  onDelete(sel: { part?: string; jumper?: number }): void;
  onInput(target: string, input: string, value: unknown): void;
  label?: string;
  handle?: Ref<Bench3DHandle>;
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

/**
 * A jumper's path. Between two holes of one surface it lies flat, as a
 * pre-cut breadboard jumper does: up 1.5 mm, along, down. Anywhere else it
 * is a flexible Dupont wire: up from each end, then an arc that rises with
 * its length.
 */
export function jumperCurve(a: Point3, b: Point3): THREE.Curve<THREE.Vector3> {
  const A = v3(a);
  const B = v3(b);
  const len = A.distanceTo(B);
  if (Math.abs(A.y - B.y) < 0.01 && len < 60) {
    const h = A.y + 1.5;
    const path = new THREE.CurvePath<THREE.Vector3>();
    const pts = [A, A.clone().setY(h - 0.6), A.clone().setY(h).lerp(B.clone().setY(h), Math.min(0.5, 0.8 / Math.max(len, 0.1))), B.clone().setY(h).lerp(A.clone().setY(h), Math.min(0.5, 0.8 / Math.max(len, 0.1))), B.clone().setY(h - 0.6), B];
    for (let i = 0; i < pts.length - 1; i++) path.add(new THREE.LineCurve3(pts[i]!, pts[i + 1]!));
    return path;
  }
  const lift = Math.min(18, 5 + len * 0.1);
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

export function Bench3D(props: Bench3DProps) {
  const hostRef = useRef<HTMLDivElement>(null);
  const cancelGesture = useRef<(() => void) | null>(null);
  const [feedback, setFeedback] = useState<{ text: string; x?: number; y?: number; ref?: string }>({ text: '' });
  const propsRef = useRef(props);
  propsRef.current = props;
  useEffect(() => { cancelGesture.current?.(); }, [props.tool, props.editable, props.live]);
  const state = useRef<{
    renderer: THREE.WebGLRenderer;
    desk: THREE.Mesh<THREE.PlaneGeometry, THREE.MeshStandardMaterial>;
    labels: CSS2DRenderer;
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    parts: Map<string, PartEntry>;
    jumpers: THREE.Group;
    links: THREE.Group;
    notes: THREE.Group;
    world: THREE.Group;
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
      s.renderer.render(s.scene, s.camera);
      s.labels.render(s.scene, s.camera);
    });
  }, []);

  const frame = useCallback((view?: 'perspective' | 'top', selectedOnly = false) => {
    const s = state.current;
    if (!s) return;
    const mode = view ?? (s.camera.position.clone().sub(s.controls.target).normalize().y > 0.99 ? 'top' : 'perspective');
    const box = new THREE.Box3();
    const chosen = selectedOnly && propsRef.current.selected?.part;
    for (const e of s.parts.values()) if (!chosen || e.view.id === chosen) box.expandByObject(e.built.group);
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
    const centre = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    // the whole bench in view, seen from the front and above as a person at the desk sees it
    const fov = (s.camera.fov * Math.PI) / 180;
    const fit = Math.max(size.z, size.x / Math.max(0.5, s.camera.aspect)) / 2 / Math.tan(fov / 2);
    const r = fit * 1.05 + 20;
    s.controls.target.copy(centre);
    if (mode === 'top') s.camera.position.set(centre.x, centre.y + r, centre.z + 0.001);
    else s.camera.position.set(centre.x, centre.y + r * 0.78, centre.z + r * 0.68);
    s.controls.update();
    s.camera.near = 1;
    s.camera.far = r * 20;
    s.camera.updateProjectionMatrix();
    invalidate();
  }, [invalidate]);

  useImperativeHandle(props.handle, () => ({ frame, canvas: () => state.current?.renderer.domElement ?? null }), [frame]);

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
    const preview = new THREE.Group();
    scene.add(jumpers, links, notes, world, preview);
    state.current = { renderer, labels, scene, camera, controls, desk, parts: new Map(), jumpers, links, notes, world, preview, raf: 0, framed: false };

    controls.addEventListener('change', invalidate);

    const resize = () => {
      const w = host.clientWidth || 1;
      const h = host.clientHeight || 1;
      renderer.setSize(w, h, false);
      renderer.domElement.style.width = `${w}px`;
      renderer.domElement.style.height = `${h}px`;
      labels.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
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
        label.position.set(0, v.shape.size[2] + 14, -v.shape.size[1] / 2);
        built.group.add(label);
        s.scene.add(built.group);
        e = { key, built, surface, label, view: v };
        s.parts.set(v.id, e);
      }
      e.view = v;
      e.built.group.position.set(v.place.x, v.elevation, v.place.y);
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
      const lum = typeof v.outputs?.luminance === 'number' ? v.outputs.luminance : 0;
      // the eye sees brightness roughly as its cube root: a 30 % duty LED looks well lit
      const bright = lum > 0 ? Math.cbrt(Math.min(1, lum)) : 0;
      if (e.built.glow) e.built.glow.emissiveIntensity = bright * 3.5;
      if (e.built.light) e.built.light.intensity = bright * 450;
      if (e.built.halo) {
        e.built.halo.visible = bright > 0.02;
        (e.built.halo.material as THREE.SpriteMaterial).opacity = Math.min(0.9, bright);
        e.built.halo.scale.setScalar(10 + bright * 14);
      }
      for (const ind of e.built.indicators ?? []) {
        const v2 = v.outputs?.[ind.output];
        const on = typeof v2 === 'number' ? Math.cbrt(Math.max(0, Math.min(1, v2))) : v2 ? 1 : 0;
        ind.material.emissiveIntensity = on * 3;
        if (ind.halo) {
          ind.halo.visible = on > 0.02;
          (ind.halo.material as THREE.SpriteMaterial).opacity = Math.min(0.9, on);
        }
      }
      const ctl = e.built.control;
      if (ctl) {
        const d = ctl.userData as PickData;
        if (d.gesture === 'hold') ctl.position.y = 4.45 - (v.inputs?.[d.input!] ? 0.45 : 0);
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
  }, [props.parts, props.selected, props.labels, invalidate, frame]);

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
    const pinMat = new THREE.MeshStandardMaterial({ color: 0xc9ccd1, metalness: 0.9, roughness: 0.3 });
    const housingMat = new THREE.MeshStandardMaterial({ color: 0x111111, roughness: 0.5 });
    for (const j of props.jumpers) {
      const g = new THREE.Group();
      const sel = props.selected?.jumper === j.index;
      const insul = new THREE.MeshStandardMaterial({ color: new THREE.Color(j.color), roughness: 0.45, emissive: sel ? new THREE.Color(j.color) : new THREE.Color(0), emissiveIntensity: sel ? 0.5 : 0 });
      const tube = new THREE.Mesh(new THREE.TubeGeometry(jumperCurve(j.a, j.b), 96, 0.8, 10, false), insul);
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
  }, [props.jumpers, props.links, props.notes, props.selected, invalidate]);

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
    const hitParts = () => ray.intersectObjects([...[...s.parts.values()].map((e) => e.built.group), s.jumpers, s.world], true);
    let dragBody: { id: string; mesh: THREE.Object3D; dx: number; dz: number; moved: boolean; start: THREE.Vector3 } | null = null;
    let activePointer: number | null = null;

    let drag: { id: string; dx: number; dz: number; moved: boolean; start: Required<BenchPlace>; rotation: number } | null = null;
    let wire: { from: TerminalHit; line: THREE.Mesh | null } | null = null;
    let held: { target: string; input: string } | null = null;
    let turning: { target: string; input: string; x: number; v: number } | null = null;
    const holeMarks: THREE.Mesh[] = [];
    const markMat = new THREE.MeshBasicMaterial({ color: 0x2ecc71 });
    const clearMarks = () => {
      for (const m of holeMarks) {
        s.preview.remove(m);
        m.geometry.dispose();
      }
      holeMarks.length = 0;
    };
    const showMarks = (pts: Point3[]) => {
      clearMarks();
      for (const p of pts) {
        const m = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.9, 0.3, 12), markMat);
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
        const blocker = occlusionRay.intersectObjects([...s.parts.values()].map((e) => e.built.group), true)
          .find((hit) => (hit.object.userData as PickData).part !== terminal.ref.split('.')[0] && hit.distance + 0.5 < s.camera.position.distanceTo(world));
        if (!blocker) projected.push({ terminal, x, y, depth: screen.z });
      }
      return pickTerminal(projected, ev.clientX - r.left, ev.clientY - r.top);
    };
    const feedbackAt = (ev: PointerEvent, from?: TerminalHit) => {
      const pick = terminalAtPointer(ev);
      const terminal = pick.kind === 'terminal' ? pick.terminal : undefined;
      const point = terminal ? v3(terminal.at).project(s.camera) : undefined;
      const r = el.getBoundingClientRect();
      const text = pick.kind === 'ambiguous' ? 'Contacts overlap — zoom in to choose a pin'
        : terminal ? from ? terminal.ref === from.ref ? `${from.ref} · choose another terminal` : `${from.ref} → ${terminal.ref} · release to connect` : `${terminal.ref} · drag to connect`
        : from ? `${from.ref} → choose a terminal · Esc cancels` : 'Point at a hole or pin to start a jumper';
      setFeedback({ text, ref: terminal?.ref, x: point ? (point.x + 1) * r.width / 2 : undefined, y: point ? (1 - point.y) * r.height / 2 : undefined });
      el.style.cursor = terminal ? 'var(--draw-cursor, crosshair)' : 'default';
      return terminal;
    };
    const down = (ev: PointerEvent) => {
      if (ev.button !== 0 || activePointer !== null) return;
      host.focus({ preventScroll: true });
      const p = propsRef.current;
      cast(ev);
      const hits = hitParts();
      const hit = hits[0];
      const data = (hit?.object.userData ?? {}) as PickData & { jumper?: number };
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
        showMarks(snapped.holes);
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
      else {
        const hit = hitParts()[0];
        const data = hit?.object.userData as PickData | undefined;
        el.style.cursor = data?.input && p.live ? 'pointer' : data?.part && p.editable ? 'grab' : 'default';
      }
    };
    const up = (ev: PointerEvent) => {
      if (ev.pointerId !== activePointer) return;
      cast(ev);
      const p = propsRef.current;
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
          p.onMove(drag.id, { x: snapped.x, y: snapped.y, rotation: drag.rotation as 0 | 90 | 180 | 270 });
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
        if (wire.line) {
          s.preview.remove(wire.line);
          wire.line.geometry.dispose();
          (wire.line.material as THREE.Material).dispose();
        }
        if (t && t.ref !== wire.from.ref) p.onWire(wire.from.ref, t.ref);
        wire = null;
        clearMarks();
        invalidate();
      }
      activePointer = null;
      setFeedback({ text: '' });
      if (el.hasPointerCapture(ev.pointerId)) el.releasePointerCapture(ev.pointerId);
    };
    const cancel = () => {
      if (held) propsRef.current.onInput(held.target, held.input, false);
      held = null;
      turning = null;
      if (dragBody) dragBody.mesh.position.copy(dragBody.start);
      dragBody = null;
      if (drag) {
        const e = s.parts.get(drag.id);
        if (e) e.built.group.position.set(e.view.place.x, e.view.elevation, e.view.place.y);
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
      if (ev.key === 'Escape') { ev.preventDefault(); cancel(); }
    };
    cancelGesture.current = cancel;
    const leave = () => { if (activePointer === null) setFeedback({ text: '' }); };
    const visibility = () => { if (document.hidden) cancel(); };
    host.addEventListener('keydown', escape);
    window.addEventListener('blur', cancel);
    document.addEventListener('visibilitychange', visibility);
    el.addEventListener('lostpointercapture', cancel);
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
      el.removeEventListener('lostpointercapture', cancel);
      el.removeEventListener('pointerleave', leave);
      host.removeEventListener('keydown', escape);
      window.removeEventListener('blur', cancel);
      document.removeEventListener('visibilitychange', visibility);
      cancel();
      cancelGesture.current = null;
      markMat.dispose();
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
    >
      {feedback.x !== undefined && feedback.y !== undefined && <span className="eb-terminal-target" style={{ left: feedback.x, top: feedback.y }} aria-hidden="true" />}
      <div className="eb-connection-feedback" role="status">{feedback.text}</div>
    </div>
  );
}
