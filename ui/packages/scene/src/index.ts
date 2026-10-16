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
  type Selection,
  type PinRef,
  type NetRef,
  type Viewport,
  type NetVisual,
} from './SchematicCanvas';
export { boardSymbol, portGroups, type BoardPort } from './boardSymbol';
export { busText, busValue } from './bus';
export { BoardView, type BoardViewProps, type BoardItem } from './BoardView';
