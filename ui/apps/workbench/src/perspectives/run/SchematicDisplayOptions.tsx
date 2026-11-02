// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { formatTime } from '@embsim/protocol';
import { useSchematicDisplay } from '../../stores/schematicDisplay';
import { useSession } from '../../stores/session';
import { t as tr } from '../../i18n';

export function SchematicDisplayOptions() {
  const exposure = useSchematicDisplay((s) => s.exposure), setExposure = useSchematicDisplay((s) => s.setExposure);
  const gain = useSchematicDisplay((s) => s.gain), setGain = useSchematicDisplay((s) => s.setGain);
  const duration = useSession((s) => s.emission.duration);
  const hasBaseline = useSession((s) => Object.keys(s.emission.totals).length > 0);
  return <div className="wb-schematic-exposure"><label title={tr('light.hint')}>{tr('light.label')} <select aria-label={tr('light.exposure')} value={exposure ? 'average' : 'instant'} onChange={(e) => setExposure(e.target.value === 'average')}><option value="average">{tr('light.average')}{duration > 0n ? ` · ${formatTime(duration, { padZeros: false })}` : hasBaseline ? ` · ${tr('light.waiting')}` : ` · ${tr('light.unavailable')}`}</option><option value="instant">{tr('light.instant')}</option></select></label><select aria-label={tr('light.gain')} title={tr('light.gain.hint')} value={gain} onChange={(e) => setGain(Number(e.target.value))}>{[1,4,16].map((g) => <option value={g} key={g}>{tr('light.gainOption', { g })}</option>)}</select></div>;
}
