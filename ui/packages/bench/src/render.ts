// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The bench's two render qualities (docs/engineering/bench-3d.md, stage 1).
// Draft is the plain forward render. Quality renders into a 4x multisampled
// half-float target, adds ground-truth ambient occlusion (the shade in the
// corners between a part and its board), and blooms what emits light (the
// LEDs and indicators the engine lights) and nothing else: a second render
// in which every other surface is black feeds the bloom, mixed back into the
// image before tone mapping. A lit white desk is brighter than white in
// linear light, so a threshold alone would bloom it too. Presentation only:
// what the circuit does never depends on it. Quality falls back to draft
// when a frame's render takes longer than the 60 Hz floor of ADR-028 on the
// CPU side, which is a lower bound on its cost.

import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { GTAOPass } from 'three/examples/jsm/postprocessing/GTAOPass.js';
import { OutlinePass } from 'three/examples/jsm/postprocessing/OutlinePass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';

export type RenderQuality = 'draft' | 'quality';

/** the 60 Hz floor of ADR-028, ms */
export const FRAME_FLOOR_MS = 1000 / 60;

/** whether a material emits light the bloom should spread: lit emissive, not a plain colour */
export function glows(material: THREE.Material): boolean {
  const m = material as THREE.MeshStandardMaterial;
  return m.isMeshStandardMaterial === true && m.emissiveIntensity > 0.05 && (m.emissive.r + m.emissive.g + m.emissive.b > 0.02 || m.emissiveMap !== null);
}

const MIX_VERTEX = 'varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }';
const MIX_FRAGMENT = 'uniform sampler2D tDiffuse; uniform sampler2D bloom; varying vec2 vUv; void main() { gl_FragColor = texture2D(tDiffuse, vUv) + vec4(texture2D(bloom, vUv).rgb, 0.0); }';

/** clips a pass's own material as the section clips the bench's */
function clipMaterial(m: THREE.Material, planes: THREE.Plane[] | null): void {
  if (m.clippingPlanes === planes) return;
  m.clippingPlanes = planes;
  m.needsUpdate = true;
}

/** runs `draw` with the glow sprites and the section's stencil helpers hidden (passes that override materials) */
function withoutHelpers(scene: THREE.Scene, draw: () => void): void {
  const hidden: THREE.Object3D[] = [];
  scene.traverseVisible((o) => { if (o instanceof THREE.Sprite || o.userData.stencil) hidden.push(o); });
  for (const o of hidden) o.visible = false;
  try {
    draw();
  } finally {
    for (const o of hidden) o.visible = true;
  }
}

/** the outline a selected part (and the part under the pointer) gets, as CAD tools draw it */
function outlinePass(scene: THREE.Scene, camera: THREE.Camera, width: number, height: number): OutlinePass {
  const pass = new OutlinePass(new THREE.Vector2(width, height), scene, camera);
  // drawn after tone mapping, into the display image: its colours are display values
  // (sRGB), so they stay the colour they are named (tone-mapped, any visible blue washed to white)
  pass.edgeStrength = 3;
  pass.edgeThickness = 1;
  pass.edgeGlow = 0;
  pass.visibleEdgeColor.set('#0ea5e9').convertLinearToSRGB();
  pass.hiddenEdgeColor.set('#0b5d80').convertLinearToSRGB();
  // the outline's mask and depth renders override materials: the section's helpers stay out of them
  const render = pass.render.bind(pass);
  pass.render = (...args: Parameters<OutlinePass['render']>) => withoutHelpers(scene, () => render(...args));
  return pass;
}

/**
 * The draft render with outlines: used only while something is outlined, so
 * a plain draft frame stays a single forward render.
 */
export class OutlineRenderer {
  readonly composer: EffectComposer;
  private readonly render: RenderPass;
  readonly outline: OutlinePass;

  constructor(renderer: THREE.WebGLRenderer, scene: THREE.Scene, camera: THREE.Camera, width: number, height: number) {
    this.composer = new EffectComposer(renderer, new THREE.WebGLRenderTarget(width, height, { type: THREE.HalfFloatType, samples: 4, stencilBuffer: true }));
    this.render = new RenderPass(scene, camera);
    this.outline = outlinePass(scene, camera, width, height);
    this.composer.addPass(this.render);
    this.composer.addPass(new OutputPass());
    this.composer.addPass(this.outline);
  }

  setCamera(camera: THREE.Camera): void {
    this.render.camera = camera;
    this.outline.renderCamera = camera;
  }

  /** a section's planes (or none) for the outline's own depth render */
  setClipping(planes: THREE.Plane[] | null): void {
    clipMaterial(this.outline.depthMaterial, planes);
  }

  setSize(width: number, height: number, pixelRatio: number): void {
    this.composer.setPixelRatio(pixelRatio);
    this.composer.setSize(width, height);
  }

  dispose(): void {
    this.composer.dispose();
    this.outline.dispose();
  }
}

export class QualityRenderer {
  private readonly glow: EffectComposer;
  readonly composer: EffectComposer;
  private readonly passes: { render: RenderPass; ao: GTAOPass; glowRender: RenderPass };
  readonly outline: OutlinePass;
  private readonly black = new THREE.MeshBasicMaterial({ color: 0x000000 });
  private readonly times: number[] = [];
  /** the first frames compile shaders and are not counted */
  private warmup = 4;

  constructor(renderer: THREE.WebGLRenderer, private readonly scene: THREE.Scene, camera: THREE.Camera, width: number, height: number) {
    const target = () => new THREE.WebGLRenderTarget(width, height, { type: THREE.HalfFloatType, samples: 4, stencilBuffer: true });
    this.glow = new EffectComposer(renderer, target());
    this.glow.renderToScreen = false;
    const glowRender = new RenderPass(scene, camera);
    this.glow.addPass(glowRender);
    this.glow.addPass(new UnrealBloomPass(new THREE.Vector2(width, height), 1.1, 0.45, 0));

    this.composer = new EffectComposer(renderer, target());
    const render = new RenderPass(scene, camera);
    const ao = new GTAOPass(scene, camera, width, height);
    // the scene is in millimetres: occlusion over the few mm between a body and its board
    ao.updateGtaoMaterial({ radius: 14, distanceExponent: 2, thickness: 10, scale: 1.4, samples: 16 });
    ao.blendIntensity = 1;
    // the occlusion's depth and normals are of solid bodies: a glow sprite would be a solid quad
    // there, and a section's stencil helpers (drawn with an override material) whole, uncut bodies
    const aoRender = ao.render.bind(ao);
    ao.render = (...args: Parameters<GTAOPass['render']>) => withoutHelpers(scene, () => aoRender(...args));
    const mix = new ShaderPass(new THREE.ShaderMaterial({
      uniforms: { tDiffuse: { value: null }, bloom: { value: this.glow.renderTarget2.texture } },
      vertexShader: MIX_VERTEX,
      fragmentShader: MIX_FRAGMENT,
    }), 'tDiffuse');
    this.outline = outlinePass(scene, camera, width, height);
    this.composer.addPass(render);
    this.composer.addPass(ao);
    this.composer.addPass(mix);
    this.composer.addPass(new OutputPass());
    this.composer.addPass(this.outline);
    this.passes = { render, ao, glowRender };
  }

  /** a section's planes (or none) for the passes that draw the scene with their own materials */
  setClipping(planes: THREE.Plane[] | null): void {
    clipMaterial(this.passes.ao.normalMaterial, planes);
    clipMaterial(this.outline.depthMaterial, planes);
    clipMaterial(this.black, planes);
  }

  setCamera(camera: THREE.Camera): void {
    this.passes.render.camera = camera;
    this.passes.ao.camera = camera;
    this.passes.glowRender.camera = camera;
    this.outline.renderCamera = camera;
  }

  setSize(width: number, height: number, pixelRatio: number): void {
    for (const c of [this.glow, this.composer]) {
      c.setPixelRatio(pixelRatio);
      c.setSize(width, height);
    }
  }

  /** what emits light, alone on black: everything else swapped for black while the bloom's input renders */
  private renderGlow(): void {
    const swapped: [THREE.Mesh, THREE.Material | THREE.Material[]][] = [];
    const hidden: THREE.Object3D[] = [];
    this.scene.traverseVisible((o) => {
      if ((o as THREE.Mesh).isMesh && !o.userData.stencil) {
        const mesh = o as THREE.Mesh;
        const mats = Array.isArray(mesh.material) ? mesh.material : [mesh.material];
        if (!mats.some(glows)) {
          swapped.push([mesh, mesh.material]);
          mesh.material = this.black;
        }
      } else if (o instanceof THREE.Sprite || o instanceof THREE.Line) hidden.push(o);
      if (o.userData.stencil) hidden.push(o);
    });
    for (const o of hidden) o.visible = false;
    const background = this.scene.background;
    const environment = this.scene.environment;
    this.scene.background = null;
    this.scene.environment = null;
    this.glow.render();
    this.scene.background = background;
    this.scene.environment = environment;
    for (const o of hidden) o.visible = true;
    for (const [mesh, material] of swapped) mesh.material = material;
  }

  /** renders a frame; false when the frames have been over the budget (the caller drops to draft) */
  draw(): boolean {
    const t0 = performance.now();
    this.still();
    if (this.warmup > 0) {
      this.warmup--;
      return true;
    }
    this.times.push(performance.now() - t0);
    if (this.times.length > 8) this.times.shift();
    if (this.times.length < 8) return true;
    const mean = this.times.reduce((a, b) => a + b, 0) / this.times.length;
    return mean <= FRAME_FLOOR_MS;
  }

  /** one frame, without the budget's bookkeeping (a still for export) */
  still(): void {
    this.renderGlow();
    this.composer.render();
  }

  dispose(): void {
    this.glow.dispose();
    this.composer.dispose();
    this.passes.ao.dispose();
    this.outline.dispose();
    this.black.dispose();
  }
}
