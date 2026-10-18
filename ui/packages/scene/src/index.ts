// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
export * from './geometry';
export * from './commands';
export { parseSvg, type ParsedSvg } from './svg';
export { ComponentSymbol, SymbolThumbnail, type ComponentSymbolProps } from './ComponentSymbol';
export {
  SchematicCanvas,
  MemoSchematicCanvas,
  AnalogLegend,
  analogColor,
  describeNetState,
  EMPTY_SELECTION,
  type SchematicCanvasProps,
  type SchematicCanvasHandle,
  type SceneCatalog,
  type SceneSymbol,
  type CanvasMode,
  type CanvasContext,
  type Selection,
  type PinRef,
  type NetRef,
  type Viewport,
  type NetVisual,
} from './SchematicCanvas';
export { boardSymbol, portGroups, type BoardPort } from './boardSymbol';
export { busText, busValue } from './bus';
export { BoardView, routeWire, type BoardViewProps, type BoardItem, type BenchWire, type WireEnd } from './BoardView';
export { autoPlace, autoRoute, createLayout, arrange, layoutParts, routeConnection, freePlacement, type LayoutOptions, type LayoutResult, type Arrangement } from './autoLayout';
export { canvasBackground, normalizeAppearance, DEFAULT_CANVAS_APPEARANCE, type CanvasAppearance, type CanvasPalette, type GridStyle } from './canvasAppearance';
export { shiftWireSegment, projectPinPoints, cleanWirePoints, segmentContacts } from './wireEditing';
export { checkSchematic, type SchematicIssue, type CheckSeverity } from './schematicChecks';
