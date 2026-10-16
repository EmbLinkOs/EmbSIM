// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Coverage and profile (SPEC-008 §5 analysis): the firmware watched
// instruction by instruction from analysis.start; each source file's lines
// and functions hit, the source with each line's count, and where the cycles
// went by function. lcov and collapsed stacks download for genhtml and a
// flame graph.

import { Badge, Button, EmptyState, Select, Table, useToast, type Column } from '@embsim/design';
import type { CoverageReport, HitTotal, ProfileReport } from '@embsim/protocol';
import { Activity, Download, Flame, RefreshCw, Square } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useSession } from '../../stores/session';
import { useVerify } from '../../stores/verify';

type FileRow = CoverageReport['files'][number];

function pct(h: HitTotal): string {
  return h.total ? `${((100 * h.hit) / h.total).toFixed(1)}%` : '–';
}

function Bar({ h }: { h: HitTotal }) {
  const f = h.total ? h.hit / h.total : 0;
  return (
    <span className="wb-cov__bar" role="img" aria-label={`${pct(h)} covered`}>
      <span className="wb-cov__fill" style={{ width: `${(f * 100).toFixed(1)}%` }} />
    </span>
  );
}

export function CoveragePanel() {
  const boards = useSession((s) => s.boards);
  const capabilities = useSession((s) => s.capabilities);
  const running = useSession((s) => s.running);
  const { toast } = useToast();
  const mcus = useMemo(() => boards.filter((b) => b.runtime && b.runtime !== 'none'), [boards]);
  const [board, setBoard] = useState<string>('');
  const on = useVerify((s) => s.analysisOn);
  const setOn = useVerify((s) => s.setAnalysisOn);
  const [cov, setCov] = useState<CoverageReport | null>(null);
  const [prof, setProf] = useState<ProfileReport | null>(null);
  const [file, setFile] = useState<string | null>(null);
  const can = capabilities.includes('analysis');
  const id = board || mcus[0]?.id || '';

  const refresh = async () => {
    if (!id) return;
    try {
      const c = getClient();
      const [cr, pr] = await Promise.all([c.analysis.coverage({ board: id }), c.analysis.profile({ board: id, detail: false })]);
      setCov(cr.boards?.[id] ?? null);
      setProf(pr.boards?.[id] ?? null);
      setOn(true);
    } catch (e) {
      setOn(false);
      setCov(null);
      setProf(null);
      if (!errorMessage(e).includes('not on')) toast({ kind: 'error', title: 'Coverage', description: errorMessage(e) });
    }
  };
  // the counts as the run stops (a pause, a scenario's end)
  useEffect(() => {
    if (on && !running) void refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [running, id]);

  const start = async () => {
    try {
      await getClient().analysis.start({});
      setOn(true);
      await refresh();
    } catch (e) {
      toast({ kind: 'error', title: 'analysis.start failed', description: errorMessage(e) });
    }
  };
  const stop = async () => {
    await getClient().analysis.stop({});
    setOn(false);
    setCov(null);
    setProf(null);
  };
  const download = async (what: 'lcov' | 'collapsed') => {
    try {
      const c = getClient();
      const text = what === 'lcov' ? (await c.analysis.coverage({ board: id, format: 'lcov' })).text : (await c.analysis.profile({ board: id, format: 'collapsed' })).text;
      const p = await platform();
      await p.saveTextFile(what === 'lcov' ? `${id}.info` : `${id}.folded`, text ?? '', 'text/plain');
    } catch (e) {
      toast({ kind: 'error', title: 'Download failed', description: errorMessage(e) });
    }
  };

  if (!mcus.length) return <EmptyState title="No firmware">The project has no board running firmware to measure.</EmptyState>;
  const fileColumns: Column<FileRow>[] = [
    { id: 'path', header: 'File', mono: true, cell: (f) => f.path },
    { id: 'lines', header: 'Lines', width: '150px', cell: (f) => (
      <span className="es-row">
        <Bar h={f.lines} />
        <span className="wb-cov__num">{pct(f.lines)}</span>
      </span>
    ) },
    { id: 'fns', header: 'Functions', width: '90px', mono: true, cell: (f) => (Array.isArray(f.functions) ? `${f.functions.filter((x) => x.count > 0).length}/${f.functions.length}` : `${f.functions.hit}/${f.functions.total}`) },
  ];
  type FnRow = ProfileReport['functions'][number];
  const fnColumns: Column<FnRow>[] = [
    { id: 'name', header: 'Function', mono: true, cell: (f) => f.name },
    { id: 'self', header: 'Self', width: '80px', mono: true, cell: (f) => (prof?.cycles ? `${((100 * f.self_cycles) / prof.cycles).toFixed(1)}%` : '–') },
    { id: 'incl', header: 'Incl.', width: '80px', mono: true, cell: (f) => (prof?.cycles ? `${((100 * f.incl_cycles) / prof.cycles).toFixed(1)}%` : '–') },
    { id: 'calls', header: 'Calls', width: '70px', mono: true, cell: (f) => String(f.calls) },
  ];
  const selected = cov?.files.find((f) => f.path === file) ?? null;
  return (
    <div className="wb-verify__pane wb-cov" role="region" aria-label="Coverage">
      <div className="es-row wb-verify__toolbar">
        <span className="wb-placeholder__title">Coverage</span>
        {mcus.length > 1 && (
          <Select<string> label="Board" size="sm" value={id} onValueChange={(v) => setBoard(v)} options={mcus.map((b) => ({ value: b.id, label: b.id }))} />
        )}
        <span className="wb-verify__spacer" />
        {!on ? (
          <Button size="sm" variant="primary" icon={<Activity />} disabled={!can} onClick={() => void start()}>
            Measure
          </Button>
        ) : (
          <>
            <Button size="sm" icon={<RefreshCw />} onClick={() => void refresh()}>
              Refresh
            </Button>
            <Button size="sm" variant="ghost" icon={<Download />} onClick={() => void download('lcov')}>
              lcov
            </Button>
            <Button size="sm" variant="ghost" icon={<Flame />} onClick={() => void download('collapsed')}>
              Flame
            </Button>
            <Button size="sm" variant="ghost" icon={<Square />} onClick={() => void stop()}>
              Stop
            </Button>
          </>
        )}
      </div>
      {!on || !cov ? (
        <div className="es-muted">
          {can
            ? 'Measure watches the firmware from now on: run the circuit, or a scenario, then see which lines ran and where the cycles went. The image needs its symbols and line tables (-g).'
            : 'Requires server capability analysis.'}
        </div>
      ) : (
        <>
          <div className="es-row wb-cov__summary">
            <Badge tone="accent">lines {cov.lines.hit}/{cov.lines.total} · {pct(cov.lines)}</Badge>
            <Badge>functions {cov.functions.hit}/{cov.functions.total} · {pct(cov.functions)}</Badge>
            <Badge>{cov.insns.toLocaleString()} instructions</Badge>
          </div>
          <Table label="Files" columns={fileColumns} rows={cov.files} rowKey={(f) => f.path} selectedKey={file} onSelect={(f) => setFile(f.path === file ? null : f.path)} />
          {selected && <SourceCounts board={id} file={selected} />}
          {prof && prof.functions.length > 0 && (
            <>
              <div className="es-row">
                <span className="es-caps">Where the cycles went</span>
                <span className="es-muted">
                  {prof.cycles.toLocaleString()} cycles run of {prof.elapsed_cycles.toLocaleString()} elapsed
                </span>
              </div>
              <Table label="Functions by self cycles" columns={fnColumns} rows={prof.functions} rowKey={(f) => f.name} />
            </>
          )}
        </>
      )}
    </div>
  );
}

/** A file's source with each line's count beside it (debug.source). */
function SourceCounts({ board, file }: { board: string; file: FileRow }) {
  const [text, setText] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    setText(null);
    setError(null);
    getClient()
      .debug.source({ board, file: file.path })
      .then((s) => live && setText(s.text))
      .catch((e: unknown) => live && setError(errorMessage(e)));
    return () => {
      live = false;
    };
  }, [board, file.path]);
  const counts = new Map(file.counts ?? []);
  const first = useRef<HTMLTableRowElement | null>(null);
  const box = useRef<HTMLDivElement | null>(null);
  const firstLine = file.counts?.[0]?.[0];
  // the first line with code at the top, a few lines of context above it
  useEffect(() => {
    if (box.current && first.current) box.current.scrollTop = Math.max(0, first.current.offsetTop - 60);
  }, [text, firstLine]);
  const lines = text !== null ? text.split('\n') : Array.from({ length: Math.max(0, ...counts.keys()) }, () => '');
  return (
    <div className="wb-debug__scroll wb-cov__source" ref={box}>
      <div className="wb-debug__file">
        {file.path}
        {error && <span className="es-muted"> · {error}</span>}
      </div>
      <table className="wb-debug__table" aria-label={`Coverage of ${file.path}`}>
        <tbody>
          {lines.map((t, k) => {
            const n = k + 1;
            const c = counts.get(n);
            return (
              <tr key={n} ref={n === firstLine ? first : undefined} className={c === undefined ? undefined : c > 0 ? 'is-hit' : 'is-miss'}>
                <td className="wb-cov__count">{c === undefined ? '' : c > 0 ? c.toLocaleString() : '0'}</td>
                <td className="wb-debug__addr">{n}</td>
                <td className="wb-debug__text">{t || ' '}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
