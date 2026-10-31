// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
export { PITCH, benchShape, artworkAnchors, dipPins, isSurfaceMount, type BenchShape, type ShapePackage } from './shape';
export {
  PLUG_TOLERANCE,
  addJumper,
  dressJumper,
  reconnectJumper,
  jumperNet,
  traceBench,
  validateJumperStyle,
  type JumperStyle,
  addNote,
  benchModel,
  footprint,
  legsOf,
  moveParts,
  nearestHole,
  previewInsertion,
  placeAll,
  removeJumper,
  setNote,
  turn,
  unbuilt,
  type BenchContext,
  type BenchModel,
  type Connection,
  type Hole,
  type InsertionContact,
  type InsertionPreview,
  type PlacedPart,
} from './layout';
export { benchContext, elevationOf, jumperViews, linkViews, netColor, snapPart, terminalNear, terminalPoint, terminalTargets, WIRE_COLORS, type BenchCatalogEntry } from './view';
export type { Bench3DHandle, Bench3DProps, BenchPartView, BenchTool, JumperView, LinkView, Point3, SnapResult, TerminalHit } from './Bench3D';
export type { Model3d } from './models3d';
export { dayOfYear, solarPosition, sunDirection, sunlit } from './sun';
export { ROOM, windowOpening } from './room';
export { beams, bodiesAt, bodyAt, rayDistance, type Beam } from './world';
