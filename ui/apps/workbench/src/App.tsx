// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { CommandPalette, Dialog, ToastProvider, TooltipProvider, resolveTheme, useToast, type ThemePreference } from '@embsim/design';
import { useEffect, useState } from 'react';
import { connectSession, tryClient, type ConnectOptions } from './connection';
import { Gallery } from './gallery/Gallery';
import { DebugPerspective } from './perspectives/debug/DebugPerspective';
import { DesignPerspective } from './perspectives/design/DesignPerspective';
import { RunPerspective } from './perspectives/run/RunPerspective';
import { VerifyPerspective } from './perspectives/verify/VerifyPerspective';
import { useCommands, useGlobalShortcuts } from './shell/commands';
import { NewProjectDialog } from './shell/NewProjectDialog';
import { DiagnosticsPanel } from './shell/DiagnosticsPanel';
import { FloatingPanels } from './shell/FloatingPanels';
import { Toolbar } from './shell/Toolbar';
import { WorkbenchStatusBar } from './shell/WorkbenchStatusBar';
import { t as tr, type MessageKey } from './i18n';
import { ConnectionScreen } from './shell/ConnectionScreen';
import { SettingsPage } from './shell/SettingsPage';
import { useLayout } from './stores/layout';
import { useSettings } from './stores/settings';
import { useSession } from './stores/session';

export function isGalleryRoute(loc: Pick<Location, 'pathname' | 'hash'> = window.location): boolean {
  return loc.hash === '#/gallery' || /\/gallery\/?$/.test(loc.pathname);
}

export interface AppProps {
  connect?: ConnectOptions;
}

export function App({ connect }: AppProps) {
  const [gallery, setGallery] = useState(() => isGalleryRoute());
  useEffect(() => {
    const onChange = () => setGallery(isGalleryRoute());
    window.addEventListener('hashchange', onChange);
    window.addEventListener('popstate', onChange);
    return () => {
      window.removeEventListener('hashchange', onChange);
      window.removeEventListener('popstate', onChange);
    };
  }, []);
  return (
    <TooltipProvider>
      <ToastProvider>{gallery ? <Gallery /> : <Workbench connect={connect} />}</ToastProvider>
    </TooltipProvider>
  );
}

/** The theme as the Settings page keeps it (the same object the palette's theme commands use). */
function useSettingsTheme() {
  const preference = useSettings((s) => s.theme);
  const set = useSettings((s) => s.set);
  const [resolved, setResolved] = useState(() => resolveTheme(preference));
  useEffect(() => {
    setResolved(resolveTheme(preference));
    if (preference !== 'system' || typeof window.matchMedia !== 'function') return;
    const mq = window.matchMedia('(prefers-color-scheme: light)');
    const onChange = () => setResolved(resolveTheme('system'));
    mq.addEventListener?.('change', onChange);
    return () => mq.removeEventListener?.('change', onChange);
  }, [preference]);
  return {
    preference,
    resolved,
    setPreference: (p: ThemePreference) => set('theme', p),
    toggle: () => set('theme', resolved === 'dark' ? 'light' : 'dark'),
  };
}

function Workbench({ connect }: AppProps) {
  const theme = useSettingsTheme();
  const settingsOpen = useSettings((s) => s.settingsOpen);
  const layoutEpoch = useSettings((s) => s.layoutEpoch);
  useSettings((s) => s.language); // re-render the shell when the language changes
  const status = useSession((s) => s.status);
  const error = useSession((s) => s.error);
  const perspective = useLayout((s) => s.perspective);
  const paletteOpen = useLayout((s) => s.paletteOpen);
  const setPaletteOpen = useLayout((s) => s.setPaletteOpen);
  const diagnosticsOpen = useLayout((s) => s.diagnosticsOpen);
  const setDiagnosticsOpen = useLayout((s) => s.setDiagnosticsOpen);
  const { toast } = useToast();
  const commands = useCommands(theme);
  useGlobalShortcuts(commands);

  useEffect(() => {
    if (tryClient()) return;
    connectSession(connect ?? {})
      .then(() => {
        const s = useSession.getState();
        if (s.error) toast({ kind: 'warning', title: tr('toast.demo'), description: s.error, duration: 8000 });
      })
      .catch((e: unknown) => {
        useSession.getState().setConnection({ status: 'error', error: e instanceof Error ? e.message : String(e) });
      });
    // Connect once per page.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Toast new diagnostics as they arrive (the panel keeps the full list).
  useEffect(() => {
    let seen = useSession.getState().diagnostics.length;
    return useSession.subscribe((s) => {
      if (s.diagnostics.length > seen) {
        for (const r of s.diagnostics.slice(seen)) {
          toast({ kind: r.severity === 'warning' ? 'warning' : r.severity === 'info' ? 'info' : 'error', title: r.code, description: r.message, duration: 6000 });
        }
      }
      seen = s.diagnostics.length;
    });
  }, [toast]);

  let main: React.ReactNode;
  if (settingsOpen) {
    main = <SettingsPage />;
  } else if (status !== 'open') {
    main = <ConnectionScreen />;
  } else {
    main =
      perspective === 'design' ? <DesignPerspective key={layoutEpoch} /> : perspective === 'run' ? <RunPerspective key={layoutEpoch} /> : perspective === 'debug' ? <DebugPerspective key={layoutEpoch} /> : <VerifyPerspective key={layoutEpoch} />;
  }
  void error;

  return (
    <div className="wb" data-perspective={perspective}>
      <Toolbar theme={theme} commands={commands} />
      <NewProjectDialog />
      <main className="wb__main" aria-label={settingsOpen ? tr('settings.title') : tr('perspective.region', { name: tr(`perspective.${perspective}` as MessageKey) })}>
        {main}
      </main>
      {status === 'open' && !settingsOpen && <FloatingPanels key={layoutEpoch} />}
      <WorkbenchStatusBar />
      <CommandPalette open={paletteOpen} onOpenChange={setPaletteOpen} commands={commands} />
      <Dialog open={diagnosticsOpen} onOpenChange={setDiagnosticsOpen} title={tr('diag.title')} className="wb-diagnostics-dialog">
        <DiagnosticsPanel />
      </Dialog>
    </div>
  );
}
