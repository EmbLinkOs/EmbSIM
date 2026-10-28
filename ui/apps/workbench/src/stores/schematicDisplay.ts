// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useMemo } from 'react';
import { create } from 'zustand';
import { useSession } from './session';

export const useSchematicDisplay = create<{ exposure: boolean; gain: number; setExposure(value: boolean): void; setGain(value: number): void }>((set) => ({
  exposure: true, gain: 4,
  setExposure: (exposure) => set({ exposure }),
  setGain: (gain) => set({ gain: [1,4,16].includes(gain) ? gain : 4 }),
}));
export function useSchematicOutputs() {
  const raw = useSession((s) => s.outputs);
  const frame = useSession((s) => s.emission);
  const exposure = useSchematicDisplay((s) => s.exposure);
  const gain = useSchematicDisplay((s) => s.gain);
  return useMemo(() => {
    const outputs = exposure ? { ...raw, ...frame.means } : { ...raw };
    // Camera/display exposure only. Currents, integrals, colors and raw state
    // stay untouched; zero emission remains dark at every gain.
    for (const [key,value] of Object.entries(outputs)) if (/(?:^|\.)(?:red_|green_|blue_)?luminance$/.test(key) && typeof value === 'number') outputs[key] = Math.max(0,Math.min(1,value*gain));
    return outputs;
  }, [exposure, raw, frame, gain]);
}
