// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The /gallery route: every design-system component, the scene and the
// instruments rendered with sample data for visual review in both themes.

import {
  Badge,
  Button,
  CommandPalette,
  Dialog,
  DockLayout,
  EmptyState,
  IconButton,
  Input,
  Kbd,
  Panel,
  SegmentedControl,
  Select,
  SignalChip,
  Slider,
  StatusBar,
  Table,
  Tabs,
  Toggle,
  ToggleButton,
  Tooltip,
  useTheme,
  useToast,
} from '@embsim/design';
import { LogicAnalyzer, Meter, Oscilloscope, SerialTerminal, WaveformRecord, type TimeWindow } from '@embsim/instruments';
import { demoProject, MOCK_CATALOG } from '@embsim/protocol';
import { BoardView, EMPTY_SELECTION, SchematicCanvas, type SceneCatalog, type Selection } from '@embsim/scene';
import { Info, Moon, Pin, Play, Sun } from 'lucide-react';
import { useMemo, useState } from 'react';

const TOKENS = [
  '--surface',
  '--surface-raised',
  '--surface-sunken',
  '--line',
  '--text',
  '--text-muted',
  '--accent',
  '--signal-low',
  '--signal-high',
  '--signal-z',
  '--signal-x',
  '--signal-analog',
  '--danger',
  '--warning',
  '--info',
];

function sampleRecords() {
  const btn = new WaveformRecord('btn', 'digital');
  const coll = new WaveformRecord('coll', 'analog', 'coll', 'V');
  const n = 4000;
  const d = new Uint8Array(n);
  const a = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const pressed = Math.floor(i / 500) % 2 === 1;
    d[i] = i > 3800 ? 2 : pressed ? 0 : 1;
    const edge = i % 500;
    const target = pressed ? 3.3 : 0.15;
    const from = pressed ? 0.15 : 3.3;
    a[i] = target + (from - target) * Math.exp(-edge / 40);
  }
  btn.append(0n, 5_000_000n, d);
  coll.append(0n, 5_000_000n, a);
  return { btn, coll };
}

export function Gallery() {
  const theme = useTheme();
  const { toast } = useToast();
  const [seg, setSeg] = useState('a');
  const [toggle, setToggle] = useState(true);
  const [pressed, setPressed] = useState(false);
  const [select, setSelect] = useState('electrical');
  const [slider, setSlider] = useState(40);
  const [tab, setTab] = useState('one');
  const [dialog, setDialog] = useState(false);
  const [palette, setPalette] = useState(false);
  const [win, setWin] = useState<TimeWindow>({ start: 0n, span: 20_000_000_000n, follow: false });
  const [cursors, setCursors] = useState<{ a: bigint | null; b: bigint | null }>({ a: 2_500_000_000n, b: 5_000_000_000n });
  const [selection, setSelection] = useState<Selection>(EMPTY_SELECTION);
  const [buttonDown, setButtonDown] = useState(false);
  const records = useMemo(sampleRecords, []);
  const project = useMemo(demoProject, []);
  const catalog = useMemo<SceneCatalog>(() => {
    const out: SceneCatalog = {};
    for (const [id, e] of Object.entries(MOCK_CATALOG)) out[id] = { name: e.description.name, symbol: e.description.symbol, svg: e.files.symbol ?? '' };
    return out;
  }, []);
  const liveStates = { '3V3': { logic: '1' as const, voltage: 3.3 }, GND: { logic: '0' as const, voltage: 0 }, BTN: { logic: '1' as const, voltage: 3.3 }, COLL: { voltage: 0.15 }, Q1B: { voltage: 0.7 }, LED_K: { voltage: 2.05 }, N9: { logic: 'Z' as const } };

  return (
    <div className="wb-gallery">
      <div className="wb-gallery__inner">
        <header className="es-row" style={{ justifyContent: 'space-between' }}>
          <h1>EmbSIM design system gallery</h1>
          <div className="es-row">
            <Badge>theme: {theme.resolved}</Badge>
            <IconButton label="Toggle theme" onClick={theme.toggle}>
              {theme.resolved === 'dark' ? <Sun /> : <Moon />}
            </IconButton>
            <a href="./">Back to the workbench</a>
          </div>
        </header>

        <section className="wb-gallery__section" aria-labelledby="g-tokens">
          <h2 id="g-tokens">Tokens</h2>
          <div className="wb-gallery__swatches">
            {TOKENS.map((t) => (
              <div key={t} className="wb-gallery__swatch">
                <div className="wb-gallery__swatch-color" style={{ background: `var(${t})` }} />
                <code>{t}</code>
              </div>
            ))}
          </div>
          <div className="wb-gallery__row">
            <SignalChip kind="low" />
            <SignalChip kind="high" />
            <SignalChip kind="z" />
            <SignalChip kind="x" />
            <SignalChip kind="analog" label="2.47 V" />
            <span className="es-mono">mono 12.345 ms</span>
            <Kbd>Ctrl</Kbd>
            <Kbd>K</Kbd>
          </div>
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-buttons">
          <h2 id="g-buttons">Buttons, toggles, segmented control</h2>
          <div className="wb-gallery__row">
            <Button variant="primary" icon={<Play />}>
              Run
            </Button>
            <Button>Default</Button>
            <Button variant="ghost">Ghost</Button>
            <Button variant="danger">Delete</Button>
            <Button size="sm">Small</Button>
            <Button disabled>Disabled</Button>
            <Tooltip content="Icon button with tooltip" shortcut="Ctrl+P">
              <IconButton label="Pin">
                <Pin />
              </IconButton>
            </Tooltip>
            <ToggleButton label="Pressed toggle" pressed={pressed} onPressedChange={setPressed}>
              <Pin />
            </ToggleButton>
            <Toggle label="Bounce" checked={toggle} onCheckedChange={setToggle} />
            <SegmentedControl
              label="Example"
              value={seg}
              onValueChange={setSeg}
              options={[
                { value: 'a', label: 'Design' },
                { value: 'b', label: 'Run' },
                { value: 'c', label: 'Debug' },
              ]}
            />
            <Badge tone="accent">accent</Badge>
            <Badge tone="warning">warning</Badge>
            <Badge tone="danger">danger</Badge>
            <Badge tone="info">info</Badge>
          </div>
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-inputs">
          <h2 id="g-inputs">Inputs, select, slider</h2>
          <div className="wb-gallery__row">
            <Input label="Resistance" unit="Ω" mono defaultValue="4.7k" hint="SI suffixes accepted" />
            <Input label="Bounce" unit="ms" mono defaultValue="2" error="Out of range (max 50)" />
            <Input aria-label="Search" placeholder="Search…" />
            <div className="es-field">
              <span className="es-field__label">Policy</span>
              <Select
                label="Policy"
                value={select}
                onValueChange={setSelect}
                options={[
                  { value: 'functional', label: 'Functional' },
                  { value: 'timed', label: 'Timed' },
                  { value: 'electrical', label: 'Electrical' },
                ]}
              />
            </div>
            <div style={{ width: 240 }}>
              <Slider label="Potentiometer" min={0} max={100} value={slider} onValueChange={setSlider} format={(v) => `${v} %`} />
            </div>
          </div>
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-panels">
          <h2 id="g-panels">Tabs, panel, table, tree-like list, empty state, dock layout</h2>
          <div className="wb-gallery__tall">
            <DockLayout
              direction="row"
              items={[
                {
                  id: 'a',
                  title: 'Tabs',
                  flush: true,
                  content: (
                    <Tabs
                      label="Example tabs"
                      value={tab}
                      onValueChange={setTab}
                      items={[
                        { value: 'one', label: 'Nets', content: <div style={{ padding: 12 }}>Tab content one</div> },
                        { value: 'two', label: 'Board', content: <div style={{ padding: 12 }}>Tab content two</div> },
                      ]}
                    />
                  ),
                },
                {
                  id: 'b',
                  title: 'Table',
                  flush: true,
                  content: (
                    <Table
                      label="Example"
                      columns={[
                        { id: 'net', header: 'Net', mono: true, cell: (r: { net: string; state: string }) => r.net },
                        { id: 'state', header: 'State', cell: (r) => r.state },
                      ]}
                      rows={[
                        { net: 'BTN', state: 'high' },
                        { net: 'COLL', state: '0.15 V' },
                      ]}
                      rowKey={(r) => r.net}
                      onSelect={() => undefined}
                    />
                  ),
                },
                { id: 'c', title: 'Empty', content: <EmptyState icon={<Info />} title="Nothing here">An empty state with guidance.</EmptyState> },
              ]}
            />
          </div>
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-overlays">
          <h2 id="g-overlays">Dialog, toast, command palette, status bar</h2>
          <div className="wb-gallery__row">
            <Button onClick={() => setDialog(true)}>Open dialog</Button>
            <Button onClick={() => toast({ kind: 'success', title: 'Saved', description: 'project.json written' })}>Success toast</Button>
            <Button onClick={() => toast({ kind: 'warning', title: 'net.contention', description: 'two drivers disagree on BTN' })}>Warning toast</Button>
            <Button onClick={() => toast({ kind: 'error', title: 'Request failed', description: 'policy can only change while paused (-32016)' })}>Error toast</Button>
            <Button onClick={() => setPalette(true)}>Command palette</Button>
          </div>
          <StatusBar
            items={[
              { id: 'a', label: 'Engine', value: '0.1.0 · protocol 1.0' },
              { id: 'b', label: 'Timing', value: 'B1: cycle' },
              { id: 's', spacer: true },
              { id: 'c', label: 'Latency', value: '0.8 ms' },
            ]}
          />
          <Dialog open={dialog} onOpenChange={setDialog} title="Rename net" description="Renames the net everywhere it is referenced." footer={<Button variant="primary" onClick={() => setDialog(false)}>Rename</Button>}>
            <Input label="Name" mono defaultValue="BTN" />
          </Dialog>
          <CommandPalette
            open={palette}
            onOpenChange={setPalette}
            commands={[
              { id: '1', label: 'Run', group: 'Control', shortcut: 'F5', run: () => undefined },
              { id: '2', label: 'Pause', group: 'Control', run: () => undefined },
              { id: '3', label: 'Zoom to fit', group: 'Schematic', shortcut: '0', run: () => undefined },
            ]}
          />
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-scene">
          <h2 id="g-scene">Schematic canvas (live colouring) and board view</h2>
          <div className="wb-gallery__canvas">
            <SchematicCanvas project={project} catalog={catalog} mode="select" selection={selection} onSelectionChange={setSelection} live netStates={liveStates} readOnly label="Gallery schematic" />
          </div>
          <div style={{ maxWidth: 420 }}>
            <BoardView
              items={[
                {
                  id: 'SW1',
                  label: 'SW1 Push button',
                  svg: MOCK_CATALOG['emblink/push_button']!.files.artwork!,
                  x: 10,
                  y: 10,
                  width: 96,
                  height: 96,
                  hotspots: MOCK_CATALOG['emblink/push_button']!.description.artwork!.hotspots,
                  states: MOCK_CATALOG['emblink/push_button']!.description.artwork!.states,
                  inputs: { pressed: buttonDown },
                  outputs: { closed: buttonDown },
                },
                {
                  id: 'D1',
                  label: 'D1 LED',
                  svg: MOCK_CATALOG['emblink/led']!.files.artwork!,
                  x: 140,
                  y: 10,
                  width: 48,
                  height: 96,
                  states: MOCK_CATALOG['emblink/led']!.description.artwork!.states,
                  outputs: { luminance: buttonDown ? 0 : 1, lit: !buttonDown },
                },
              ]}
              height={130}
              onInput={(_, __, v) => setButtonDown(Boolean(v))}
              label="Gallery board"
            />
          </div>
        </section>

        <section className="wb-gallery__section" aria-labelledby="g-instr">
          <h2 id="g-instr">Instruments</h2>
          <div className="wb-gallery__tall">
            <Panel title="Oscilloscope" flush>
              <Oscilloscope traces={[{ record: records.coll, color: '#f0975a' }]} window={win} onWindowChange={setWin} cursors={cursors} onCursorsChange={setCursors} now={18_000_000_000n} />
            </Panel>
          </div>
          <div style={{ height: 160 }}>
            <Panel title="Logic analyzer" flush>
              <LogicAnalyzer channels={[{ record: records.btn }]} window={win} onWindowChange={setWin} cursors={cursors} onCursorsChange={setCursors} now={18_000_000_000n} />
            </Panel>
          </div>
          <div className="wb-gallery__row">
            <Meter label="D1.luminance" value={0.83} track />
            <Meter label="Q1.ic" value={0.0057} unit="A" />
            <Meter label="D1.lit" value tone="accent" />
            <Meter label="net COLL" value={0.152} unit="V" />
          </div>
          <div style={{ height: 180 }}>
            <Panel title="Serial terminal" flush>
              <SerialTerminal target="B1.USART1" targets={['B1.USART1']} lines={[{ at: 5_000_000_000n, direction: 'rx', text: 'ready' }, { at: 7_000_000_000n, direction: 'tx', text: 'hello' }]} onSend={() => undefined} />
            </Panel>
          </div>
        </section>
      </div>
    </div>
  );
}
