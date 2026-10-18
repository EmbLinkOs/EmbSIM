// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Meter } from '@embsim/instruments';
import { useSession } from '../../stores/session';

/** The unit of an output from its name (SPEC-005 output conventions). */
function outputUnit(key: string): string {
  const name = key.slice(key.lastIndexOf('.') + 1).toLowerCase();
  if (['i', 'ic', 'ib', 'ie', 'id', 'current'].includes(name)) return 'A';
  if (['v', 'vd', 'vce', 'vbe', 'vgs', 'vds', 'voltage'].includes(name)) return 'V';
  if (['p', 'power'].includes(name)) return 'W';
  return '';
}

/** A circuit's own parts first, then the parts inside boards ("B1.LED1.i"). */
function depth(key: string): number {
  return key.split('.').length;
}

/** Numeric readouts of every output and analog net state the session reports. */
export function MetersPanel() {
  const outputs = useSession((s) => s.outputs);
  const nets = useSession((s) => s.nets);
  const entries = Object.entries(outputs)
    // a text that grows (a terminal's, an EEPROM's dump) is no reading: the Serial tab and
    // the inspector show those; a short state word still is
    .filter(([k, v]) => typeof v === 'number' || typeof v === 'boolean' || (typeof v === 'string' && v.length <= 24 && !/(text|dump)$/i.test(k)))
    .sort(([a], [b]) => depth(a) - depth(b) || a.localeCompare(b));
  const analog = Object.entries(nets)
    .filter(([, s]) => s.voltage !== undefined)
    .sort(([a], [b]) => a.localeCompare(b));
  if (entries.length === 0 && analog.length === 0) return <div className="es-empty">No observations yet</div>;
  return (
    <div className="es-meter-grid">
      {entries.map(([k, v]) => (
        <Meter key={k} label={k} value={v} unit={outputUnit(k)} track={typeof v === 'number'} tone={typeof v === 'boolean' ? 'accent' : 'default'} />
      ))}
      {analog.map(([k, s]) => (
        <Meter key={`net:${k}`} label={`net ${k}`} value={s.voltage} unit="V" track />
      ))}
    </div>
  );
}
