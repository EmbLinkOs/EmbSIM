// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Meter } from '@embsim/instruments';
import { useSession } from '../../stores/session';

/** Numeric readouts of every output and analog net state the session reports. */
export function MetersPanel() {
  const outputs = useSession((s) => s.outputs);
  const nets = useSession((s) => s.nets);
  const entries = Object.entries(outputs).sort(([a], [b]) => a.localeCompare(b));
  const analog = Object.entries(nets)
    .filter(([, s]) => s.voltage !== undefined)
    .sort(([a], [b]) => a.localeCompare(b));
  if (entries.length === 0 && analog.length === 0) return <div className="es-empty">No observations yet</div>;
  return (
    <div className="es-meter-grid">
      {entries.map(([k, v]) => (
        <Meter key={k} label={k} value={v} unit={k.endsWith('.ic') ? 'A' : ''} track={typeof v === 'number'} tone={typeof v === 'boolean' ? 'accent' : 'default'} />
      ))}
      {analog.map(([k, s]) => (
        <Meter key={`net:${k}`} label={`net ${k}`} value={s.voltage} unit="V" track />
      ))}
    </div>
  );
}
