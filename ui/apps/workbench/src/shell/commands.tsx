// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The command registry: every action reachable from the palette (Ctrl/Cmd-K)
// and the global keyboard shortcuts. Perspective-specific commands register
// through `useRegisterCommands`.

import { useToast, type Command, type ThemePreference } from '@embsim/design';
import { JsonRpcError, formatTime, parseDuration } from '@embsim/protocol';
import { useCommandStack } from '@embsim/scene';
import {
  Activity,
  Bug,
  CheckSquare,
  FilePlus,
  FlaskConical,
  FolderOpen,
  LayoutPanelLeft,
  Settings as SettingsIcon,
  Moon,
  Pause,
  PenTool,
  Play,
  Redo2,
  RotateCcw,
  Save,
  SkipForward,
  StepForward,
  Sun,
  Undo2,
  Zap,
} from 'lucide-react';
import { useEffect, useMemo } from 'react';
import { t as tr, type MessageKey } from '../i18n';
import { useSettings } from '../stores/settings';
import { create } from 'zustand';
import { controls, errorMessage } from '../actions';
import { getClient, mockDev } from '../connection';
import { platform } from '../platform/platform';
import { useLayout, type Perspective } from '../stores/layout';
import { useProject } from '../stores/project';
import { useSession } from '../stores/session';
import { useTransportPreferences } from '../stores/transportPreferences';

interface RegistryState {
  extra: Record<string, Command[]>;
  register(owner: string, commands: Command[]): void;
  unregister(owner: string): void;
}

const useRegistry = create<RegistryState>((set) => ({
  extra: {},
  register: (owner, commands) => set((s) => ({ extra: { ...s.extra, [owner]: commands } })),
  unregister: (owner) =>
    set((s) => {
      const extra = { ...s.extra };
      delete extra[owner];
      return { extra };
    }),
}));

/** Registers commands for the lifetime of the calling component. */
export function useRegisterCommands(owner: string, commands: Command[]): void {
  const register = useRegistry((s) => s.register);
  const unregister = useRegistry((s) => s.unregister);
  useEffect(() => {
    register(owner, commands);
    return () => unregister(owner);
  }, [owner, commands, register, unregister]);
}

const PERSPECTIVE_META: Record<Perspective, { label: string; icon: React.ReactNode; shortcut: string }> = {
  design: { label: 'Design', icon: <PenTool />, shortcut: 'Ctrl+1' },
  run: { label: 'Run', icon: <Activity />, shortcut: 'Ctrl+2' },
  debug: { label: 'Debug', icon: <Bug />, shortcut: 'Ctrl+3' },
  verify: { label: 'Verify', icon: <CheckSquare />, shortcut: 'Ctrl+4' },
};

export function perspectiveMeta(p: Perspective) {
  return { ...PERSPECTIVE_META[p], label: tr(`perspective.${p}` as MessageKey) };
}

/** All commands, global ones plus the registered extras. */
export function useCommands(theme: { resolved: 'dark' | 'light'; setPreference: (p: ThemePreference) => void }): Command[] {
  const extra = useRegistry((s) => s.extra);
  const running = useSession((s) => s.running);
  const advanceBy = useTransportPreferences((s) => s.advanceBy);
  const isDemo = useSession((s) => s.isDemo);
  const perspective = useLayout((s) => s.perspective);
  const setPerspective = useLayout((s) => s.setPerspective);
  const commandsStack = useProject((s) => s.commands);
  const { canUndo, canRedo, undoLabel, redoLabel } = useCommandStack(commandsStack);
  const { toast } = useToast();
  const lang = useSettings((s) => s.language);

  return useMemo(() => {
    const guard = (fn: () => Promise<void>) => () => {
      fn().catch((e: unknown) => toast({ kind: 'error', title: tr('toast.commandFailed'), description: errorMessage(e) }));
    };
    /** Downloads the project's JSON as it now stands. */
    const saveCopy = async () => {
      const project = useProject.getState().project;
      if (!project) return;
      const p = await platform();
      const where = await p.saveTextFile(`${project.name}.json`, `${JSON.stringify(project, null, 2)}\n`, 'application/json');
      if (where) toast({ kind: 'success', title: tr('toast.saved', { where }) });
    };
    const list: Command[] = [
      { id: 'control.run', label: running ? tr('transport.pause') : tr('transport.run'), group: tr('cmd.group.control'), icon: running ? <Pause /> : <Play />, shortcut: 'F5', keywords: ['start', 'stop'], run: guard(controls.toggle) },
      { id: 'control.step', label: tr('transport.step'), group: tr('cmd.group.control'), icon: <StepForward />, shortcut: 'F6', run: guard(() => controls.step('event')) },
      { id: 'control.advance', label: tr('transport.advanceBy', { amount: advanceBy }), group: tr('cmd.group.control'), icon: <SkipForward />, shortcut: 'F7', disabled: (() => { try { return running || parseDuration(advanceBy) <= 0n; } catch { return true; } })(), run: guard(() => controls.advance(advanceBy)) },
      { id: 'control.advance10', label: tr('cmd.advance10'), group: tr('cmd.group.control'), icon: <SkipForward />, run: guard(() => controls.advance('10ms')) },
      { id: 'control.reset', label: tr('cmd.resetSession'), group: tr('cmd.group.control'), icon: <RotateCcw />, run: guard(controls.reset) },
      ...(['design', 'run', 'debug', 'verify'] as Perspective[]).map((p) => ({
        id: `perspective.${p}`,
        label: tr('perspective.switchTo', { name: tr(`perspective.${p}` as MessageKey) }),
        group: tr('cmd.group.perspective'),
        icon: PERSPECTIVE_META[p].icon,
        shortcut: PERSPECTIVE_META[p].shortcut,
        disabled: perspective === p,
        run: () => setPerspective(p),
      })),
      {
        id: 'edit.undo',
        label: undoLabel ? tr('edit.undoWhat', { what: undoLabel }) : tr('edit.undo'),
        group: tr('cmd.group.edit'),
        icon: <Undo2 />,
        shortcut: 'Ctrl+Z',
        disabled: !canUndo,
        run: guard(() => commandsStack.undo()),
      },
      {
        id: 'edit.redo',
        label: redoLabel ? tr('edit.redoWhat', { what: redoLabel }) : tr('edit.redo'),
        group: tr('cmd.group.edit'),
        icon: <Redo2 />,
        shortcut: 'Ctrl+Shift+Z',
        disabled: !canRedo,
        run: guard(() => commandsStack.redo()),
      },
      {
        id: 'view.theme.dark',
        label: tr('cmd.themeDark'),
        group: tr('cmd.group.view'),
        icon: <Moon />,
        disabled: theme.resolved === 'dark',
        run: () => theme.setPreference('dark'),
      },
      {
        id: 'view.theme.light',
        label: tr('cmd.themeLight'),
        group: tr('cmd.group.view'),
        icon: <Sun />,
        disabled: theme.resolved === 'light',
        run: () => theme.setPreference('light'),
      },
      { id: 'view.theme.system', label: tr('cmd.themeSystem'), group: tr('cmd.group.view'), icon: <Sun />, run: () => theme.setPreference('system') },
      { id: 'view.gallery', label: tr('cmd.gallery'), group: tr('cmd.group.view'), icon: <FlaskConical />, run: () => window.open('#/gallery', '_blank') },
      { id: 'view.settings', label: tr('cmd.settings'), group: tr('cmd.group.view'), icon: <SettingsIcon />, shortcut: 'Ctrl+,', keywords: ['preferences', 'language', 'langue', 'paramètres'], run: () => useSettings.getState().setSettingsOpen(true) },
      { id: 'view.reset-layout', label: tr('cmd.resetLayout'), group: tr('cmd.group.view'), icon: <LayoutPanelLeft />, run: () => { useSettings.getState().resetLayout(); toast({ kind: 'success', title: tr('toast.layoutReset') }); } },
      {
        id: 'project.new',
        label: tr('cmd.newProject'),
        group: tr('cmd.group.project'),
        icon: <FilePlus />,
        keywords: ['blank', 'board', 'start'],
        run: () => useLayout.getState().setNewProjectOpen(true),
      },
      {
        id: 'project.open',
        label: tr('cmd.openProject'),
        group: tr('cmd.group.project'),
        icon: <FolderOpen />,
        shortcut: 'Ctrl+O',
        run: guard(async () => {
          const p = await platform();
          const file = await p.openProjectFile();
          if (!file) return;
          const json = JSON.parse(file.text);
          await useProject.getState().loadProjectJson(json);
          toast({ kind: 'success', title: tr('toast.loaded', { name: file.name }) });
        }),
      },
      {
        id: 'project.save',
        label: tr('cmd.saveProject'),
        group: tr('cmd.group.project'),
        icon: <Save />,
        shortcut: 'Ctrl+S',
        run: guard(async () => {
          // the engine writes the file it loaded; a session without a file
          // (the demo, a project opened from the browser) downloads a copy
          try {
            const { path } = await getClient().project.save({});
            toast({ kind: 'success', title: tr('toast.saved', { where: path }) });
          } catch (e) {
            if (!(e instanceof JsonRpcError)) throw e;
            await saveCopy();
          }
        }),
      },
      {
        id: 'project.save-as',
        label: tr('cmd.saveCopy'),
        group: tr('cmd.group.project'),
        icon: <Save />,
        run: guard(saveCopy),
      },
      {
        id: 'project.copy-time',
        label: tr('cmd.copyTime'),
        group: tr('cmd.group.project'),
        run: guard(async () => {
          const now = useSession.getState().now;
          await (await platform()).writeClipboard(now.toString());
          toast({ title: tr('toast.copiedTime', { time: formatTime(now), ps: now.toString() }) });
        }),
      },
    ];
    if (isDemo) {
      list.push({
        id: 'dev.contention',
        label: tr('cmd.demoContention'),
        group: tr('cmd.group.developer'),
        icon: <Zap />,
        keywords: ['mock', 'diagnostics', 'warning'],
        run: () => mockDev()?.raiseContention(),
      });
    }
    for (const owner of Object.keys(extra).sort()) list.push(...(extra[owner] ?? []));
    return list;
    // lang: the labels are translated with tr(), which reads the language from the store
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [running, advanceBy, isDemo, perspective, setPerspective, canUndo, canRedo, undoLabel, redoLabel, commandsStack, theme, toast, extra, lang]);
}

function isEditable(el: Element | null): boolean {
  if (!el) return false;
  const tag = el.tagName;
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || (el as HTMLElement).isContentEditable;
}

/** Global shortcuts: Ctrl/Cmd-K palette, F5/F6/F7 transport, Ctrl+1..4 perspectives, Ctrl+Z/Y undo. */
export function useGlobalShortcuts(commands: Command[]): void {
  const setPaletteOpen = useLayout((s) => s.setPaletteOpen);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const mod = e.ctrlKey || e.metaKey;
      // an undo or redo goes to the stack whatever the last render said: one
      // pressed while an edit is still finishing waits for it in the stack's
      // queue instead of being lost, and one with nothing to undo does nothing
      const run = (id: string, queued = false) => {
        const c = commands.find((x) => x.id === id);
        if (c && (!c.disabled || queued)) {
          e.preventDefault();
          c.run();
        }
      };
      if (mod && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        setPaletteOpen(true);
        return;
      }
      if (e.key === 'F5') return run('control.run');
      if (e.key === 'F6') return run('control.step');
      if (e.key === 'F7') return run('control.advance');
      if (mod && !e.shiftKey && e.key === 'z' && !isEditable(document.activeElement)) return run('edit.undo', true);
      if (mod && (e.key === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z')) && !isEditable(document.activeElement)) return run('edit.redo', true);
      // copying text the user selected stays the browser's
      if (mod && !isEditable(document.activeElement) && e.key.toLowerCase() === 'c' && !e.shiftKey && !window.getSelection()?.toString()) return run('canvas.copy');
      if (mod && !isEditable(document.activeElement) && e.key.toLowerCase() === 'v' && !e.shiftKey) return run('canvas.paste');
      if (mod && !isEditable(document.activeElement) && e.key.toLowerCase() === 'd') return run('canvas.duplicate');
      if (mod && e.key === ',') {
        e.preventDefault();
        const s = useSettings.getState();
        s.setSettingsOpen(!s.settingsOpen);
        return;
      }
      if (mod && e.key === 'o') return run('project.open');
      if (mod && e.key === 's') return run('project.save');
      if (mod && ['1', '2', '3', '4'].includes(e.key)) return run(`perspective.${(['design', 'run', 'debug', 'verify'] as const)[Number(e.key) - 1]}`);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [commands, setPaletteOpen]);
}
