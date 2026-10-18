// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
export { PITCH, benchShape, artworkAnchors, dipPins, type BenchShape, type ShapePackage } from './shape';
export {
  PLUG_TOLERANCE,
  addJumper,
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
export { Bench3D, jumperCurve, type Bench3DHandle, type Bench3DProps, type BenchPartView, type BenchTool, type JumperView, type LinkView, type Point3, type SnapResult, type TerminalHit } from './Bench3D';
export { parseValue, resistorBands, ledColor } from './parts3d';
export { beams, rayDistance, type Beam } from './world';
