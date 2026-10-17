// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { CommandPalette, Dialog, ToastProvider, TooltipProvider, useTheme, useToast } from '@embsim/design';
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
import { Toolbar } from './shell/Toolbar';
import { WorkbenchStatusBar } from './shell/WorkbenchStatusBar';
import { useLayout } from './stores/layout';
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

function Workbench({ connect }: AppProps) {
  const theme = useTheme();
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
        if (s.error) toast({ kind: 'warning', title: 'Using the demo session', description: s.error, duration: 8000 });
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
  if (status === 'error') {
    main = (
      <div className="wb-connecting" role="alert">
        <div className="es-empty__title">Could not connect</div>
        <div>{error}</div>
      </div>
    );
  } else if (status !== 'open') {
    main = (
      <div className="wb-connecting" role="status">
        <div className="es-empty__title">Connecting…</div>
        <div>Opening the session</div>
      </div>
    );
  } else {
    main =
      perspective === 'design' ? <DesignPerspective /> : perspective === 'run' ? <RunPerspective /> : perspective === 'debug' ? <DebugPerspective /> : <VerifyPerspective />;
  }

  return (
    <div className="wb" data-perspective={perspective}>
      <Toolbar theme={theme} commands={commands} />
      <NewProjectDialog />
      <main className="wb__main" aria-label={`${perspective} perspective`}>
        {main}
      </main>
      <WorkbenchStatusBar />
      <CommandPalette open={paletteOpen} onOpenChange={setPaletteOpen} commands={commands} />
      <Dialog open={diagnosticsOpen} onOpenChange={setDiagnosticsOpen} title="Diagnostics" className="wb-diagnostics-dialog">
        <DiagnosticsPanel />
      </Dialog>
    </div>
  );
}
