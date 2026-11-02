// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Coverage and profile (SPEC-008 §5 analysis): the firmware watched
// instruction by instruction from analysis.start; each source file's lines
// and functions hit, the source with each line's count, and where the cycles
// went by function, and how deep each stack went (its own frame and its
// depth with its callees, by function; an overflow past the link's limit),
// and each fault the firmware took, decoded, with its backtrace.
// lcov and collapsed stacks download for genhtml and a flame graph.

import { Badge, Button, EmptyState, Select, Table, useToast, type Column } from '@embsim/design';
import type { CoverageReport, FaultReport, HitTotal, ProfileReport, StackReport } from '@embsim/protocol';
import { Activity, Download, Flame, RefreshCw, Square } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { platform } from '../../platform/platform';
import { useSession } from '../../stores/session';
import { useVerify } from '../../stores/verify';
import { t as tr } from '../../i18n';

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
  const [stack, setStack] = useState<StackReport | null>(null);
  const [faults, setFaults] = useState<FaultReport | null>(null);
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
      // the stacks when they are measured (an older engine has no analysis.stack)
      setStack(await c.analysis.stack({ board: id, detail: false }).then((r) => r.boards?.[id] ?? null, () => null));
      setFaults(await c.analysis.faults({ board: id }).then((r) => r.boards?.[id] ?? null, () => null));
    } catch (e) {
      setOn(false);
      setCov(null);
      setProf(null);
      setStack(null);
      setFaults(null);
      if (!errorMessage(e).includes('not on')) toast({ kind: 'error', title: tr('verify.coverage'), description: errorMessage(e) });
    }
  };
  // the counts as the run stops (a pause, a scenario's end)
  useEffect(() => {
    if (on && !running) void refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [running, id]);

  const start = async () => {
    try {
      await getClient().analysis.start({ stack: true, faults: true });
      setOn(true);
      await refresh();
    } catch (e) {
      toast({ kind: 'error', title: tr('cov.cannotStart'), description: errorMessage(e) });
    }
  };
  const stop = async () => {
    await getClient().analysis.stop({});
    setOn(false);
    setCov(null);
    setProf(null);
    setStack(null);
    setFaults(null);
  };
  const download = async (what: 'lcov' | 'collapsed') => {
    try {
      const c = getClient();
      const text = what === 'lcov' ? (await c.analysis.coverage({ board: id, format: 'lcov' })).text : (await c.analysis.profile({ board: id, format: 'collapsed' })).text;
      const p = await platform();
      await p.saveTextFile(what === 'lcov' ? `${id}.info` : `${id}.folded`, text ?? '', 'text/plain');
    } catch (e) {
      toast({ kind: 'error', title: tr('cov.downloadFailed'), description: errorMessage(e) });
    }
  };

  if (!mcus.length) return <EmptyState title={tr('cov.noFirmware')}>{tr('cov.noBoard')}</EmptyState>;
  const fileColumns: Column<FileRow>[] = [
    { id: 'path', header: tr('se.file'), mono: true, cell: (f) => f.path },
    { id: 'lines', header: tr('cov.lines'), width: '150px', cell: (f) => (
      <span className="es-row">
        <Bar h={f.lines} />
        <span className="wb-cov__num">{pct(f.lines)}</span>
      </span>
    ) },
    { id: 'fns', header: tr('cov.functions'), width: '90px', mono: true, cell: (f) => (Array.isArray(f.functions) ? `${f.functions.filter((x) => x.count > 0).length}/${f.functions.length}` : `${f.functions.hit}/${f.functions.total}`) },
  ];
  type FnRow = ProfileReport['functions'][number];
  const fnColumns: Column<FnRow>[] = [
    { id: 'name', header: tr('cov.function'), mono: true, cell: (f) => f.name },
    { id: 'self', header: tr('cov.self'), width: '80px', mono: true, cell: (f) => (prof?.cycles ? `${((100 * f.self_cycles) / prof.cycles).toFixed(1)}%` : '–') },
    { id: 'incl', header: tr('cov.incl'), width: '80px', mono: true, cell: (f) => (prof?.cycles ? `${((100 * f.incl_cycles) / prof.cycles).toFixed(1)}%` : '–') },
    { id: 'calls', header: tr('cov.calls'), width: '70px', mono: true, cell: (f) => String(f.calls) },
  ];
  const selected = cov?.files.find((f) => f.path === file) ?? null;
  return (
    <div className="wb-verify__pane wb-cov" role="region" aria-label={tr('verify.coverage')}>
      <div className="es-row wb-verify__toolbar">
        {mcus.length > 1 && (
          <Select<string> label={tr('insp.board')} size="sm" value={id} onValueChange={(v) => setBoard(v)} options={mcus.map((b) => ({ value: b.id, label: b.id }))} />
        )}
        <span className="wb-verify__spacer" />
        {!on ? (
          <Button size="sm" variant="primary" icon={<Activity />} disabled={!can} onClick={() => void start()}>
            {tr('cov.measure')}
          </Button>
        ) : (
          <>
            <Button size="sm" icon={<RefreshCw />} onClick={() => void refresh()}>
              {tr('cov.refresh')}
            </Button>
            <Button size="sm" variant="ghost" icon={<Download />} onClick={() => void download('lcov')}>
              lcov
            </Button>
            <Button size="sm" variant="ghost" icon={<Flame />} onClick={() => void download('collapsed')}>
              {tr('cov.flame')}
            </Button>
            <Button size="sm" variant="ghost" icon={<Square />} onClick={() => void stop()}>
              {tr('common.stop')}
            </Button>
          </>
        )}
      </div>
      {!on || !cov ? (
        <div className="es-muted">
          {can
            ? tr('cov.intro')
            : tr('cov.needsAnalysis')}
        </div>
      ) : (
        <>
          <div className="es-row wb-cov__summary">
            <Badge tone="accent">lines {cov.lines.hit}/{cov.lines.total} · {pct(cov.lines)}</Badge>
            <Badge>functions {cov.functions.hit}/{cov.functions.total} · {pct(cov.functions)}</Badge>
            <Badge>{cov.insns.toLocaleString()} instructions</Badge>
          </div>
          <Table label={tr('cov.files')} columns={fileColumns} rows={cov.files} rowKey={(f) => f.path} selectedKey={file} onSelect={(f) => setFile(f.path === file ? null : f.path)} />
          {selected && <SourceCounts board={id} file={selected} />}
          {prof && prof.functions.length > 0 && (
            <>
              <div className="es-row">
                <span className="es-caps">{tr('cov.cycles')}</span>
                <span className="es-muted">
                  {prof.cycles.toLocaleString()} cycles run of {prof.elapsed_cycles.toLocaleString()} elapsed
                </span>
              </div>
              <Table label={tr('cov.bySelf')} columns={fnColumns} rows={prof.functions} rowKey={(f) => f.name} />
            </>
          )}
          {faults && faults.count > 0 && <Faults faults={faults} />}
          {stack && <Stacks stack={stack} />}
        </>
      )}
    </div>
  );
}

/** Each fault the cores took: its summary, and the whole report on demand. */
function Faults({ faults }: { faults: FaultReport }) {
  return (
    <>
      <div className="es-row">
        <span className="es-caps">{tr('cov.faults')}</span>
        <Badge tone="danger">{faults.count}</Badge>
      </div>
      {faults.faults.map((f, i) => (
        <details key={`${i}-${f.pc}`} className="wb-verify__divergence" role="alert">
          <summary>
            <code>{f.summary}</code>
          </summary>
          <pre className="wb-cov__fault">{f.text}</pre>
        </details>
      ))}
    </>
  );
}

function hex(v: number): string {
  return `0x${v.toString(16).padStart(8, '0')}`;
}

/** How deep each stack went, and by function. */
function Stacks({ stack }: { stack: StackReport }) {
  type Row = StackReport['functions'][number];
  const columns: Column<Row>[] = [
    { id: 'name', header: tr('cov.function'), mono: true, cell: (f) => f.name },
    { id: 'deepest', header: tr('cov.deepest'), width: '110px', mono: true, cell: (f) => hex(f.deepest) },
    { id: 'frame', header: tr('cov.frame'), width: '70px', mono: true, cell: (f) => `${f.frame} B` },
    { id: 'incl', header: tr('cov.incl'), width: '80px', mono: true, cell: (f) => `${f.incl} B` },
  ];
  const ovf = stack.overflow;
  return (
    <>
      <div className="es-row">
        <span className="es-caps">{tr('cov.stacks')}</span>
        {stack.stacks.map((s) => (
          <Badge key={`${s.core}-${s.name}`} tone={ovf && ovf.core === s.core && s.name !== 'psp' ? 'danger' : undefined}>
            {stack.stacks.length > 1 && s.name !== 'psp' ? `core${s.core} ` : ''}
            {s.name === 'stack' ? 'stack' : s.name.toUpperCase()}{' '}
            {s.used === undefined ? 'not used' : `${s.used.toLocaleString()}${s.size ? ` / ${s.size.toLocaleString()}` : ''} bytes`}
          </Badge>
        ))}
      </div>
      {ovf && (
        <div className="wb-verify__divergence" role="alert">
          <span className="es-caps">{tr('cov.overflow')}</span>{' '}
          <code>
            sp {hex(ovf.sp)} past {hex(ovf.limit)} ({ovf.limit_from}) in {ovf.function || hex(ovf.pc)}
          </code>
        </div>
      )}
      {stack.functions.length > 0 && <Table label={tr('cov.byStack')} columns={columns} rows={stack.functions} rowKey={(f) => f.name} />}
    </>
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
