// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { SignalChip, Table, type Column } from '@embsim/design';
import { describeNetState } from '@embsim/scene';
import { useMemo } from 'react';
import { useProject } from '../../stores/project';
import { useSelection } from '../../stores/selection';
import { useSession } from '../../stores/session';

interface NetRow {
  name: string;
  island: string;
  pins: string[];
}

export function NetsPanel({ live }: { live?: boolean }) {
  const project = useProject((s) => s.project);
  const topology = useProject((s) => s.topology);
  const states = useSession((s) => s.nets);
  const selected = useSelection((s) => s.net);
  const selectNet = useSelection((s) => s.selectNet);
  const setSelection = useSelection((s) => s.setSelection);

  const rows = useMemo<NetRow[]>(() => {
    const islands = new Map(topology?.nets.map((n) => [n.name, n.island]) ?? []);
    return Object.entries(project?.nets ?? {})
      .map(([name, pins]) => ({ name, island: islands.get(name) ?? '—', pins }))
      .sort((a, b) => a.name.localeCompare(b.name));
  }, [project, topology]);

  const columns: Column<NetRow>[] = [
    { id: 'name', header: 'Net', mono: true, cell: (r) => r.name },
    { id: 'island', header: 'Island', mono: true, width: '70px', cell: (r) => r.island },
    { id: 'pins', header: 'Pins', mono: true, cell: (r) => r.pins.join(' ') },
    {
      id: 'state',
      header: 'State',
      width: '190px',
      cell: (r) => {
        const v = describeNetState(states[r.name], [0, 3.3]);
        return live || states[r.name] ? <SignalChip kind={v.kind} label={v.text} /> : <span className="es-muted">—</span>;
      },
    },
  ];
  return (
    <Table
      label="Nets"
      columns={columns}
      rows={rows}
      rowKey={(r) => r.name}
      selectedKey={selected}
      onSelect={(r) => {
        const wires = project?.layout?.wires ?? [];
        setSelection({ components: [], wires: wires.map((w, i) => (w.net === r.name ? i : -1)).filter((i) => i >= 0) });
        selectNet(r.name);
      }}
      empty="No nets yet — wire two pins together"
    />
  );
}
