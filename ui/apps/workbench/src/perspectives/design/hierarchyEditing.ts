// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { blockPortOrder, hierarchyOf, sheetAt, type CircuitBlock, type Project } from '@embsim/protocol';
import { autoRoute, projectPinPoints, type SceneCatalog, type NetRef, type PinRef } from '@embsim/scene';

export function editSheet(p: Project, path: string, fn: (sheet: Project | CircuitBlock) => void): Project {
  const { sheet, block } = sheetAt(p, path),
    previous = structuredClone(sheet.nets);
  const paths = hierarchyOf(p)
    .filter((n) => n.block === block)
    .map((n) => n.path);
  fn(sheet);
  for (const [old, pins] of Object.entries(previous))
    if (!(old in sheet.nets)) {
      const to = Object.entries(sheet.nets).find(([, now]) => pins.some((pin) => now.includes(pin)))?.[0];
      if (to)
        for (const prefix of paths)
          for (const probe of p.probes ?? [])
            if (probe.target === `net:${prefix ? prefix + '.' : ''}${old}`)
              probe.target = `net:${prefix ? prefix + '.' : ''}${to}`;
    }
  // Authoritative electrical validation remains native; this catches cycles before dispatch.
  hierarchyOf(p);
  for (const id of Object.keys(p.blocks ?? {}))
    hierarchyOf({ ...p, components: [{ id: 'Preview', component: `block/${id}` }], nets: {} });
  return p;
}

export function connectSheet(
  sheet: Project | CircuitBlock,
  from: PinRef,
  to: PinRef | NetRef,
  points: [number, number][],
): void {
  const a = from.component ? `${from.component}.${from.pin}` : null,
    b = 'component' in to && to.component ? `${to.component}.${to.pin}` : null;
  const netOf = (pin: string | null) =>
    pin ? Object.entries(sheet.nets).find(([, pins]) => pins.includes(pin))?.[0] : undefined;
  let net = a ? netOf(a) : from.net;
  const other = b ? netOf(b) : 'net' in to ? to.net : undefined;
  if (net && other && net !== other) {
    sheet.nets[net] = [...new Set([...sheet.nets[net]!, ...sheet.nets[other]!])];
    delete sheet.nets[other];
    for (const w of sheet.layout?.wires ?? []) if (w.net === other) w.net = net;
    for (const l of sheet.layout?.labels ?? []) if (l.net === other) l.net = net;
    for (const bus of sheet.layout?.buses ?? []) bus.nets = bus.nets.map((n) => (n === other ? net! : n));
  } else net ??= other;
  if (!net) {
    let i = 1;
    while (`N${i}` in sheet.nets) i++;
    net = `N${i}`;
    sheet.nets[net] = [];
  }
  for (const pin of [a, b]) if (pin && !sheet.nets[net]!.includes(pin)) sheet.nets[net]!.push(pin);
  if (points.length >= 2) sheet.layout = { ...sheet.layout, wires: [...(sheet.layout?.wires ?? []), { net, points }] };
}

/** Extract a selection, preserve boundary nets and remap project-owned observations. */
export function extractBlock(p: Project, path: string, selected: string[], id: string): string {
  if (!/^[a-z][a-z0-9_-]{0,63}$/.test(id) || p.blocks?.[id])
    throw new Error('Use a unique block identifier (lowercase letters, digits, underscores or hyphens)');
  const { sheet, block: source } = sheetAt(p, path),
    own = new Set(selected),
    parts = sheet.components.filter((c) => own.has(c.id));
  if (!parts.length || parts.some((c) => c.layout?.locked)) throw new Error('Select unlocked components to extract');
  const inside = (pin: string) => own.has(pin.split('.')[0]!);
  const beforeNodes = hierarchyOf(p).filter((n) => n.block === source);
  let n = 1;
  while ([...sheet.components, ...('boards' in sheet ? (sheet.boards ?? []) : [])].some((c) => c.id === `X${n}`)) n++;
  const instance = `X${n}`,
    x = Math.min(...parts.map((c) => c.layout?.x ?? 0)),
    y = Math.min(...parts.map((c) => c.layout?.y ?? 0));
  const child: CircuitBlock = { name: id, components: structuredClone(parts), nets: {}, ports: {} };
  for (const c of child.components)
    c.layout = { ...c.layout, x: (c.layout?.x ?? 0) - x + 80, y: (c.layout?.y ?? 0) - y + 80 };
  const inherited: Record<string, string> = {};
  for (const c of child.components)
    for (const value of Object.values(c.params ?? {})) {
      if (typeof value !== 'string' || !value.startsWith('$')) continue;
      const name = value.slice(1),
        spec = 'ports' in sheet ? sheet.params?.[name] : undefined;
      if (!spec) throw new Error(`Unknown source parameter ${value}`);
      child.params = { ...child.params, [name]: structuredClone(spec) };
      inherited[name] = value;
    }
  const affected = new Set<string>(),
    internal = new Set<string>();
  for (const [net, pins] of Object.entries(sheet.nets)) {
    const selectedPins = pins.filter(inside);
    if (!selectedPins.length) continue;
    affected.add(net);
    child.nets[net] = selectedPins;
    const external = pins.filter((pin) => !inside(pin));
    const exports = 'ports' in sheet ? Object.entries(sheet.ports).filter(([, pin]) => selectedPins.includes(pin)) : [];
    if (external.length || exports.length) {
      let port = net.replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 50) || 'PORT';
      if (!/^[A-Za-z]/.test(port)) port = `P_${port}`;
      let suffix = 1;
      const base = port;
      while (port in child.ports) port = `${base}_${suffix++}`;
      child.ports[port] = selectedPins[0]!;
      sheet.nets[net] = [...external, `${instance}.${port}`];
      for (const [name] of exports) if ('ports' in sheet) sheet.ports[name] = `${instance}.${port}`;
    } else {
      delete sheet.nets[net];
      internal.add(net);
    }
  }
  child.layout = {
    port_order: Object.keys(child.ports),
    wires: (sheet.layout?.wires ?? [])
      .filter((w) => internal.has(w.net))
      .map((w) => ({ ...w, points: w.points.map(([a, b]) => [a - x + 80, b - y + 80]) })),
  };
  sheet.layout = {
    ...sheet.layout,
    wires: (sheet.layout?.wires ?? []).filter((w) => !affected.has(w.net)),
    labels: (sheet.layout?.labels ?? []).filter((l) => !internal.has(l.net)),
    buses: (sheet.layout?.buses ?? [])
      .map((b) => ({ ...b, nets: b.nets.filter((net) => !internal.has(net)) }))
      .filter((b) => b.nets.length),
    groups: (sheet.layout?.groups ?? [])
      .map((g) => ({ ...g, members: g.members.filter((part) => !own.has(part)) }))
      .filter((g) => g.members.length),
  };
  if (sheet.layout?.bench) {
    const bench = sheet.layout.bench;
    child.layout = {
      ...child.layout,
      bench: {
        parts: Object.fromEntries(Object.entries(bench.parts ?? {}).filter(([key]) => own.has(key))),
        wires: (bench.wires ?? []).filter((w) => inside(w.from) && inside(w.to)),
      },
    };
    bench.parts = Object.fromEntries(Object.entries(bench.parts ?? {}).filter(([key]) => !own.has(key)));
    bench.wires = (bench.wires ?? []).filter((w) => !inside(w.from) && !inside(w.to));
  }
  sheet.components = sheet.components.filter((c) => !own.has(c.id));
  sheet.components.push({
    id: instance,
    component: `block/${id}`,
    layout: { x, y },
    ...(Object.keys(inherited).length ? { params: inherited } : {}),
  });
  p.blocks = { ...p.blocks, [id]: child };
  for (const node of beforeNodes)
    for (const probe of p.probes ?? []) {
      const prefix = node.path ? node.path + '.' : '',
        target = probe.target;
      for (const part of parts)
        for (const kind of ['pin', 'device', 'output'])
          if (target.startsWith(`${kind}:${prefix}${part.id}.`))
            probe.target = `${kind}:${prefix}${instance}.${target.slice(kind.length + 1 + prefix.length)}`;
      for (const part of parts)
        if (target === `serial:${prefix}${part.id}`) probe.target = `serial:${prefix}${instance}.${part.id}`;
      for (const net of internal)
        if (target === `net:${prefix}${net}`) probe.target = `net:${prefix}${instance}.${net}`;
    }
  hierarchyOf(p);
  return instance;
}

export function exposePort(sheet: CircuitBlock, name: string, target: string): void {
  if (!/^[A-Za-z0-9_+\-/~#]{1,32}$/.test(name)) throw new Error('Use a port name without spaces or dots');
  if (!Object.values(sheet.nets).some((pins) => pins.includes(target))) {
    let i = 1;
    while (`N${i}` in sheet.nets) i++;
    sheet.nets[`N${i}`] = [target];
  }
  const order = blockPortOrder(sheet);
  sheet.ports = { ...sheet.ports, [name]: target };
  sheet.layout = { ...sheet.layout, port_order: order.includes(name) ? order : [...order, name] };
}

export function renamePort(p: Project, block: string, old: string, name: string): void {
  const def = p.blocks?.[block];
  if (!def || name === old) return;
  if (!/^[A-Za-z0-9_+\-/~#]{1,32}$/.test(name) || name in def.ports) throw new Error('Use a unique port name');
  def.layout = { ...def.layout, port_order: blockPortOrder(def).map((port) => (port === old ? name : port)) };
  def.ports = Object.fromEntries(Object.entries(def.ports).map(([key, target]) => [key === old ? name : key, target]));
  for (const sheet of [p, ...Object.values(p.blocks ?? {})]) {
    const ids = sheet.components.filter((c) => c.component === `block/${block}`).map((c) => c.id);
    const ref = (pin: string) =>
      ids.some((id) => pin === `${id}.${old}`) ? `${pin.slice(0, -old.length)}${name}` : pin;
    for (const net of Object.keys(sheet.nets)) sheet.nets[net] = sheet.nets[net]!.map(ref);
    if ('ports' in sheet) for (const key of Object.keys(sheet.ports)) sheet.ports[key] = ref(sheet.ports[key]!);
  }
  for (const node of hierarchyOf(p).filter((n) => n.block === block))
    for (const probe of p.probes ?? [])
      if (probe.target === `pin:${node.path}.${old}`) probe.target = `pin:${node.path}.${name}`;
}

/** Re-anchor labels onto retained geometry when automatic routing repairs a move. */
export function routeSheet(p: Project, catalog: SceneCatalog, affected?: Set<string>): Project {
  const result = autoRoute(p, catalog, affected).project;
  const pins = projectPinPoints(result, catalog);
  if (result.layout?.labels)
    result.layout.labels = result.layout.labels.map((label) => {
      if (affected && !affected.has(label.net)) return label;
      const points = (result.layout?.wires ?? [])
        .filter((w) => w.net === label.net)
        .flatMap((w) => w.points.map(([x, y]) => ({ x, y })));
      if (!points.length) points.push(...(pins[label.net] ?? []));
      const at = points.sort(
        (a, b) => (a.x - label.x) ** 2 + (a.y - label.y) ** 2 - ((b.x - label.x) ** 2 + (b.y - label.y) ** 2),
      )[0];
      return at ? { ...label, ...at } : label;
    });
  return result;
}
