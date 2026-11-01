// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The instrument front panels floating over the workbench: each in a window
// that stays put across perspectives, moved by its title bar (or the arrow
// keys on it), resized from its corner (or Shift+arrows), raised by a click,
// and docked back into the Run perspective.

import { IconButton, type Command } from '@embsim/design';
import { Gauge, PanelBottomClose, PictureInPicture2 } from 'lucide-react';
import { useEffect, useMemo, type KeyboardEvent, type PointerEvent, type ReactNode } from 'react';
import { benchInstrumentOf, benchInstrumentParts, openFrontPanel, titleOf } from '../perspectives/run/benchInstruments';
import { useRegisterCommands } from './commands';
import { useFloating, type FloatingRect } from '../stores/floating';
import { useProject } from '../stores/project';

const MIN_W = 360;
const MIN_H = 220;
const STEP = 16;

function clamp(r: FloatingRect): FloatingRect {
  const vw = window.innerWidth;
  const vh = window.innerHeight;
  const width = Math.max(MIN_W, Math.min(r.width, vw));
  const height = Math.max(MIN_H, Math.min(r.height, vh));
  // keep the title bar reachable
  return { width, height, x: Math.max(0, Math.min(r.x, vw - Math.min(width, 120))), y: Math.max(0, Math.min(r.y, vh - 36)) };
}

export function FloatingPanels() {
  const floating = useFloating((s) => s.floating);
  const components = useProject((s) => s.project?.components);
  const dock = useFloating((s) => s.dock);
  const float = useFloating((s) => s.float);
  const project = useProject((s) => s.project);
  // each instrument's panel from the palette, in any perspective
  const commands = useMemo<Command[]>(
    () =>
      benchInstrumentParts(project).flatMap(({ part, instrument }) => [
        { id: `panel.open.${part}`, group: 'Instruments', label: `Open ${titleOf(instrument, part)}`, icon: <Gauge />, keywords: ['front panel', 'instrument'], run: () => openFrontPanel(instrument, part) },
        floating.includes(part)
          ? { id: `panel.dock.${part}`, group: 'Instruments', label: `Dock ${titleOf(instrument, part)} in Run`, icon: <PanelBottomClose />, run: () => dock(part) }
          : { id: `panel.float.${part}`, group: 'Instruments', label: `Float ${titleOf(instrument, part)}`, icon: <PictureInPicture2 />, keywords: ['window'], run: () => float(part) },
      ]),
    [project, floating, dock, float],
  );
  useRegisterCommands('floating-panels', commands);
  // a part removed from the project takes its window with it
  useEffect(() => {
    if (!components) return;
    for (const part of floating) if (!components.some((c) => c.id === part && benchInstrumentOf(c.component))) dock(part);
  }, [components, floating, dock]);
  return (
    <>
      {floating.map((part, z) => {
        const instrument = benchInstrumentOf(components?.find((c) => c.id === part)?.component);
        return instrument ? <FloatingWindow key={part} part={part} z={z} title={titleOf(instrument, part)} size={instrument.size} content={instrument.panel(part)} /> : null;
      })}
    </>
  );
}

function FloatingWindow({ part, z, title, size, content }: { part: string; z: number; title: string; size: { width: number; height: number }; content: ReactNode }) {
  const stored = useFloating((s) => s.rects[part]);
  const { place, raise, dock } = useFloating.getState();
  const rect = clamp(stored ?? { x: 80 + z * 32, y: 80 + z * 32, ...size });

  // drag from the title bar, or resize from the corner
  const drag = (kind: 'move' | 'size') => (e: PointerEvent<HTMLElement>) => {
    if (e.button !== 0 || (e.target as HTMLElement).closest('button')) return;
    e.preventDefault();
    raise(part);
    const start = { x: e.clientX, y: e.clientY, rect };
    const target = e.currentTarget;
    target.setPointerCapture(e.pointerId);
    const move = (ev: globalThis.PointerEvent) => {
      const dx = ev.clientX - start.x;
      const dy = ev.clientY - start.y;
      place(part, clamp(kind === 'move' ? { ...start.rect, x: start.rect.x + dx, y: start.rect.y + dy } : { ...start.rect, width: start.rect.width + dx, height: start.rect.height + dy }));
    };
    const up = () => {
      target.removeEventListener('pointermove', move);
      target.removeEventListener('pointerup', up);
      target.removeEventListener('pointercancel', up);
    };
    target.addEventListener('pointermove', move);
    target.addEventListener('pointerup', up);
    target.addEventListener('pointercancel', up);
  };
  const keys = (e: KeyboardEvent<HTMLElement>) => {
    const d = { ArrowLeft: [-STEP, 0], ArrowRight: [STEP, 0], ArrowUp: [0, -STEP], ArrowDown: [0, STEP] }[e.key];
    if (!d) return;
    e.preventDefault();
    place(part, clamp(e.shiftKey ? { ...rect, width: rect.width + d[0]!, height: rect.height + d[1]! } : { ...rect, x: rect.x + d[0]!, y: rect.y + d[1]! }));
  };

  return (
    <div
      className="wb-floating"
      role="dialog"
      aria-label={title}
      style={{ left: rect.x, top: rect.y, width: rect.width, height: rect.height, zIndex: 40 + z }}
      onPointerDownCapture={() => raise(part)}
    >
      <div
        className="wb-floating__bar"
        role="group"
        tabIndex={0}
        aria-label={`${title}: arrow keys move the window, Shift+arrow keys resize it`}
        onPointerDown={drag('move')}
        onKeyDown={keys}
      >
        <span className="wb-floating__title">{title}</span>
        <IconButton size="sm" variant="ghost" label={`Dock ${title} in Run`} onClick={() => dock(part)}><PanelBottomClose /></IconButton>
      </div>
      <div className="wb-floating__body">{content}</div>
      <div className="wb-floating__grip" aria-hidden="true" onPointerDown={drag('size')} />
    </div>
  );
}
