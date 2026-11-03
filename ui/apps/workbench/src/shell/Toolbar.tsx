// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The workbench's header: two rows with distinct jobs.
//  - The app bar: the project, the four perspectives, and the utilities
//    (diagnostics, settings, the command palette).
//  - The simulation bar: what the simulation is doing and the controls that
//    change it, the same in every perspective. Its readout says the state in
//    words and an icon (never by colour alone) and keeps simulated time apart
//    from the host's rate.
import { Badge, Button, IconButton, Input, Menu, SegmentedControl, Select, Tooltip, useToast, type Command, type MenuItem, type ThemePreference } from '@embsim/design';
import { formatPsRaw, parseDuration, type Policy } from '@embsim/protocol';
import { useCommandStack } from '@embsim/scene';
import { AlertTriangle, ChevronDown, CircuitBoard, Command as CommandIcon, Loader2, Pause, Play, Plug, Redo2, RotateCcw, Settings as SettingsIcon, SkipForward, Square, StepForward, Undo2, XOctagon } from 'lucide-react';
import { controls, errorMessage } from '../actions';
import { useT, type MessageKey } from '../i18n';
import { PERSPECTIVES, useLayout, type Perspective } from '../stores/layout';
import { useProject } from '../stores/project';
import { diagnosticSummary, useSession } from '../stores/session';
import { useSettings } from '../stores/settings';
import { useTransportPreferences } from '../stores/transportPreferences';
import { perspectiveMeta } from './commands';
import { SIM_STATE_HINT, SIM_STATE_LABEL, formatRate, useRealTimeRate, useSimState, useSimTimeFormat, type SimState } from './simState';

export { useRealTimeRate } from './simState';

const SPEEDS = [
  { value: '0.1', speed: 0.1 },
  { value: '1', speed: 1 },
  { value: '10', speed: 10 },
  { value: '0', speed: 0 },
];

export function SimStateIcon({ state }: { state: SimState }) {
  const p = { 'aria-hidden': true as const };
  switch (state) {
    case 'running':
      return <Play {...p} />;
    case 'paused':
      return <Pause {...p} />;
    case 'stopped':
      return <Square {...p} />;
    case 'loading':
      return <Loader2 {...p} className="wb-spin" />;
    case 'failed':
      return <XOctagon {...p} />;
    default:
      return <Plug {...p} />;
  }
}

/** The state chip: an icon and a word, in the state's colour. */
export function SimStateChip({ state, compact = false }: { state: SimState; compact?: boolean }) {
  const t = useT();
  return (
    <span className={`wb-state wb-state--${state}${compact ? ' wb-state--compact' : ''}`} data-state={state} data-testid={compact ? 'status-sim-state' : 'sim-state'}>
      <SimStateIcon state={state} />
      <span className="wb-state__label">{t(SIM_STATE_LABEL[state])}</span>
    </span>
  );
}

function SimulationReadout() {
  const t = useT();
  const now = useSession((s) => s.now);
  const running = useSession((s) => s.running);
  const state = useSimState();
  const rate = useRealTimeRate(now, running);
  const rateFormat = useSettings((s) => s.rateFormat);
  const fmt = useSimTimeFormat();
  const time = fmt(now);
  return (
    <div className="wb-readout" role="group" aria-label={t('sim.simulatedTime')}>
      <Tooltip content={t(SIM_STATE_HINT[state], { time })}>
        <span tabIndex={0} className="wb-readout__state">
          <SimStateChip state={state} />
        </span>
      </Tooltip>
      <Tooltip content={<span className="es-mono">{t('sim.exactTime', { ps: formatPsRaw(now) })}</span>}>
        <div className={`wb-readout__field wb-clock${running ? ' wb-clock--running' : ''}`} tabIndex={0} role="timer" aria-label={t('sim.clockLabel', { time, state: t(SIM_STATE_LABEL[state]) })} data-testid="clock">
          <span className="wb-readout__caption">{t('sim.simulated')}</span>
          <span className="wb-readout__value es-mono">{time}</span>
        </div>
      </Tooltip>
      <Tooltip content={t('sim.hostRate.hint')}>
        <div className="wb-readout__field wb-readout__field--host" tabIndex={0} aria-label={`${t('sim.hostRate')} ${rate !== null ? formatRate(rate, rateFormat, t.lang) : t('common.unavailable')}`}>
          <span className="wb-readout__caption">{t('sim.host')}</span>
          {rate !== null ? (
            <span className="wb-readout__value wb-readout__value--host es-mono wb-clock__rate" data-testid="clock-rate">
              {formatRate(rate, rateFormat, t.lang)}
            </span>
          ) : (
            <span className="wb-readout__value wb-readout__value--idle es-mono">{t('sim.hostRate.idle')}</span>
          )}
        </div>
      </Tooltip>
    </div>
  );
}

export function Toolbar({ theme, commands = [] }: { theme: { resolved: 'dark' | 'light'; setPreference: (p: ThemePreference) => void; toggle: () => void }; commands?: Command[] }) {
  void theme;
  const t = useT();
  const running = useSession((s) => s.running);
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
  const setSettingsOpen = useSettings((s) => s.setSettingsOpen);
  const settingsOpen = useSettings((s) => s.settingsOpen);
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
  const projectItems: MenuItem[] = ['project.new', 'project.open', 'project.save', 'project.save-as', 'view.gallery']
    .map((id) => commands.find((c) => c.id === id))
    .filter((c): c is Command => Boolean(c))
    .map((c) => ({ id: c.id, label: c.label, icon: c.icon, shortcut: c.shortcut, disabled: c.disabled, separator: c.id === 'view.gallery', onSelect: () => c.run() }));
  const connected = status === 'open';

  const guard = (fn: () => Promise<void>) => () => {
    fn().catch((e: unknown) => toast({ kind: 'error', title: t('toast.requestFailed'), description: errorMessage(e) }));
  };
  const runLabel = running ? t('transport.pause') : t('transport.run');

  return (
    <header className="wb-header">
      <div className="wb-appbar" role="toolbar" aria-label={t('header.toolbar')}>
        <div className="wb-appbar__brand">
          <Menu
            label={projectName ? t('app.projectMenuOf', { name: projectName }) : t('app.projectMenu')}
            items={projectItems}
            trigger={
              <>
                <span className="wb-appbar__logo" aria-hidden="true">
                  <CircuitBoard />
                </span>
                <span className="wb-appbar__product">{t('app.name')}</span>
                <span className="wb-appbar__divider" aria-hidden="true" />
                <span className="wb-appbar__project wb-toolbar__project">{projectName ?? t('app.noProject')}</span>
                <ChevronDown aria-hidden="true" className="wb-appbar__chevron" />
              </>
            }
          />
        </div>
        <nav className="wb-tabs" aria-label={t('perspective.label')}>
          <div role="radiogroup" aria-label={t('perspective.label')} className="wb-tabs__list">
            {PERSPECTIVES.map((p) => {
              const meta = perspectiveMeta(p);
              const label = t(`perspective.${p}` as MessageKey);
              const active = perspective === p;
              return (
                <Tooltip key={p} content={t(`perspective.${p}.hint` as MessageKey)} shortcut={meta.shortcut}>
                  <button type="button" role="radio" aria-checked={active} aria-label={label} className={`wb-tab${active ? ' wb-tab--active' : ''}`} onClick={() => setPerspective(p as Perspective)} data-perspective={p}>
                    {meta.icon}
                    <span className="wb-tab__label">{label}</span>
                  </button>
                </Tooltip>
              );
            })}
          </div>
        </nav>
        <div className="wb-appbar__utilities">
          {isDemo && (
            <Badge tone="accent" title={t('transport.demo.hint')}>
              {t('transport.demo')}
            </Badge>
          )}
          <Tooltip content={t('header.diagnosticsSummary', { errors: summary.errors, warnings: summary.warnings, infos: summary.infos })}>
            <IconButton label={t('header.diagnosticsCount', { total: diagnostics.length, unread })} className="wb-diag-badge" onClick={() => setDiagnosticsOpen(true)}>
              <AlertTriangle />
              {unread > 0 && <span className={`wb-diag-badge__count${summary.errors ? ' wb-diag-badge__count--error' : ''}`}>{unread > 99 ? '99+' : unread}</span>}
            </IconButton>
          </Tooltip>
          <Tooltip content={t('header.settings')} shortcut="Ctrl+,">
            <IconButton label={t('header.settings')} aria-pressed={settingsOpen} onClick={() => setSettingsOpen(!settingsOpen)} data-testid="open-settings">
              <SettingsIcon />
            </IconButton>
          </Tooltip>
          <Tooltip content={t('header.commandPalette')} shortcut="Ctrl+K">
            <Button variant="ghost" icon={<CommandIcon />} onClick={() => setPaletteOpen(true)} aria-label={t('header.openPalette')}>
              <span className="wb-appbar__label">{t('header.commands')}</span>
            </Button>
          </Tooltip>
        </div>
      </div>

      <div className="wb-simbar" role="group" aria-label={t('transport.group')}>
        <div className="wb-simbar__transport" role="group" aria-label={t('transport.group')}>
          <Tooltip content={runLabel} shortcut="F5">
            <IconButton label={runLabel} variant={running ? 'default' : 'primary'} className="wb-simbar__run" onClick={guard(controls.toggle)} disabled={!connected} data-testid="run-toggle">
              {running ? <Pause /> : <Play />}
            </IconButton>
          </Tooltip>
          <Tooltip content={t('transport.step')} shortcut="F6">
            <IconButton label={t('transport.step')} onClick={guard(() => controls.step('event'))} disabled={!connected || running}>
              <StepForward />
            </IconButton>
          </Tooltip>
          <Tooltip content={t('transport.advanceBy', { amount: advanceBy })} shortcut="F7">
            <IconButton label={t('transport.advanceBy', { amount: advanceBy })} onClick={guard(() => controls.advance(advanceBy))} disabled={!connected || running || !advanceValid}>
              <SkipForward />
            </IconButton>
          </Tooltip>
          <Input
            aria-label={t('transport.advanceAmount')}
            wrapperClassName="wb-advance"
            mono
            value={advanceBy}
            onChange={(e) => setAdvanceBy(e.target.value)}
            error={advanceValid ? undefined : t('transport.advanceInvalid')}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && advanceValid && !running) guard(() => controls.advance(advanceBy))();
            }}
          />
          <Tooltip content={t('transport.reset.hint')}>
            <IconButton label={t('transport.reset')} onClick={guard(controls.reset)} disabled={!connected}>
              <RotateCcw />
            </IconButton>
          </Tooltip>
        </div>

        <SimulationReadout />

        <div className="wb-simbar__options">
          <SegmentedControl
            label={t('transport.speed')}
            size="sm"
            value={String(speed)}
            options={SPEEDS.map((s) => ({
              value: s.value,
              label: s.speed === 0 ? t('transport.speed.max') : `${t.number(s.speed)}×`,
              title: s.speed === 0 ? t('transport.speed.unbounded') : t('transport.speed.factor', { n: t.number(s.speed) }),
            }))}
            onValueChange={(v) => guard(() => controls.setSpeed(Number(v)))()}
          />
          <Tooltip content={t('transport.policy.hint')}>
            <span className="wb-simbar__policy">
              <Select
                label={t('transport.policy')}
                size="sm"
                value={policy}
                disabled={running || !connected}
                options={[
                  { value: 'functional', label: t('policy.functional') },
                  { value: 'timed', label: t('policy.timed') },
                  { value: 'electrical', label: t('policy.electrical') },
                ]}
                onValueChange={(v) => guard(() => controls.setPolicy(v as Policy))()}
              />
            </span>
          </Tooltip>
        </div>

        {perspective === 'design' && (
          <div className="wb-simbar__edit" role="group" aria-label={t('edit.group')}>
            <Tooltip content={undoLabel ? t('edit.undoWhat', { what: undoLabel }) : t('edit.undo')} shortcut="Ctrl+Z">
              <IconButton label={t('edit.undo')} disabled={!canUndo} onClick={guard(() => stack.undo())}>
                <Undo2 />
              </IconButton>
            </Tooltip>
            <Tooltip content={redoLabel ? t('edit.redoWhat', { what: redoLabel }) : t('edit.redo')} shortcut="Ctrl+Shift+Z">
              <IconButton label={t('edit.redo')} disabled={!canRedo} onClick={guard(() => stack.redo())}>
                <Redo2 />
              </IconButton>
            </Tooltip>
          </div>
        )}
      </div>
    </header>
  );
}
