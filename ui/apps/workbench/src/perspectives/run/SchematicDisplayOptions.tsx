// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { formatTime } from '@embsim/protocol';
import { useSchematicDisplay } from '../../stores/schematicDisplay';
import { useSession } from '../../stores/session';

export function SchematicDisplayOptions() {
  const exposure = useSchematicDisplay((s) => s.exposure), setExposure = useSchematicDisplay((s) => s.setExposure);
  const gain = useSchematicDisplay((s) => s.gain), setGain = useSchematicDisplay((s) => s.setGain);
  const duration = useSession((s) => s.emission.duration);
  return <div className="wb-schematic-exposure"><label title="Interval light averages use native accepted-time emission integrals. Instant shows the state at this simulation time. Brightness is illustrative, not calibrated photometry.">Light <select aria-label="Schematic light exposure" value={exposure ? 'average' : 'instant'} onChange={(e) => setExposure(e.target.value === 'average')}><option value="average">Interval average{duration > 0n ? ` · ${formatTime(duration, { padZeros: false })}` : ''}</option><option value="instant">Instant</option></select></label><select aria-label="Schematic display gain" title="Illustrative light display gain. This changes appearance only; electrical currents and recorded measurements are unchanged." value={gain} onChange={(e) => setGain(Number(e.target.value))}>{[1,4,16].map((g) => <option value={g} key={g}>{g}× light</option>)}</select></div>;
}
