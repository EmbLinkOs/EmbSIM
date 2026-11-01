// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The scenario editor (SPEC-011 Verify, SPEC-007 §2): a scenario of the
// project opened from its file (scenario.get), or a new one, or one made from
// a recording; its inputs timeline and its assertions edited in place, run
// against the project (scenario.run with the scenario itself: the session is
// reset, the inputs applied, the assertions checked) and saved back
// (scenario.save, listed in the project's scenarios).

import { Badge, Button, EmptyState, Input, Select, useToast } from '@embsim/design';
import { formatTime, parseDuration, type ProjectProbe, type Report, type Scenario, type ScenarioAssertion, type ScenarioInput } from '@embsim/protocol';
import { FilePlus, FolderOpen, Play, Plus, Save, Trash2 } from 'lucide-react';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { expectationProblem, scenarioProblems } from '../../stores/scenarioValidation';
import { emptyScenario, formatValue, parseValue, useVerify, whenOf, withWhen, type AssertionWhen } from '../../stores/verify';
import { t as tr } from '../../i18n';

// a project without scenarios or probes: the same empty list each render (a
// new one from a store selector renders again, without end)
const NO_SCENARIOS: string[] = [];
const NO_PROBES: ProjectProbe[] = [];

function timeError(t: string | undefined): string | undefined {
  if (t === undefined || t === '') return 'a time with a unit: 10ms';
  try {
    parseDuration(t);
    return undefined;
  } catch (e) {
    return errorMessage(e);
  }
}

export function ScenarioEditor() {
  const scenarios = useProject((s) => s.project?.scenarios ?? NO_SCENARIOS);
  const capabilities = useSession((s) => s.capabilities);
  const running = useSession((s) => s.running);
  const draft = useVerify((s) => s.draft);
  const { toast } = useToast();
  const [busy, setBusy] = useState<string | null>(null);
  const canEdit = capabilities.includes('scenario.edit');
  const canRun = capabilities.includes('scenario');

  const open = async (path: string) => {
    setBusy('open');
    try {
      const r = await getClient().scenario.get({ path });
      useVerify.getState().openDraft(path, r.scenario);
    } catch (e) {
      toast({ kind: 'error', title: `Cannot open ${path}`, description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };

  if (!draft) {
    return (
      <div className="wb-verify__pane" role="region" aria-label={tr('verify.editor')}>
        <div className="es-row">
          {!canEdit && <Badge tone="warning">{canRun ? tr('se.runOnly') : tr('ph.notAvailable')}</Badge>}
        </div>
        <div className="es-muted">
          {tr('se.intro')}
        </div>
        {scenarios.length === 0 ? (
          <div className="es-muted">{tr('se.none')}</div>
        ) : (
          <ul className="wb-verify__list">
            {scenarios.map((s) => (
              <li key={s} className="es-row">
                <code>{s}</code>
                <Button size="sm" icon={<FolderOpen />} disabled={!canEdit || busy !== null} onClick={() => void open(s)}>
                  {tr('common.open')}
                </Button>
                <QuickRun path={s} disabled={!canRun || running || busy !== null} />
              </li>
            ))}
          </ul>
        )}
        <div className="es-row">
          <Button size="sm" icon={<FilePlus />} disabled={!canEdit} onClick={() => useVerify.getState().openDraft(null, emptyScenario())}>
            {tr('se.new')}
          </Button>
        </div>
      </div>
    );
  }
  return <DraftEditor canRun={canRun && !running} />;
}

/** Runs a scenario file as it is, without opening it. */
function QuickRun({ path, disabled }: { path: string; disabled: boolean }) {
  const { toast } = useToast();
  const [busy, setBusy] = useState(false);
  const run = async () => {
    setBusy(true);
    try {
      const r = await getClient().scenario.run({ path });
      const report = ('report' in r ? r.report : r) as Report;
      useVerify.getState().setReport(report);
      notify(toast, report, path);
    } catch (e) {
      toast({ kind: 'error', title: tr('se.failed'), description: errorMessage(e) });
    } finally {
      setBusy(false);
    }
  };
  return (
    <Button size="sm" icon={<Play />} disabled={disabled || busy} onClick={() => void run()}>
      {busy ? 'Running…' : 'Run'}
    </Button>
  );
}

function notify(toast: ReturnType<typeof useToast>['toast'], report: Report, what: string) {
  const failed = report.assertions.filter((a) => !a.ok).length;
  toast({
    kind: failed ? 'error' : 'success',
    title: failed ? `${failed} assertion(s) failed` : `${report.assertions.length} assertions passed`,
    description: `${what} · ended ${report.ended.reason} at ${formatTime(BigInt(report.ended.now_ps), { padZeros: false })}`,
  });
}

function DraftEditor({ canRun }: { canRun: boolean }) {
  const draft = useVerify((s) => s.draft)!;
  const edit = useVerify((s) => s.editDraft);
  const { toast } = useToast();
  const [busy, setBusy] = useState<string | null>(null);
  const [savePath, setSavePath] = useState(draft.path ?? `scenarios/${draft.scenario.name || 'scenario'}.json`);
  const [expectDraft, setExpectDraft] = useState<Record<number, string>>({});
  const sc = draft.scenario;
  const problems = scenarioProblems(sc);
  for (const [index, text] of Object.entries(expectDraft)) {
    try {
      const error = expectationProblem(JSON.parse(text));
      if (error) problems.push(`Assertion ${Number(index) + 1}: ${error}`);
    } catch {
      problems.push(`Assertion ${Number(index) + 1}: expectation must be valid JSON.`);
    }
  }
  const ids = sc.assertions.map((a) => a.id);
  const duplicate = ids.find((id, k) => ids.indexOf(id) !== k);

  const run = async () => {
    if (problems.length) return;
    setBusy('run');
    try {
      const r = await getClient().scenario.run({ scenario: sc });
      const report = ('report' in r ? r.report : r) as Report;
      useVerify.getState().setReport(report);
      notify(toast, report, sc.name);
    } catch (e) {
      toast({ kind: 'error', title: tr('se.failed'), description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };
  const save = async () => {
    if (problems.length) return;
    const path = draft.path ?? savePath.trim();
    setBusy('save');
    try {
      await getClient().scenario.save({ path, scenario: sc, add: true });
      useVerify.getState().markSaved(path);
      await useProject.getState().loadAll();
      toast({ kind: 'success', title: `Saved ${path}`, description: draft.path ? undefined : 'Added to the project\'s scenarios: save the project to keep the list.' });
    } catch (e) {
      toast({ kind: 'error', title: tr('se.cannotSave'), description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };

  return (
    <div className="wb-verify__pane wb-verify__editor" role="region" aria-label={tr('verify.editor')}>
      <div className="es-row wb-verify__toolbar">
        <span className="wb-placeholder__title">{draft.path ?? tr('se.new')}</span>
        {draft.dirty && <Badge tone="warning">{tr('se.unsaved')}</Badge>}
        <span className="wb-verify__spacer" />
        <Button size="sm" variant="primary" icon={<Play />} disabled={!canRun || busy !== null || problems.length > 0} onClick={() => void run()}>
          {busy === 'run' ? 'Running…' : 'Run'}
        </Button>
        <Button size="sm" icon={<Save />} disabled={busy !== null || problems.length > 0 || !!duplicate || (!draft.path && !savePath.trim())} onClick={() => void save()}>
          {tr('common.save')}
        </Button>
        <Button size="sm" variant="ghost" onClick={() => useVerify.getState().closeDraft()}>
          {tr('common.close')}
        </Button>
      </div>
      <div className="wb-verify__fields">
        <Input label={tr('insp.name')} value={sc.name} onChange={(e) => edit((s) => ({ ...s, name: e.target.value }))} />
        <Input label={tr('se.until')} mono value={sc.until ?? ''} error={sc.until ? timeError(sc.until) : undefined} onChange={(e) => edit((s) => ({ ...s, until: e.target.value }))} />
        <Select<string>
          label={tr('se.policy')}
          size="sm"
          value={sc.policy ?? 'project'}
          onValueChange={(v) => edit((s) => ({ ...s, policy: v === 'project' ? undefined : (v as Scenario['policy']) }))}
          options={[
            { value: 'project', label: 'the project\'s' },
            { value: 'electrical', label: 'electrical' },
            { value: 'timed', label: 'timed' },
            { value: 'functional', label: 'functional' },
          ]}
        />
        {!draft.path && <Input label={tr('se.file')} mono value={savePath} onChange={(e) => setSavePath(e.target.value)} hint="relative to the project's folder" />}
      </div>
      {problems.length > 0 && (
        <div role="alert" className="wb-verify__validation">
          <strong>{tr('se.fixFirst')}</strong>
          <ul>{problems.map((problem, index) => <li key={index}>{problem}</li>)}</ul>
        </div>
      )}
      <InputsTable inputs={sc.inputs} onChange={(inputs) => edit((s) => ({ ...s, inputs }))} />
      <AssertionsTable expectDraft={expectDraft} setExpectDraft={setExpectDraft} assertions={sc.assertions} duplicate={duplicate} onChange={(assertions) => edit((s) => ({ ...s, assertions }))} />
    </div>
  );
}

function InputsTable({ inputs, onChange }: { inputs: ScenarioInput[]; onChange: (inputs: ScenarioInput[]) => void }) {
  const known = useSession((s) => s.inputs);
  const targets = Array.from(new Set(known.map((i) => i.target)));
  const set = (k: number, patch: Partial<ScenarioInput>) => onChange(inputs.map((x, j) => (j === k ? { ...x, ...patch } : x)));
  const sortByTime = () =>
    onChange(
      [...inputs].sort((a, b) => {
        try {
          const d = parseDuration(a.at) - parseDuration(b.at);
          return d < 0n ? -1 : d > 0n ? 1 : 0;
        } catch {
          return 0;
        }
      }),
    );
  const add = () => {
    const first = known[0];
    onChange([...inputs, { at: inputs.length ? inputs[inputs.length - 1]!.at : '10ms', target: first?.target ?? '', input: first?.input ?? '', value: first?.type === 'boolean' ? true : '' }]);
  };
  return (
    <section className="wb-verify__section" aria-label={tr('run.group.inputs')}>
      <div className="es-row">
        <span className="es-caps">{tr('run.group.inputs')}</span>
        <span className="es-muted">{inputs.length}</span>
        <span className="wb-verify__spacer" />
        <Button size="sm" variant="ghost" disabled={inputs.length < 2} onClick={sortByTime}>
          {tr('se.sort')}
        </Button>
        <Button size="sm" icon={<Plus />} onClick={add}>
          {tr('se.addInput')}
        </Button>
      </div>
      <datalist id="wb-verify-targets">
        {targets.map((t) => (
          <option key={t} value={t} />
        ))}
      </datalist>
      {inputs.length === 0 ? (
        <div className="es-muted">{tr('se.noInputs')}</div>
      ) : (
        <table className="wb-verify__table">
          <thead>
            <tr>
              <th>At</th>
              <th>{tr('se.target')}</th>
              <th>{tr('se.input')}</th>
              <th>{tr('se.value')}</th>
              <th className="wb-verify__narrow" aria-label={tr('se.remove')} />
            </tr>
          </thead>
          <tbody>
            {inputs.map((x, k) => {
              const names = known.filter((i) => i.target === x.target).map((i) => i.input);
              return (
                <tr key={k}>
                  <td>
                    <Input aria-label={`Input ${k + 1} time`} mono value={x.at} error={timeError(x.at)} onChange={(e) => set(k, { at: e.target.value })} />
                  </td>
                  <td>
                    <Input aria-label={`Input ${k + 1} target`} mono list="wb-verify-targets" value={x.target} onChange={(e) => set(k, { target: e.target.value })} />
                  </td>
                  <td>
                    <Input aria-label={`Input ${k + 1} input`} mono list={`wb-verify-inputs-${k}`} value={x.input} onChange={(e) => set(k, { input: e.target.value })} />
                    <datalist id={`wb-verify-inputs-${k}`}>
                      {names.map((n) => (
                        <option key={n} value={n} />
                      ))}
                    </datalist>
                  </td>
                  <td>
                    <Input aria-label={`Input ${k + 1} value`} mono value={formatValue(x.value)} onChange={(e) => set(k, { value: parseValue(e.target.value) })} />
                  </td>
                  <td>
                    <Button size="sm" variant="ghost" icon={<Trash2 />} aria-label={`Remove input ${k + 1}`} onClick={() => onChange(inputs.filter((_, j) => j !== k))} />
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      )}
    </section>
  );
}

function expectText(a: ScenarioAssertion): string {
  return a.expect ? JSON.stringify(a.expect) : '';
}

function AssertionsTable({ assertions, duplicate, onChange, expectDraft, setExpectDraft }: { expectDraft: Record<number, string>; setExpectDraft: React.Dispatch<React.SetStateAction<Record<number, string>>>; assertions: ScenarioAssertion[]; duplicate?: string; onChange: (a: ScenarioAssertion[]) => void }) {
  const probes = useProject((s) => s.project?.probes ?? NO_PROBES);
  const results = useVerify((s) => s.results);
  const set = (k: number, a: ScenarioAssertion) => onChange(assertions.map((x, j) => (j === k ? a : x)));
  const add = () => {
    let n = assertions.length + 1;
    while (assertions.some((a) => a.id === `check-${n}`)) n++;
    onChange([...assertions, { id: `check-${n}`, at: '10ms', probe: probes[0]?.id ?? '', expect: { logic: '1' } }]);
  };
  return (
    <section className="wb-verify__section" aria-label={tr('verify.assertions')}>
      <div className="es-row">
        <span className="es-caps">{tr('verify.assertions')}</span>
        <span className="es-muted">{assertions.length}</span>
        {duplicate && <Badge tone="danger">two assertions are named {duplicate}</Badge>}
        <span className="wb-verify__spacer" />
        <Button size="sm" icon={<Plus />} onClick={add}>
          {tr('se.addAssertion')}
        </Button>
      </div>
      <datalist id="wb-verify-probes">
        {probes.map((p) => (
          <option key={p.id} value={p.id} />
        ))}
      </datalist>
      {assertions.length === 0 ? (
        <div className="es-muted">{tr('se.noAssertions')}</div>
      ) : (
        <table className="wb-verify__table">
          <thead>
            <tr>
              <th className="wb-verify__result">{tr('se.result')}</th>
              <th>Id</th>
              <th className="wb-verify__when">{tr('se.when')}</th>
              <th style={{ width: '26%' }}>{tr('se.time')}</th>
              <th>{tr('se.probe')}</th>
              <th style={{ width: '24%' }}>{tr('se.expect')}</th>
              <th className="wb-verify__narrow" aria-label={tr('se.remove')} />
            </tr>
          </thead>
          <tbody>
            {assertions.map((a, k) => {
              const when = whenOf(a);
              const r = results[a.id];
              const text = expectDraft[k] ?? (when === 'always' ? (a.diagnostics?.forbid ?? []).join(', ') : expectText(a));
              let expectError: string | undefined;
              if (when !== 'always' && expectDraft[k] !== undefined) {
                try {
                  const v = JSON.parse(expectDraft[k]!) as unknown;
                  expectError = expectationProblem(v);
                } catch {
                  expectError = 'JSON: {"min_v": 3.0}';
                }
              }
              return (
                <tr key={k}>
                  <td>{r ? <Badge tone={r.ok ? 'accent' : 'danger'}>{r.ok ? 'pass' : `fail · ${JSON.stringify(r.observed)}`}</Badge> : <span className="es-muted">–</span>}</td>
                  <td>
                    <Input aria-label={`Assertion ${k + 1} id`} mono value={a.id} onChange={(e) => set(k, { ...a, id: e.target.value })} />
                  </td>
                  <td>
                    <Select<AssertionWhen>
                      label={`Assertion ${k + 1} timing`}
                      size="sm"
                      value={when}
                      onValueChange={(w) => { setExpectDraft((d) => { const next = { ...d }; delete next[k]; return next; }); set(k, withWhen(a, w)); }}
                      options={[
                        { value: 'at', label: tr('se.at') },
                        { value: 'between', label: tr('se.between') },
                        { value: 'by', label: tr('se.by') },
                        { value: 'always', label: tr('se.always') },
                      ]}
                    />
                  </td>
                  <td>
                    {when === 'at' && <Input aria-label={`Assertion ${k + 1} time`} mono value={a.at ?? ''} error={timeError(a.at)} onChange={(e) => set(k, { ...a, at: e.target.value })} />}
                    {when === 'by' && <Input aria-label={`Assertion ${k + 1} time`} mono value={a.by ?? ''} error={timeError(a.by)} onChange={(e) => set(k, { ...a, by: e.target.value })} />}
                    {when === 'between' && (
                      <span className="es-row">
                        <Input aria-label={`Assertion ${k + 1} from`} mono value={a.between?.[0] ?? ''} error={timeError(a.between?.[0])} onChange={(e) => set(k, { ...a, between: [e.target.value, a.between?.[1] ?? ''] })} />
                        <Input aria-label={`Assertion ${k + 1} to`} mono value={a.between?.[1] ?? ''} error={timeError(a.between?.[1])} onChange={(e) => set(k, { ...a, between: [a.between?.[0] ?? '', e.target.value] })} />
                      </span>
                    )}
                    {when === 'always' && <span className="es-muted">{tr('se.wholeRun')}</span>}
                  </td>
                  <td>
                    {when === 'always' ? (
                      <span className="es-muted">{tr('se.diagnostics')}</span>
                    ) : (
                      <>
                        <Input aria-label={`Assertion ${k + 1} probe`} mono list="wb-verify-probes" value={a.probe ?? ''} onChange={(e) => set(k, { ...a, probe: e.target.value })} />
                        {when === 'between' && <Select<NonNullable<ScenarioAssertion['measure']> | 'samples'>
                          label={`Assertion ${k + 1} measurement`}
                          size="sm"
                          value={a.measure ?? 'samples'}
                          options={[{ value: 'samples', label: tr('se.everySample') }, { value: 'mean', label: tr('se.timeMean') }, { value: 'rms', label: tr('se.timeRms') }, { value: 'duty_cycle', label: tr('se.dutyCycle') }]}
                          onValueChange={(measure) => {
                            const next = { ...a };
                            if (measure !== 'samples') next.measure = measure;
                            else delete next.measure;
                            set(k, next);
                          }}
                        />}
                      </>
                    )}
                  </td>
                  <td>
                    <Input
                      aria-label={when === 'always' ? `Assertion ${k + 1} forbidden diagnostics` : `Assertion ${k + 1} expectation`}
                      mono
                      value={text}
                      error={expectError}
                      placeholder={when === 'always' ? 'net.contention, mcu.bus_error' : '{"logic": "1"}'}
                      onChange={(e) => {
                        const v = e.target.value;
                        if (when === 'always') {
                          set(k, { ...a, diagnostics: { forbid: v.split(',').map((s) => s.trim()).filter(Boolean) } });
                          return;
                        }
                        useVerify.getState().editDraft((s) => s);
                        setExpectDraft((d) => ({ ...d, [k]: v }));
                        try {
                          const parsed = JSON.parse(v) as unknown;
                          if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) set(k, { ...a, expect: parsed as Record<string, unknown> });
                        } catch {
                          // kept as typed until it parses
                        }
                      }}
                      onBlur={() => setExpectDraft((d) => {
                        const next = { ...d };
                        if (!expectError) delete next[k];
                        return next;
                      })}
                    />
                  </td>
                  <td>
                    <Button size="sm" variant="ghost" icon={<Trash2 />} aria-label={`Remove assertion ${k + 1}`} onClick={() => {
                      setExpectDraft((d) => Object.fromEntries(Object.entries(d).filter(([index]) => Number(index) !== k).map(([index, value]) => [Number(index) > k ? Number(index) - 1 : Number(index), value])));
                      onChange(assertions.filter((_, j) => j !== k));
                    }} />
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      )}
      {probes.length === 0 && <EmptyState title={tr('se.noProbes')}>{tr('se.needProbes')}</EmptyState>}
    </section>
  );
}
