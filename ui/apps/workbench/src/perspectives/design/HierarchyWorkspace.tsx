// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { Badge, Button, DockLayout, IconButton, SegmentedControl, useToast, type DockItem } from '@embsim/design';
import { blockDependencies, parseSi, sheetAt, type CircuitBlock, type Project } from '@embsim/protocol';
import {
  EMPTY_SELECTION,
  freePlacement,
  routeConnection,
  shiftWireSegment,
  SchematicCanvas,
  type CanvasMode,
  type SchematicCanvasHandle,
  type Selection,
} from '@embsim/scene';
import { ArrowLeft, Maximize2, RotateCw, Trash2, Undo2, Redo2 } from 'lucide-react';
import { useMemo, useRef, useState } from 'react';
import { errorMessage, inputs as inputActions, probes as probeActions } from '../../actions';
import { platform } from '../../platform/platform';
import { useHierarchy } from '../../stores/hierarchy';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { useCanvasAppearance } from '../../stores/canvasAppearance';
import { useSchematicOutputs } from '../../stores/schematicDisplay';
import { connectSheet, editSheet, exposePort, extractBlock, renamePort, routeSheet } from './hierarchyEditing';

export function HierarchyWorkspace() {
  const store = useProject(),
    p = store.project,
    h = useHierarchy(),
    session = useSession(),
    outputs = useSchematicOutputs();
  const appearance = useCanvasAppearance((s) => s.appearance),
    { toast } = useToast();
  const canvas = useRef<SchematicCanvasHandle>(null),
    host = useRef<HTMLDivElement>(null);
  const [mode, setMode] = useState<CanvasMode>('select'),
    [chosen, setChosen] = useState<{ path: string; value: Selection }>({ path: '', value: EMPTY_SELECTION });
  const [query, setQuery] = useState(''),
    [part, setPart] = useState('emblink/resistor'),
    [blockName, setBlockName] = useState(''),
    [portName, setPortName] = useState(''),
    [target, setTarget] = useState('');
  const [trace, setTrace] = useState<string | null>(null);
  const context = useMemo(() => {
    if (!p) return null;
    let path = h.path,
      resolved: ReturnType<typeof sheetAt>;
    try {
      resolved = sheetAt(p, path);
    } catch {
      path = '';
      resolved = sheetAt(p, '');
    }
    const { sheet, block } = resolved;
    const resolvedParams = store.hierarchy.find((n) => n.path === path)?.params ?? {};
    const components =
      path && !path.startsWith('@')
        ? sheet.components.map((c) => ({
            ...c,
            params: c.params
              ? Object.fromEntries(
                  Object.entries(c.params).map(([name, value]) => [
                    name,
                    typeof value === 'string' && value.startsWith('$')
                      ? (resolvedParams[value.slice(1)] ?? value)
                      : value,
                  ]),
                )
              : undefined,
          }))
        : sheet.components;
    const view: Project = {
      ...p,
      name: sheet.name,
      components,
      nets: sheet.nets,
      layout: sheet.layout,
      boards: path ? [] : p.boards,
    };
    return { path, sheet, block, view };
  }, [p, h.path, store.hierarchy]);
  if (!p || !context) return <p>Loading project…</p>;
  const { path, sheet, block, view } = context;
  const definition = block ? p.blocks![block]! : null;
  const selection = chosen.path === path ? chosen.value : EMPTY_SELECTION;
  const select = (value: Selection) => setChosen({ path, value });
  const prefix = path && !path.startsWith('@') ? path + '.' : '';
  const aliases = store.hierarchy.find((n) => n.path === path)?.nets ?? {};
  const states = Object.fromEntries(
    Object.entries(aliases)
      .filter(([, net]) => session.nets[net])
      .map(([net, full]) => [net, session.nets[full]!]),
  );
  const highlight = path.startsWith('@')
    ? trace?.startsWith(`@${block}:`)
      ? trace.slice(block.length + 2)
      : null
    : (Object.keys(aliases).find((net) => aliases[net] === trace) ?? null);
  const guard = (fn: () => Promise<unknown>) => () =>
    void fn().catch((e: unknown) =>
      toast({ kind: 'error', title: 'Hierarchy edit rejected', description: errorMessage(e) }),
    );
  const edit = (label: string, fn: (s: Project | CircuitBlock, p: Project) => void) =>
    store.mutate(label, (doc) => editSheet(doc, path, (s) => fn(s, doc)));
  const writable = store.editable && !store.busy;
  const go = (next: string) => {
    h.show(next);
    setChosen({ path: next, value: EMPTY_SELECTION });
  };
  const numericKey = (base: string) => {
    let n = 1;
    while ([...sheet.components, ...(!path ? (p.boards ?? []) : [])].some((c) => c.id === `${base}${n}`)) n++;
    return `${base}${n}`;
  };
  const place = async (id: string, at?: { x: number; y: number }) => {
    await store.ensureCatalog([id]);
    const catalog = useProject.getState().sceneCatalog,
      symbol = catalog[id]?.symbol;
    if (!symbol) throw new Error('Part symbol unavailable');
    const vp = canvas.current?.getViewport() ?? { x: 0, y: 0, k: 1 },
      svg = host.current?.querySelector('.es-scene__svg');
    const centre = at ?? {
      x: ((svg?.clientWidth ?? 600) / 2 - vp.x) / vp.k,
      y: ((svg?.clientHeight ?? 400) / 2 - vp.y) / vp.k,
    };
    const cid = numericKey(
      id.startsWith('block/') ? 'X' : id.endsWith('/resistor') ? 'R' : id.endsWith('/capacitor') ? 'C' : 'U',
    );
    await edit(`Place ${cid} on ${path || 'Main'}`, (s) => {
      const placement = freePlacement({ ...view, components: s.components, nets: s.nets }, catalog, symbol, centre);
      s.components.push({ id: cid, component: id, layout: placement });
    });
    select({ components: [cid], wires: [] });
  };
  const remove = async (sel: Selection) =>
    edit('Delete sheet selection', (s) => {
      const ids = new Set(sel.components);
      if (s.components.some((c) => ids.has(c.id) && c.layout?.locked))
        throw new Error('Unlock components before deleting them');
      if ('ports' in s && Object.values(s.ports).some((pin) => ids.has(pin.split('.')[0]!)))
        throw new Error('Remap or remove exported ports before deleting their components');
      const touched = new Set(
        Object.entries(s.nets)
          .filter(([, pins]) => pins.some((pin) => ids.has(pin.split('.')[0]!)))
          .map(([n]) => n),
      );
      s.components = s.components.filter((c) => !ids.has(c.id));
      if ('boards' in s) s.boards = s.boards?.filter((c) => !ids.has(c.id));
      for (const [net, pins] of Object.entries(s.nets)) {
        s.nets[net] = pins.filter((pin) => !ids.has(pin.split('.')[0]!));
        if (!s.nets[net]!.length) delete s.nets[net];
      }
      s.layout = {
        ...s.layout,
        wires: (s.layout?.wires ?? []).filter((w, i) => !sel.wires.includes(i) && !touched.has(w.net)),
        labels: (s.layout?.labels ?? []).filter((l) => l.net in s.nets),
        buses: (s.layout?.buses ?? [])
          .map((b) => ({ ...b, nets: b.nets.filter((n) => n in s.nets) }))
          .filter((b) => b.nets.length),
        groups: (s.layout?.groups ?? [])
          .map((g) => ({ ...g, members: g.members.filter((id) => !ids.has(id)) }))
          .filter((g) => g.members.length),
      };
    });
  const move = (ids: string[], dx: number, dy: number) =>
    edit('Move sheet components', (s) => {
      for (const c of [...s.components, ...('boards' in s ? (s.boards ?? []) : [])])
        if (ids.includes(c.id)) {
          if (c.layout?.locked) throw new Error('Component is locked');
          c.layout = { ...c.layout, x: (c.layout?.x ?? 0) + dx, y: (c.layout?.y ?? 0) + dy };
        }
      s.layout = routeSheet(
        { ...view, components: s.components, boards: 'boards' in s ? s.boards : [], nets: s.nets, layout: s.layout },
        store.sceneCatalog,
        new Set(
          Object.entries(s.nets)
            .filter(([, pins]) => pins.some((pin) => ids.includes(pin.split('.')[0]!)))
            .map(([n]) => n),
        ),
      ).layout;
    });
  const rotate = (ids: string[]) =>
    edit('Rotate sheet components', (s) => {
      for (const c of [...s.components, ...('boards' in s ? (s.boards ?? []) : [])])
        if (ids.includes(c.id)) {
          if (c.layout?.locked) throw new Error('Component is locked');
          c.layout = {
            ...c.layout,
            x: c.layout?.x ?? 0,
            y: c.layout?.y ?? 0,
            rotation: ((c.layout?.rotation ?? 0) + 90) % 360,
          };
        }
      s.layout = routeSheet(
        { ...view, components: s.components, boards: 'boards' in s ? s.boards : [], nets: s.nets, layout: s.layout },
        store.sceneCatalog,
      ).layout;
    });
  const addBlock = async (extract: boolean) => {
    const id = blockName.trim();
    if (!/^[a-z][a-z0-9_-]{0,63}$/.test(id) || p.blocks?.[id]) throw new Error('Enter a unique block identifier');
    await store.mutate(extract ? 'Extract reusable circuit block' : 'Create circuit block', (doc) => {
      if (extract) {
        extractBlock(doc, path, selection.components, id);
        return doc;
      }
      doc.blocks = {
        ...doc.blocks,
        [id]: { name: id, components: [], nets: {}, ports: {}, layout: { port_order: [] } },
      };
      return doc;
    });
    setBlockName('');
    go(`@${id}`);
  };
  const selected = sheet.components.find((c) => selection.components.includes(c.id));
  const params = selected ? (store.catalog[selected.component]?.description.params ?? {}) : {};
  const pinOptions = sheet.components.flatMap((c) =>
    Object.keys(store.sceneCatalog[c.component]?.symbol?.pins ?? {}).map((pin) => `${c.id}.${pin}`),
  );
  const parts = [
    ...store.items.filter((i) => i.kind === 'component'),
    ...Object.entries(p.blocks ?? {}).map(([id, b]) => ({ id: `block/${id}`, name: b.name })),
  ]
    .filter((i) => `${i.id} ${i.name}`.toLowerCase().includes(query.toLowerCase()))
    .slice(0, 100);
  const selectedPart = parts.some((i) => i.id === part) ? part : (parts[0]?.id ?? '');
  const exportBlock = async () => {
    if (!block) return;
    const instanceParams = store.hierarchy.find((n) => n.path === path)?.params;
    const requiredMissing = Object.entries(definition?.params ?? {}).some(
      ([name, spec]) => (spec.required ?? true) && spec.default === undefined && instanceParams?.[name] === undefined,
    );
    const doc: Project = {
      schema_version: 1,
      kind: 'project',
      name: definition!.name,
      policy: p.policy,
      components: requiredMissing ? [] : [{ id: 'X1', component: `block/${block}`, params: instanceParams }],
      nets: {},
      blocks: blockDependencies(p, block),
    };
    await (await platform()).saveTextFile(`${block}.embsim.json`, JSON.stringify(doc, null, 2), 'application/json');
  };
  const tree = (
    <div className="wb-hierarchy__sidebar">
      <p className="es-muted">
        Instance sheets share their block definition. Electrical edits restart simulation; drawing edits preserve time.
      </p>
      <nav aria-label="Hierarchy sheets">
        {store.hierarchy.map((n) => (
          <button
            className={path === n.path ? 'is-active' : ''}
            key={n.path}
            onClick={() => go(n.path)}
            style={{ paddingLeft: 12 + n.path.split('.').filter(Boolean).length * 14 }}
          >
            {n.path || 'Main'}
            {n.block && <small>{p.blocks?.[n.block]?.name}</small>}
          </button>
        ))}
      </nav>
      <h3>Reusable blocks</h3>
      {Object.entries(p.blocks ?? {}).map(([id, b]) => (
        <div className="wb-hierarchy__block" key={id}>
          <button onClick={() => go(`@${id}`)} aria-current={path === `@${id}` ? 'page' : undefined}>
            {b.name}
            <small>{store.hierarchy.filter((n) => n.block === id).length} instances</small>
          </button>
          <Button
            size="sm"
            variant="ghost"
            disabled={!writable || block === id}
            onClick={guard(() => place(`block/${id}`))}
          >
            Place
          </Button>
        </div>
      ))}
      <label>
        Block identifier
        <input value={blockName} onChange={(e) => setBlockName(e.target.value)} placeholder="power_stage" />
      </label>
      <div className="es-row">
        <Button size="sm" disabled={!writable} onClick={guard(() => addBlock(false))}>
          New block
        </Button>
        <Button size="sm" disabled={!writable || !selection.components.length} onClick={guard(() => addBlock(true))}>
          Extract selection
        </Button>
      </div>
      <label>
        Search parts
        <input value={query} onChange={(e) => setQuery(e.target.value)} placeholder="Resistor, amplifier, block…" />
      </label>
      <label>
        Part to place
        <select value={selectedPart} onChange={(e) => setPart(e.target.value)}>
          {parts.map((i) => (
            <option key={i.id} value={i.id}>
              {i.name}
            </option>
          ))}
        </select>
      </label>
      <Button size="sm" disabled={!writable || !selectedPart} onClick={guard(() => place(selectedPart))}>
        Place part
      </Button>
      <h3>Share blocks</h3>
      <Button size="sm" disabled={!block} onClick={guard(exportBlock)}>
        Export block project
      </Button>
      <Button
        size="sm"
        variant="ghost"
        disabled={!writable}
        onClick={guard(async () => {
          const file = await (await platform()).openProjectFile();
          if (!file) return;
          const doc = JSON.parse(file.text) as Project;
          if (doc.kind !== 'project' || !doc.blocks || Array.isArray(doc.blocks))
            throw new Error('Choose an EmbSIM project with block definitions');
          await store.mutate('Import reusable blocks', (next) => {
            for (const [id, b] of Object.entries(doc.blocks!))
              if (next.blocks?.[id] && JSON.stringify(next.blocks[id]) !== JSON.stringify(b))
                throw new Error(`Block ${id} already exists with different content`);
            next.blocks = { ...next.blocks, ...doc.blocks };
            return next;
          });
        })}
      >
        Import block project
      </Button>
    </div>
  );
  const inspector = (
    <div className="wb-hierarchy__sidebar">
      <h3>{selected ? selected.id : 'Sheet properties'}</h3>
      {definition && (
        <>
          <p className="es-muted">
            Editing shared definition <strong>{block}</strong>. All instances update together.
          </p>
          <label>
            Sheet name
            <input
              key={`${block}:${definition.name}`}
              defaultValue={definition.name}
              disabled={!writable}
              onBlur={(e) =>
                e.target.value.trim() !== definition.name &&
                guard(() =>
                  edit('Rename block', (s) => {
                    s.name = e.target.value.trim() || block;
                  }),
                )()
              }
            />
          </label>
        </>
      )}
      {selected && (
        <>
          <p>{selected.component}</p>
          {selected.component.startsWith('block/') && (
            <Button
              size="sm"
              onClick={() => go(path.startsWith('@') ? `@${selected.component.slice(6)}` : `${prefix}${selected.id}`)}
            >
              Open sheet
            </Button>
          )}
          {Object.entries(params).map(([name, spec]) => (
            <label key={`${selected.id}:${name}:${String(selected.params?.[name])}`}>
              {name}
              {spec.unit ? ` (${spec.unit})` : ''}
              <input
                aria-label={`Parameter ${selected.id}.${name}`}
                defaultValue={String(selected.params?.[name] ?? spec.default ?? '')}
                disabled={!writable}
                onBlur={(e) =>
                  e.target.value !== String(selected.params?.[name] ?? spec.default ?? '') &&
                  guard(() =>
                    edit(`Set ${selected.id}.${name}`, (s) => {
                      const c = s.components.find((c) => c.id === selected.id)!;
                      const raw = e.target.value;
                      let value: string | number | boolean = raw;
                      if (spec.type === 'number' || spec.type === 'integer') {
                        const n = parseSi(raw);
                        if (n === undefined) throw new Error('Enter a valid number with optional SI suffix');
                        value = n;
                      }
                      if (spec.type === 'boolean') value = raw === 'true';
                      c.params = { ...c.params, [name]: value };
                    }),
                  )()
                }
              />
            </label>
          ))}
          <h3>Pin connections</h3>
          {Object.keys(store.sceneCatalog[selected.component]?.symbol?.pins ?? {}).map((pin) => {
            const ref = `${selected.id}.${pin}`,
              net = Object.entries(sheet.nets).find(([, pins]) => pins.includes(ref))?.[0] ?? '';
            return (
              <label key={pin}>
                {pin}
                <select
                  aria-label={`Connect ${ref}`}
                  value={net}
                  disabled={!writable}
                  onChange={(e) =>
                    guard(() =>
                      edit(`Connect ${ref}`, (s) => {
                        for (const pins of Object.values(s.nets)) {
                          const i = pins.indexOf(ref);
                          if (i >= 0) pins.splice(i, 1);
                        }
                        if (e.target.value) s.nets[e.target.value]!.push(ref);
                        s.layout = {
                          ...s.layout,
                          wires: (s.layout?.wires ?? []).filter((w) => w.net !== net && w.net !== e.target.value),
                        };
                      }),
                    )()
                  }
                >
                  <option value="">Unconnected</option>
                  {Object.keys(sheet.nets).map((n) => (
                    <option key={n}>{n}</option>
                  ))}
                </select>
              </label>
            );
          })}
        </>
      )}
      {definition && (
        <>
          <h3>Sheet ports</h3>
          {Object.entries(definition.ports).map(([name, pin]) => (
            <div key={name} className="wb-hierarchy__port">
              <label>
                Port name
                <input
                  aria-label={`Rename port ${name}`}
                  defaultValue={name}
                  disabled={!writable}
                  onBlur={(e) =>
                    e.target.value !== name &&
                    guard(() =>
                      store.mutate('Rename block port', (doc) => {
                        renamePort(doc, block, name, e.target.value);
                        return doc;
                      }),
                    )()
                  }
                />
              </label>
              <label>
                Internal pin
                <select
                  aria-label={`Map port ${name}`}
                  disabled={!writable}
                  value={pin}
                  onChange={(e) =>
                    guard(() =>
                      edit('Remap block port', (s) => {
                        if ('ports' in s) exposePort(s, name, e.target.value);
                      }),
                    )()
                  }
                >
                  {pinOptions.map((ref) => (
                    <option key={ref}>{ref}</option>
                  ))}
                </select>
              </label>
            </div>
          ))}
          <label>
            New port name
            <input value={portName} onChange={(e) => setPortName(e.target.value)} placeholder="OUT" />
          </label>
          <label>
            Port target pin
            <select value={target} onChange={(e) => setTarget(e.target.value)}>
              <option value="">Select a pin</option>
              {pinOptions.map((pin) => (
                <option key={pin}>{pin}</option>
              ))}
            </select>
          </label>
          <Button
            size="sm"
            disabled={!writable || !target || !portName}
            onClick={guard(() =>
              edit('Expose sheet port', (s) => {
                if (!('ports' in s) || portName in s.ports) throw new Error('Use a unique port name');
                exposePort(s, portName, target);
              }),
            )}
          >
            Expose port
          </Button>
        </>
      )}
      <h3>Cross-sheet net trace</h3>
      <label>
        Trace local net
        <select
          aria-label="Trace local net"
          value={highlight ?? ''}
          onChange={(e) =>
            setTrace(path.startsWith('@') ? `@${block}:${e.target.value}` : (aliases[e.target.value] ?? null))
          }
        >
          <option value="">Select a net</option>
          {Object.keys(sheet.nets).map((net) => (
            <option key={net}>{net}</option>
          ))}
        </select>
      </label>
      {trace && !path.startsWith('@') && (
        <div className="wb-hierarchy__trace" role="region" aria-label="Cross-sheet trace">
          <strong>{trace}</strong>
          {store.hierarchy.flatMap((n) =>
            Object.entries(n.nets)
              .filter(([, full]) => full === trace)
              .map(([net]) => (
                <button
                  key={`${n.path}:${net}`}
                  onClick={() => {
                    go(n.path);
                    requestAnimationFrame(() => canvas.current?.focusNet(net));
                  }}
                >
                  {n.path || 'Main'} · {net}
                  <small>{sheetAt(p, n.path).sheet.nets[net]?.join(' · ')}</small>
                </button>
              )),
          )}
        </div>
      )}
      <p className="es-muted">
        Ports are electrical aliases. Their loading and behavior come from the actual internal components.
        Uninstantiated definitions have no live measurements.
      </p>
    </div>
  );
  const pane = (
    <div className="wb-hierarchy__canvas" ref={host}>
      <SchematicCanvas
        key={path}
        handle={canvas}
        project={view}
        sheetPorts={definition?.ports}
        catalog={store.sceneCatalog}
        appearance={appearance}
        mode={mode}
        selection={selection}
        onSelectionChange={select}
        readOnly={!writable}
        live={!path.startsWith('@')}
        netStates={states}
        outputs={
          path.startsWith('@')
            ? {}
            : Object.fromEntries(
                Object.entries(outputs)
                  .filter(([id]) => id.startsWith(prefix))
                  .map(([id, value]) => [id.slice(prefix.length), value]),
              )
        }
        inputs={
          path.startsWith('@')
            ? []
            : session.inputs
                .filter((i) => i.target.startsWith(prefix))
                .map((i) => ({ ...i, target: i.target.slice(prefix.length) }))
        }
        onInput={
          path.startsWith('@')
            ? undefined
            : (target, input, value) => guard(() => inputActions.set(`${prefix}${target}`, input, value))()
        }
        highlightNet={highlight}
        onHighlightNet={(net) => {
          if (net) setTrace(path.startsWith('@') ? `@${block}:${net}` : (aliases[net] ?? null));
        }}
        onMove={(ids, dx, dy) => guard(() => move(ids, dx, dy))()}
        onRotate={(ids) => guard(() => rotate(ids))()}
        onDelete={(sel) =>
          guard(async () => {
            await remove(sel);
            select(EMPTY_SELECTION);
          })()
        }
        onPlace={(id, at) => guard(() => place(id, at))()}
        onOpenComponent={(id) => {
          select({ components: [id], wires: [] });
          const c = sheet.components.find((c) => c.id === id);
          if (c?.component.startsWith('block/'))
            go(path.startsWith('@') ? `@${c.component.slice(6)}` : `${prefix}${id}`);
        }}
        onWire={(from, to, points, manual) =>
          guard(() =>
            edit('Connect sheet pins', (s) => {
              connectSheet(
                s,
                from,
                to,
                manual ? points : (routeConnection(view, store.sceneCatalog, from, to) ?? points),
              );
            }),
          )()
        }
        onReshapeWire={(index, segment, offset, source) => {
          if (source !== view) return;
          guard(() =>
            edit('Reshape sheet wire', (s) => {
              const w = s.layout?.wires?.[index];
              if (w) s.layout!.wires = shiftWireSegment(s.layout!.wires!, index, segment, offset) ?? s.layout!.wires;
            }),
          )();
        }}
        onNetClick={(net) =>
          guard(async () => {
            const native = aliases[net];
            if (!native) throw new Error('Instantiate this definition before probing');
            await probeActions.add(`net:${native}`, 'analog', 200000);
          })()
        }
        label={`Sheet ${path || 'Main'}`}
      />
    </div>
  );
  const items: DockItem[] = [
    { id: 'tree', title: 'Sheets and blocks', content: tree, weight: 1, minSize: 230, collapsible: true },
    {
      id: 'canvas',
      title: path.startsWith('@') ? 'Block definition' : path || 'Main sheet',
      content: pane,
      weight: 3.6,
      minSize: 360,
      flush: true,
    },
    {
      id: 'properties',
      title: 'Sheet inspector and tracing',
      content: inspector,
      weight: 1.2,
      minSize: 270,
      collapsible: true,
    },
  ];
  return (
    <div className="wb-perspective wb-hierarchy">
      <div className="wb-hierarchy__toolbar">
        <Button size="sm" variant="ghost" icon={<ArrowLeft />} onClick={h.close}>
          Overview
        </Button>
        <label>
          Active sheet
          <select aria-label="Active sheet" value={path} onChange={(e) => go(e.target.value)}>
            <option value="">Main</option>
            {store.hierarchy
              .filter((n) => n.path)
              .map((n) => (
                <option key={n.path} value={n.path}>
                  {n.path} · {n.block}
                </option>
              ))}
            {Object.entries(p.blocks ?? {}).map(([id, b]) => (
              <option key={`@${id}`} value={`@${id}`}>
                Definition · {b.name}
              </option>
            ))}
          </select>
        </label>
        <Badge tone="accent">{path.startsWith('@') ? 'Definition' : path || 'Main'}</Badge>
        <SegmentedControl
          label="Sheet interaction"
          value={mode}
          onValueChange={setMode}
          options={[
            { value: 'select', label: 'Edit' },
            { value: 'wire', label: 'Wire' },
            { value: 'interact', label: 'Operate' },
            { value: 'probe', label: 'Probe' },
          ]}
        />
        <Button
          size="sm"
          variant="ghost"
          disabled={!writable}
          onClick={guard(() =>
            edit('Route active sheet', (s) => {
              s.layout = routeSheet(
                { ...view, components: s.components, nets: s.nets, layout: s.layout },
                store.sceneCatalog,
              ).layout;
            }),
          )}
        >
          Route sheet
        </Button>
        <IconButton label="Fit active sheet" size="sm" variant="ghost" onClick={() => canvas.current?.zoomToFit()}>
          <Maximize2 />
        </IconButton>
        <IconButton
          label="Rotate sheet selection"
          size="sm"
          variant="ghost"
          disabled={!writable || !selection.components.length}
          onClick={guard(() => rotate(selection.components))}
        >
          <RotateCw />
        </IconButton>
        <IconButton
          label="Delete sheet selection"
          size="sm"
          variant="ghost"
          disabled={!writable || (!selection.components.length && !selection.wires.length)}
          onClick={guard(async () => {
            await remove(selection);
            select(EMPTY_SELECTION);
          })}
        >
          <Trash2 />
        </IconButton>
        <IconButton
          label="Undo sheet edit"
          size="sm"
          variant="ghost"
          disabled={store.busy || !store.commands.canUndo}
          onClick={guard(() => store.commands.undo())}
        >
          <Undo2 />
        </IconButton>
        <IconButton
          label="Redo sheet edit"
          size="sm"
          variant="ghost"
          disabled={store.busy || !store.commands.canRedo}
          onClick={guard(() => store.commands.redo())}
        >
          <Redo2 />
        </IconButton>
      </div>
      <DockLayout direction="row" items={items} storageKey="embsim.layout.hierarchy" />
    </div>
  );
}
