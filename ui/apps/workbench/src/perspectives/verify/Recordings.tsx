// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Recordings (SPEC-012): record what the session does from now on (inputs and
// observations), then replay a recording (record.replay: the project from
// reset, its inputs at their times, every observation compared), turn it
// into a scenario for the editor (record.to_scenario), or save it.

import { Badge, Button, useToast } from '@embsim/design';
import { formatTime } from '@embsim/protocol';
import { Disc, Download, FileInput, RotateCcw, Square } from 'lucide-react';
import { useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient, primeStores } from '../../connection';
import { platform } from '../../platform/platform';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { useVerify, type RecordingEntry } from '../../stores/verify';

export function RecordingsPanel() {
  const capabilities = useSession((s) => s.capabilities);
  const recordings = useVerify((s) => s.recordings);
  const { toast } = useToast();
  const recording = useVerify((s) => s.recordingOn);
  const setRecording = useVerify((s) => s.setRecordingOn);
  const can = capabilities.includes('record') || capabilities.includes('record.inputs');
  const start = async () => {
    try {
      await getClient().record.start({});
      setRecording(true);
    } catch (e) {
      toast({ kind: 'error', title: 'record.start failed', description: errorMessage(e) });
    }
  };
  const stop = async () => {
    try {
      const r = await getClient().record.stop({});
      setRecording(false);
      if (r.recording) useVerify.getState().addRecording(r.recording);
      else toast({ kind: 'success', title: 'Recorded', description: r.path });
    } catch (e) {
      toast({ kind: 'error', title: 'record.stop failed', description: errorMessage(e) });
    }
  };
  return (
    <div className="wb-verify__pane" role="region" aria-label="Recordings">
      <div className="es-row">
        <span className="wb-placeholder__title">Recordings</span>
        <Badge tone={can ? 'accent' : 'warning'}>{can ? 'available' : 'not available'}</Badge>
        {recording && <Badge tone="danger">recording</Badge>}
      </div>
      <div className="es-muted">
        Records the session's inputs and what the run observes (interrupts, probe edges, serial text, diagnostics) from now on. A recording replays identically: a divergence shows both observations.
      </div>
      <div className="es-row">
        <Button size="sm" icon={<Disc />} variant={recording ? 'danger' : 'default'} disabled={!can || recording} onClick={() => void start()}>
          Record
        </Button>
        <Button size="sm" icon={<Square />} disabled={!can || !recording} onClick={() => void stop()}>
          Stop
        </Button>
      </div>
      {recordings.length > 0 && (
        <ul className="wb-verify__list">
          {recordings.map((r) => (
            <RecordingRow key={r.id} entry={r} />
          ))}
        </ul>
      )}
    </div>
  );
}

function RecordingRow({ entry }: { entry: RecordingEntry }) {
  const { toast } = useToast();
  const [busy, setBusy] = useState<string | null>(null);
  const rec = entry.recording;
  const end = BigInt(rec.end?.at_ps ?? '0');
  const replay = async () => {
    setBusy('replay');
    try {
      const c = getClient();
      const r = await c.record.replay({ recording: rec });
      useVerify.getState().setReplay(entry.id, r);
      await primeStores(c); // the replay reloaded the project and ran it
      toast(
        r.ok
          ? { kind: 'success', title: 'Replayed identically', description: `${r.compared ?? rec.observations.length} observations matched` }
          : { kind: 'error', title: r.divergence ? `Diverged at observation ${r.divergence.index + 1}` : 'The project changed since the recording', description: r.drift?.map((d) => d.what).join(', ') },
      );
    } catch (e) {
      toast({ kind: 'error', title: 'Replay failed', description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };
  const toScenario = async () => {
    setBusy('scenario');
    try {
      const name = `${useProject.getState().project?.name ?? 'session'}-recorded`;
      const r = await getClient().record.to_scenario({ recording: rec, name });
      useVerify.getState().openDraft(null, r.scenario);
      toast({ kind: 'success', title: 'A scenario from the recording', description: `${r.scenario.inputs.length} inputs, ${r.scenario.assertions.length} assertions, open in the scenario editor` });
    } catch (e) {
      toast({ kind: 'error', title: 'Cannot make a scenario', description: errorMessage(e) });
    } finally {
      setBusy(null);
    }
  };
  const save = async () => {
    const p = await platform();
    await p.saveTextFile(`recording-${entry.id}.rec.json`, `${JSON.stringify(rec, null, 1)}\n`, 'application/json');
  };
  return (
    <li className="wb-verify__recording">
      <div className="es-row">
        <span className="es-muted">{entry.at}</span>
        <code>
          {rec.inputs.length} inputs · {rec.observations.length} observations · {formatTime(end, { padZeros: false })}
        </code>
        {entry.replay && (
          <Badge tone={entry.replay.ok ? 'accent' : 'danger'}>{entry.replay.ok ? 'replays identically' : entry.replay.divergence ? `diverges at ${entry.replay.divergence.index + 1}` : 'drift'}</Badge>
        )}
      </div>
      <div className="es-row">
        <Button size="sm" icon={<RotateCcw />} disabled={busy !== null} onClick={() => void replay()}>
          {busy === 'replay' ? 'Replaying…' : 'Replay'}
        </Button>
        <Button size="sm" icon={<FileInput />} disabled={busy !== null} onClick={() => void toScenario()}>
          To scenario
        </Button>
        <Button size="sm" variant="ghost" icon={<Download />} onClick={() => void save()}>
          Save
        </Button>
      </div>
      {entry.replay?.divergence && (
        <div className="wb-verify__divergence">
          <div>
            <span className="es-caps">expected</span> <code>{JSON.stringify(entry.replay.divergence.expected)}</code>
          </div>
          <div>
            <span className="es-caps">observed</span> <code>{JSON.stringify(entry.replay.divergence.observed)}</code>
          </div>
        </div>
      )}
    </li>
  );
}
