// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, IconButton, Input, Menu, SegmentedControl, Select, Tooltip, useToast, type Command, type MenuItem, type ThemePreference } from '@embsim/design';
import { formatPsRaw, formatTime, parseDuration, type Policy } from '@embsim/protocol';
import { useCommandStack } from '@embsim/scene';
import { AlertTriangle, ChevronDown, Command as CommandIcon, CircuitBoard, Moon, Pause, Play, Redo2, RotateCcw, SkipForward, StepForward, Sun, Undo2 } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { controls, errorMessage } from '../actions';
import { PERSPECTIVES, useLayout, type Perspective } from '../stores/layout';
import { useProject } from '../stores/project';
import { diagnosticSummary, useSession } from '../stores/session';
import { useTransportPreferences } from '../stores/transportPreferences';
import { perspectiveMeta } from './commands';

const SPEEDS: { value: string; label: string; speed: number }[] = [
  { value: '0.1', label: '0.1×', speed: 0.1 },
  { value: '1', label: '1×', speed: 1 },
  { value: '10', label: '10×', speed: 10 },
  { value: '0', label: 'Max', speed: 0 },
];

/** Virtual time against wall time over the last two seconds while running:
 * 1 is real time (the rate a bench's own clock shows). */
export function useRealTimeRate(now: bigint, running: boolean): number | null {
  const samples = useRef<{ wall: number; ps: bigint }[]>([]);
  const [rate, setRate] = useState<number | null>(null);
  useEffect(() => {
    if (!running) {
      samples.current = [];
      setRate(null);
      return;
    }
    const wall = performance.now();
    const list = samples.current;
    list.push({ wall, ps: now });
    while (list.length > 2 && wall - list[0]!.wall > 2000) list.shift();
    const first = list[0]!;
    const dt = wall - first.wall;
    if (dt >= 250) setRate(Number(now - first.ps) / 1e9 / dt);
  }, [now, running]);
  return rate;
}

export function Toolbar({ theme, commands = [] }: { theme: { resolved: 'dark' | 'light'; setPreference: (p: ThemePreference) => void; toggle: () => void }; commands?: Command[] }) {
  const now = useSession((s) => s.now);
  const running = useSession((s) => s.running);
  const rate = useRealTimeRate(now, running);
  const speed = useSession((s) => s.speed);
  const policy = useSession((s) => s.policy);
  const isDemo = useSession((s) => s.isDemo);
  const status = useSession((s) => s.status);
  const diagnostics = useSession((s) => s.diagnostics);
  const unread = useSession((s) => s.unreadDiagnostics);
  const perspective = useLayout((s) => s.perspective);
  const setPerspective = useLayout((s) => s.setPerspective);
  const setPaletteOpen = useLayout((s) => s.setPaletteOpen);
  const setDiagnosticsOpen = useLayout((s) => s.setDiagnosticsOpen);
  const stack = useProject((s) => s.commands);
  const { canUndo, canRedo, undoLabel, redoLabel } = useCommandStack(stack);
  const { toast } = useToast();
  const { advanceBy, setAdvanceBy } = useTransportPreferences();
  const advanceValid = (() => {
    try {
      return parseDuration(advanceBy) > 0n;
    } catch {
      return false;
    }
  })();
  const summary = diagnosticSummary(diagnostics);
  const projectName = useProject((s) => s.project?.name);
  // the project menu runs the palette's own commands
  const projectItems: MenuItem[] = ['project.new', 'project.open', 'project.save', 'project.save-as', 'view.gallery']
    .map((id) => commands.find((c) => c.id === id))
    .filter((c): c is Command => Boolean(c))
    .map((c) => ({ id: c.id, label: c.label, icon: c.icon, shortcut: c.shortcut, disabled: c.disabled, separator: c.id === 'view.gallery', onSelect: () => c.run() }));
  const connected = status === 'open';

  const guard = (fn: () => Promise<void>) => () => {
    fn().catch((e: unknown) => toast({ kind: 'error', title: 'Request failed', description: errorMessage(e) }));
  };

  return (
    <div className="wb-toolbar" role="toolbar" aria-label="Workbench toolbar">
      <div className="wb-toolbar__navigation">
        <div className="wb-toolbar__brand">
          {projectItems.length ? (
            <Menu
              label={`Project menu${projectName ? `: ${projectName}` : ''}`}
              items={projectItems}
              trigger={
                <>
                  <CircuitBoard aria-hidden="true" />
                  <span>EmbSIM</span>
                  {projectName && <span className="wb-toolbar__project">{projectName}</span>}
                  <ChevronDown aria-hidden="true" style={{ width: 14, height: 14, color: 'var(--text-muted)' }} />
                </>
              }
            />
          ) : (
            <>
              <CircuitBoard aria-hidden="true" />
              <span>EmbSIM</span>
            </>
          )}
        </div>
        <SegmentedControl<Perspective>
          className="wb-toolbar__perspectives"
          label="Perspective"
          value={perspective}
          onValueChange={setPerspective}
          options={PERSPECTIVES.map((p) => ({ value: p, label: <span className="wb-toolbar__label-text">{perspectiveMeta(p).label}</span>, icon: perspectiveMeta(p).icon, title: `${perspectiveMeta(p).label} (${perspectiveMeta(p).shortcut})` }))}
        />
        <div className="wb-toolbar__group wb-toolbar__utilities">
          <Tooltip content={`${summary.errors} errors, ${summary.warnings} warnings, ${summary.infos} notes`}>
            <IconButton label={`Diagnostics: ${diagnostics.length} records, ${unread} new`} className="wb-diag-badge" onClick={() => setDiagnosticsOpen(true)}>
              <AlertTriangle />
              {unread > 0 && <span className={`wb-diag-badge__count${summary.errors ? ' wb-diag-badge__count--error' : ''}`}>{unread > 99 ? '99+' : unread}</span>}
            </IconButton>
          </Tooltip>
          <Tooltip content={theme.resolved === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'}>
            <IconButton label={theme.resolved === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'} onClick={theme.toggle}>
              {theme.resolved === 'dark' ? <Sun /> : <Moon />}
            </IconButton>
          </Tooltip>
          <Tooltip content="Command palette" shortcut="Ctrl+K">
            <Button variant="ghost" icon={<CommandIcon />} onClick={() => setPaletteOpen(true)} aria-label="Open command palette">
              <span className="wb-toolbar__label-text">Commands</span>
            </Button>
          </Tooltip>
        </div>
      </div>
      <div className="wb-toolbar__simulation" role="group" aria-label="Simulation controls">
        <div className="wb-toolbar__group" role="group" aria-label="Transport">
          <Tooltip content={running ? 'Pause' : 'Run'} shortcut="F5">
            <IconButton label={running ? 'Pause' : 'Run'} variant={running ? 'default' : 'primary'} onClick={guard(controls.toggle)} disabled={!connected} data-testid="run-toggle">
              {running ? <Pause /> : <Play />}
            </IconButton>
          </Tooltip>
          <Tooltip content="Step one event" shortcut="F6">
            <IconButton label="Step" onClick={guard(() => controls.step('event'))} disabled={!connected || running}>
              <StepForward />
            </IconButton>
          </Tooltip>
          <Tooltip content={`Advance by ${advanceBy}`} shortcut="F7">
            <IconButton label={`Advance by ${advanceBy}`} onClick={guard(() => controls.advance(advanceBy))} disabled={!connected || running || !advanceValid}>
              <SkipForward />
            </IconButton>
          </Tooltip>
          <Input
            aria-label="Advance amount"
            wrapperClassName="wb-advance"
            mono
            value={advanceBy}
            onChange={(e) => setAdvanceBy(e.target.value)}
            error={advanceValid ? undefined : 'Enter a duration such as 10ms'}
            hint={undefined}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && advanceValid && !running) guard(() => controls.advance(advanceBy))();
            }}
          />
          <Tooltip content="Reset: time to zero, inputs released">
            <IconButton label="Reset" onClick={guard(controls.reset)} disabled={!connected}>
              <RotateCcw />
            </IconButton>
          </Tooltip>
        </div>

        <div className="wb-toolbar__group">
          <SegmentedControl
            label="Speed"
            size="sm"
            value={String(speed)}
            options={SPEEDS.map((s) => ({ value: s.value, label: s.label, title: s.speed === 0 ? 'Unbounded' : `${s.speed}× real time` }))}
            onValueChange={(v) => guard(() => controls.setSpeed(Number(v)))()}
          />
        </div>

        <Tooltip content={<span className="es-mono">{formatPsRaw(now)}</span>}>
          <div className={`wb-clock${running ? ' wb-clock--running' : ''}`} tabIndex={0} role="timer" aria-label={`Virtual time ${formatTime(now)}, ${running ? 'running' : 'paused'}`} data-testid="clock">
            <span className="wb-clock__state">{running ? 'run' : 'paused'}</span>
            <span>{formatTime(now)}</span>
            {rate !== null && (
              <span className="wb-clock__rate" title="Virtual time against wall time" data-testid="clock-rate">
                {rate >= 10 ? `${rate.toFixed(0)}×` : `${Math.round(rate * 100)}%`}
              </span>
            )}
          </div>
        </Tooltip>

        <div className="wb-toolbar__group">
          <Select
            label="Fidelity policy"
            size="sm"
            value={policy}
            disabled={running || !connected}
            options={[
              { value: 'functional', label: 'Functional' },
              { value: 'timed', label: 'Timed' },
              { value: 'electrical', label: 'Electrical' },
            ]}
            onValueChange={(v) => guard(() => controls.setPolicy(v as Policy))()}
          />
        </div>

        {perspective === 'design' && (
          <div className="wb-toolbar__group" role="group" aria-label="Edit">
            <Tooltip content={undoLabel ? `Undo ${undoLabel}` : 'Undo'} shortcut="Ctrl+Z">
              <IconButton label="Undo" disabled={!canUndo} onClick={guard(() => stack.undo())}>
                <Undo2 />
              </IconButton>
            </Tooltip>
            <Tooltip content={redoLabel ? `Redo ${redoLabel}` : 'Redo'} shortcut="Ctrl+Shift+Z">
              <IconButton label="Redo" disabled={!canRedo} onClick={guard(() => stack.redo())}>
                <Redo2 />
              </IconButton>
            </Tooltip>
          </div>
        )}
        <div className="wb-toolbar__session">
          {isDemo && (
            <Badge tone="accent" title="No engine is connected: an in-memory demo session answers the protocol">
              Demo session
            </Badge>
          )}
        </div>
      </div>
    </div>
  );
}
