// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The workbench's own panels that can leave their perspective: floated, a
// panel shows over every perspective in a window of its own (the instrument
// front panels' windows, shell/FloatingPanels.tsx) and its home dock keeps
// its place with a way back. Only panels that read the session's stores, and
// need nothing from the perspective around them, are here.
import { Button, EmptyState, IconButton } from '@embsim/design';
import { PanelBottomClose, PictureInPicture2 } from 'lucide-react';
import type { ReactNode } from 'react';
import { useFloating } from '../stores/floating';
import { DiagnosticsPanel } from '../shell/DiagnosticsPanel';
import { AirPanel } from './run/AirPanel';
import { MetersPanel } from './run/MetersPanel';
import {
  BreakpointsPanel,
  ConsolePanel,
  DisassemblyPanel,
  MemoryPanel,
  PeripheralsPanel,
  RegistersPanel,
  SourcePanel,
} from './debug/DebugPerspective';
import type { Perspective } from '../stores/layout';
import { t as tr, type MessageKey } from '../i18n';

export interface MovablePanel {
  id: string;
  /** the perspective it docks in */
  home: Perspective;
  titleKey: MessageKey;
  size: { width: number; height: number };
  content: () => ReactNode;
}

const PANELS: MovablePanel[] = [
  { id: 'meters', home: 'run', titleKey: 'run.tab.meters', size: { width: 640, height: 260 }, content: () => <MetersPanel /> },
  { id: 'air', home: 'run', titleKey: 'run.theAir', size: { width: 620, height: 380 }, content: () => <AirPanel /> },
  { id: 'diagnostics', home: 'run', titleKey: 'run.tab.diagnostics', size: { width: 720, height: 300 }, content: () => <DiagnosticsPanel /> },
  { id: 'source', home: 'debug', titleKey: 'debug.source', size: { width: 640, height: 420 }, content: () => <SourcePanel /> },
  { id: 'disassembly', home: 'debug', titleKey: 'debug.disassembly', size: { width: 560, height: 380 }, content: () => <DisassemblyPanel /> },
  { id: 'console', home: 'debug', titleKey: 'debug.console', size: { width: 560, height: 260 }, content: () => <ConsolePanel /> },
  { id: 'registers', home: 'debug', titleKey: 'debug.registers', size: { width: 420, height: 420 }, content: () => <RegistersPanel /> },
  { id: 'breakpoints', home: 'debug', titleKey: 'debug.breakpoints', size: { width: 420, height: 260 }, content: () => <BreakpointsPanel /> },
  { id: 'memory', home: 'debug', titleKey: 'debug.memory', size: { width: 560, height: 360 }, content: () => <MemoryPanel /> },
  { id: 'peripherals', home: 'debug', titleKey: 'debug.peripherals', size: { width: 520, height: 380 }, content: () => <PeripheralsPanel /> },
];

/** the floating key of a movable panel (instrument windows are keyed by their part's id) */
export const PANEL_PREFIX = 'panel:';
export const floatingKey = (id: string) => `${PANEL_PREFIX}${id}`;

export function movablePanel(key: string): MovablePanel | undefined {
  if (!key.startsWith(PANEL_PREFIX)) return undefined;
  const id = key.slice(PANEL_PREFIX.length);
  return PANELS.find((p) => p.id === id);
}

export function movablePanels(): readonly MovablePanel[] {
  return PANELS;
}

export const panelTitle = (p: MovablePanel) => tr(p.titleKey);

/** whether panel `id` floats, and the means to float it or dock it home */
export function useFloatingPanel(id: string) {
  const key = floatingKey(id);
  const floating = useFloating((s) => s.floating.includes(key));
  return { floating, float: () => useFloating.getState().float(key), dock: () => useFloating.getState().dock(key) };
}

/** the action in a panel's header that floats it over every perspective */
export function FloatPanelButton({ id }: { id: string }) {
  const p = PANELS.find((x) => x.id === id);
  const { floating, float } = useFloatingPanel(id);
  if (!p || floating) return null;
  return (
    <IconButton size="sm" variant="ghost" label={tr('mp.float', { name: panelTitle(p) })} onClick={float}>
      <PictureInPicture2 />
    </IconButton>
  );
}

/** a panel in its home dock: itself, or while it floats, where it went and the way back */
export function Docked({ id, children }: { id: string; children: ReactNode }) {
  const p = PANELS.find((x) => x.id === id);
  const { floating, dock } = useFloatingPanel(id);
  if (!p || !floating) return <>{children}</>;
  return (
    <EmptyState icon={<PictureInPicture2 />} title={tr('mp.floating', { name: panelTitle(p) })}>
      <Button size="sm" variant="default" onClick={dock}>
        <PanelBottomClose /> {tr('mp.dockHere')}
      </Button>
    </EmptyState>
  );
}
