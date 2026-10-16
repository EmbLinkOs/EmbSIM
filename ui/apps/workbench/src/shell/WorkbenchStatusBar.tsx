// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { StatusBar, type StatusItem } from '@embsim/design';
import { Cpu, Gauge, Plug, Radio } from 'lucide-react';
import { useInstruments } from '../stores/instruments';
import { useSession } from '../stores/session';

export function WorkbenchStatusBar() {
  const engine = useSession((s) => s.engine);
  const protocol = useSession((s) => s.protocol);
  const status = useSession((s) => s.status);
  const label = useSession((s) => s.transportLabel);
  const error = useSession((s) => s.error);
  const boards = useSession((s) => s.boards);
  const timingTiers = useSession((s) => s.timingTiers);
  const latency = useSession((s) => s.latencyMs);
  const projectName = useSession((s) => s.projectName);
  const frames = useInstruments((s) => s.frames);
  const gaps = useInstruments((s) => s.gaps);

  const tiers = boards.length
    ? boards.map((b) => `${b.id}: ${timingTiers[b.id] ?? b.timing_tier}`).join(' · ')
    : 'no boards';

  const items: StatusItem[] = [
    { id: 'conn', icon: <Plug aria-hidden="true" />, label: status === 'open' ? 'Connected' : status, value: label, title: error ?? undefined },
    { id: 'engine', label: 'Engine', value: engine ? `${engine} · protocol ${protocol}` : '—' },
    { id: 'project', label: 'Project', value: projectName ?? '—' },
    { id: 'tiers', icon: <Cpu aria-hidden="true" />, label: 'Timing', value: tiers },
    { id: 'spacer', spacer: true },
    { id: 'frames', icon: <Radio aria-hidden="true" />, label: 'Frames', value: gaps ? `${frames} (${gaps} gaps)` : String(frames), title: 'Waveform frames received (gaps mean dropped frames)' },
    { id: 'latency', icon: <Gauge aria-hidden="true" />, label: 'Latency', value: `${latency.toFixed(1)} ms`, title: 'Protocol round trip, smoothed' },
  ];
  return <StatusBar items={items} />;
}
