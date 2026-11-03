// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The air (docs/engineering/c3-radios.md, step 7) as the bench's receivers
// see it: a phone's Wi-Fi list (emblink/wifi_scanner), a phone's Bluetooth
// LE scanner (emblink/ble_scanner) and a sniffer's capture for Wireshark
// (emblink/air_sniffer). Every row is what one of those parts heard where it
// stands on the bench; the panel hears nothing itself. A part added here is
// added to the project like any other.

import { Button, EmptyState, Select, useToast } from '@embsim/design';
import { captureToPcap, parseBleList, parseWifiList, signalBars, summarizeFrame, type CapturedFrame } from '@embsim/instruments';
import type { Project } from '@embsim/protocol';
import { Bluetooth, Download, Eraser, Radio, Wifi } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { errorMessage } from '../../actions';
import { platform } from '../../platform/platform';
import { useAir } from '../../stores/air';
import { useProject } from '../../stores/project';
import { useSession } from '../../stores/session';
import { t as tr } from '../../i18n';

const WIFI_SCANNER = 'emblink/wifi_scanner';
const BLE_SCANNER = 'emblink/ble_scanner';
const SNIFFER = 'emblink/air_sniffer';
const SHOWN = 400;

function Bars({ rssi }: { rssi: number }) {
  const n = signalBars(rssi);
  return (
    <svg className="wb-air__bars" viewBox="0 0 16 12" width="16" height="12" role="img" aria-label={`${n} of 4 bars`}>
      {[0, 1, 2, 3].map((i) => (
        <rect key={i} x={i * 4} y={9 - i * 3} width="3" height={3 + i * 3} rx="0.5" data-on={i < n ? 'true' : undefined} />
      ))}
    </svg>
  );
}

function hexDump(d: Uint8Array): string {
  const lines: string[] = [];
  for (let i = 0; i < d.length; i += 16) {
    const row = Array.from(d.subarray(i, i + 16));
    const hex = row.map((b) => b.toString(16).padStart(2, '0')).join(' ');
    const text = row.map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('');
    lines.push(`${i.toString(16).padStart(4, '0')}  ${hex.padEnd(47)}  ${text}`);
  }
  return lines.join('\n');
}

const scans = (n: number) => `${n} scan${n === 1 ? '' : 's'}`;
const seconds = (ps: bigint) => (Number(ps / 1000n) / 1e9).toFixed(6);
const channelOf = (f: CapturedFrame) => (f.kind === 'ble' ? (f.mhz === 2402 ? 37 : f.mhz === 2426 ? 38 : f.mhz === 2480 ? 39 : Math.round((f.mhz - 2404) / 2)) : f.mhz === 2484 ? 14 : (f.mhz - 2407) / 5);

function CaptureView({ id }: { id: string }) {
  const capture = useAir((s) => s.captures[id]);
  const clear = useAir((s) => s.clear);
  const radio = useProject((s) => s.project?.components.find((c) => c.id === id)?.params?.radio ?? 'wifi');
  const { toast } = useToast();
  const [selected, setSelected] = useState<number | null>(null);
  const list = useRef<HTMLDivElement>(null);
  const follow = useRef(true);
  const frames = capture?.frames ?? [];
  const shown = frames.length > SHOWN ? frames.slice(frames.length - SHOWN) : frames;
  const rows = useMemo(() => shown.map((f) => ({ f, s: summarizeFrame(f) })), [shown]);
  const chosen = selected === null ? undefined : frames.find((f) => f.seq === selected);

  useEffect(() => {
    if (follow.current && list.current) list.current.scrollTop = list.current.scrollHeight;
  }, [rows]);

  const save = async () => {
    try {
      const kind = radio === 'ble' ? 'ble' : 'wifi';
      const p = await platform();
      const where = await p.saveBinaryFile(`${id}-${kind}.pcap`, captureToPcap(frames, kind), 'application/vnd.tcpdump.pcap');
      if (where) toast({ kind: 'success', title: `Saved ${where}`, description: `${frames.length} frames, for Wireshark` });
    } catch (e) {
      toast({ kind: 'error', title: tr('air.saveFailed'), description: errorMessage(e) });
    }
  };

  return (
    <div className="wb-air__capture">
      <div className="wb-air__bar">
        <span className="wb-air__count">
          {frames.length} frames{capture?.missed ? `, ${capture.missed} missed` : ''}
        </span>
        <Button size="sm" variant="ghost" icon={<Eraser />} onClick={() => clear(id)} disabled={!frames.length}>
          {tr('common.clear')}
        </Button>
        <Button size="sm" variant="ghost" icon={<Download />} onClick={() => void save()} disabled={!frames.length}>
          {tr('air.savePcap')}
        </Button>
      </div>
      <div
        className="wb-air__list"
        ref={list}
        onScroll={(e) => {
          const el = e.currentTarget;
          follow.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24;
        }}
      >
        <table className="es-table wb-air__frames" aria-label={`Frames captured by ${id}`}>
          <thead>
            <tr>
              <th scope="col">No.</th>
              <th scope="col">{tr('air.timeS')}</th>
              <th scope="col">Ch</th>
              <th scope="col">RSSI</th>
              <th scope="col">{tr('air.protocol')}</th>
              <th scope="col">{tr('air.source')}</th>
              <th scope="col">{tr('air.destination')}</th>
              <th scope="col">{tr('air.length')}</th>
              <th scope="col">{tr('air.info')}</th>
            </tr>
          </thead>
          <tbody>
            {rows.length === 0 && (
              <tr>
                <td colSpan={9} className="es-muted">
                  {tr('air.nothingHeard')}
                </td>
              </tr>
            )}
            {rows.map(({ f, s }) => (
              <tr key={f.seq} tabIndex={0} aria-selected={selected === f.seq} data-selected={selected === f.seq ? 'true' : undefined} onClick={() => setSelected(f.seq)} onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && setSelected(f.seq)}>
                <td>{f.seq}</td>
                <td className="es-mono">{seconds(f.atPs)}</td>
                <td>{channelOf(f)}</td>
                <td>{f.rssi}</td>
                <td>{s.protocol}</td>
                <td className="es-mono">{s.source}</td>
                <td className="es-mono">{s.destination}</td>
                <td>{f.length}</td>
                <td>{s.info}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {chosen && (
        <pre className="wb-air__hex" aria-label={`Frame ${chosen.seq}'s bytes`}>
          {hexDump(chosen.data)}
          {chosen.data.length < chosen.length ? `\n(${chosen.length - chosen.data.length} more bytes beyond the snapshot length)` : ''}
        </pre>
      )}
    </div>
  );
}

export function AirPanel() {
  const project = useProject((s) => s.project);
  const editable = useProject((s) => s.editable);
  const outputs = useSession((s) => s.outputs);
  const { toast } = useToast();
  const comps = project?.components ?? [];
  const wifi = comps.filter((c) => c.component === WIFI_SCANNER);
  const ble = comps.filter((c) => c.component === BLE_SCANNER);
  const sniffers = comps.filter((c) => c.component === SNIFFER);
  const [sniffer, setSniffer] = useState<string | null>(null);
  const current = sniffers.find((c) => c.id === sniffer)?.id ?? sniffers[0]?.id;

  const add = (component: string, prefix: string, name: string) => {
    void useProject
      .getState()
      .mutate(`Add a ${name}`, (p: Project) => {
        let n = 1;
        while (p.components.some((c) => c.id === `${prefix}${n}`)) n++;
        p.components.push({ id: `${prefix}${n}`, component });
        return p;
      })
      .catch((e: unknown) => toast({ kind: 'error', title: `Adding a ${name} failed`, description: errorMessage(e) }));
  };
  const adders = editable && project && (
    <div className="wb-air__add">
      {!wifi.length && (
        <Button size="sm" variant="ghost" icon={<Wifi />} onClick={() => add(WIFI_SCANNER, 'PHONE', tr('air.wifiAnalyzer'))}>
          {tr('air.wifiAnalyzer')}
        </Button>
      )}
      {!ble.length && (
        <Button size="sm" variant="ghost" icon={<Bluetooth />} onClick={() => add(BLE_SCANNER, 'SCAN', tr('air.bleScanner'))}>
          {tr('air.bleScanner')}
        </Button>
      )}
      <Button size="sm" variant="ghost" icon={<Radio />} onClick={() => add(SNIFFER, 'SNIFF', 'sniffer')}>
        {tr('air.sniffer')}
      </Button>
    </div>
  );

  if (!project) return <EmptyState icon={<Radio />} title={tr('world.noProject')}>{tr('air.openProject')}</EmptyState>;
  if (!wifi.length && !ble.length && !sniffers.length)
    return (
      <EmptyState icon={<Radio />} title={tr('air.nothingListens')}>
        {tr('air.howTo')}
        for Wireshark. Each hears the air where it stands on the bench.
        {adders}
      </EmptyState>
    );

  return (
    <div className="wb-air" aria-label={tr('air.region')}>
      <section className="wb-air__lists">
        {wifi.map((c) => {
          const nets = parseWifiList(String(outputs[`${c.id}.list`] ?? ''));
          const ch = Number(outputs[`${c.id}.channel`] ?? 0);
          return (
            <div key={c.id}>
              <h3>
                <Wifi aria-hidden="true" /> Wi-Fi, as {c.id} sees it <span className="wb-air__meta">{ch ? `scanning channel ${ch}` : scans(Number(outputs[`${c.id}.scans`] ?? 0))}</span>
              </h3>
              {nets.length === 0 ? (
                <p className="wb-air__hint">{tr('air.noNetwork')}</p>
              ) : (
                <ul className="wb-air__rows">
                  {nets.map((n) => (
                    <li key={n.bssid}>
                      <Bars rssi={n.rssi} />
                      <span className="wb-air__name">{n.hidden ? <em>{tr('air.hidden')}</em> : n.ssid}</span>
                      <span className="wb-air__meta">
                        {n.security} · ch {n.channel} · {n.rssi} dBm
                      </span>
                      <span className="wb-air__meta es-mono">{n.bssid}</span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          );
        })}
        {ble.map((c) => {
          const devs = parseBleList(String(outputs[`${c.id}.list`] ?? ''));
          return (
            <div key={c.id}>
              <h3>
                <Bluetooth aria-hidden="true" /> Bluetooth LE, as {c.id} sees it
              </h3>
              {devs.length === 0 ? (
                <p className="wb-air__hint">{tr('air.noAdvertiser')}</p>
              ) : (
                <ul className="wb-air__rows">
                  {devs.map((d) => (
                    <li key={d.address}>
                      <Bars rssi={d.rssi} />
                      <span className="wb-air__name">{d.name || (d.beacon ? 'iBeacon' : <em>{tr('air.noName')}</em>)}</span>
                      <span className="wb-air__meta">
                        {d.connectable ? 'connectable' : 'not connectable'} · {d.pdu} · {d.rssi} dBm
                      </span>
                      <span className="wb-air__meta es-mono">
                        {d.address} ({d.random ? 'random' : 'public'})
                      </span>
                      {d.beacon && (
                        <span className="wb-air__meta es-mono">
                          {d.beacon.uuid} major {d.beacon.major} minor {d.beacon.minor} · {d.beacon.power} dBm at 1 m
                        </span>
                      )}
                    </li>
                  ))}
                </ul>
              )}
            </div>
          );
        })}
        {adders}
      </section>
      {current && (
        <section className="wb-air__sniffer">
          <h3>
            <Radio aria-hidden="true" /> Capture
            {sniffers.length > 1 ? (
              <Select size="sm" label={tr('air.sniffer')} value={current} onValueChange={setSniffer} options={sniffers.map((s) => ({ value: s.id, label: s.id }))} />
            ) : (
              <span className="wb-air__meta"> {current}</span>
            )}
            <span className="wb-air__meta">
              {(() => {
                const p = sniffers.find((s) => s.id === current)?.params ?? {};
                return p.radio === 'ble' ? `BLE, channel ${Number(p.channel) || 37}` : `Wi-Fi monitor, channel ${Number(p.channel) || 6}`;
              })()}
            </span>
          </h3>
          <CaptureView id={current} />
        </section>
      )}
    </div>
  );
}
