// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// The air as the bench's receivers report it (docs/engineering/c3-radios.md,
// step 7): a phone's Wi-Fi list (emblink/wifi_scanner), a phone's BLE scanner
// (emblink/ble_scanner) and a sniffer's capture (emblink/air_sniffer). Each
// is read from the part's outputs; nothing here hears the air itself. A
// capture is dissected as Wireshark's packet list summarises it and saved as
// a pcap file Wireshark opens: radiotap (LINKTYPE 127) for Wi-Fi,
// LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR (256) for Bluetooth LE.

export interface WifiNetwork {
  bssid: string;
  channel: number;
  rssi: number;
  security: string;
  ssid: string;
  hidden: boolean;
}

/** A Wi-Fi analyzer's `list`: one network a line, the strongest first. */
export function parseWifiList(text: string): WifiNetwork[] {
  const out: WifiNetwork[] = [];
  for (const line of text.split('\n')) {
    const m = /^([0-9a-f:]{17}) ch (\d+) (-?\d+)dBm (\S+) "(.*)"( hidden)?$/.exec(line);
    if (m) out.push({ bssid: m[1]!, channel: Number(m[2]), rssi: Number(m[3]), security: m[4]!, ssid: m[5]!, hidden: m[6] !== undefined });
  }
  return out;
}

export interface BleDevice {
  address: string;
  random: boolean;
  pdu: string;
  rssi: number;
  connectable: boolean;
  name: string;
  beacon?: { uuid: string; major: number; minor: number; power: number };
}

/** A BLE scanner's `list`: one advertiser a line. */
export function parseBleList(text: string): BleDevice[] {
  const out: BleDevice[] = [];
  for (const line of text.split('\n')) {
    const m = /^([0-9a-f:]{17}) \((public|random)\) (\S+) (-?\d+)dBm (connectable|-) "(.*?)"(?: iBeacon ([0-9A-F-]{36}) major (\d+) minor (\d+) (-?\d+)dBm@1m)?$/.exec(line);
    if (!m) continue;
    const d: BleDevice = { address: m[1]!, random: m[2] === 'random', pdu: m[3]!, rssi: Number(m[4]), connectable: m[5] === 'connectable', name: m[6]! };
    if (m[7]) d.beacon = { uuid: m[7], major: Number(m[8]), minor: Number(m[9]), power: Number(m[10]) };
    out.push(d);
  }
  return out;
}

/** The signal as a phone's bars show it: 0 to 4. */
export function signalBars(rssi: number): number {
  return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -89 ? 1 : 0;
}

export type AirKind = 'wifi' | 'ble';

export interface CapturedFrame {
  seq: number;
  /** when its last bit was on the air, in picoseconds */
  atPs: bigint;
  kind: AirKind;
  mhz: number;
  rssi: number;
  /** its whole length on the air */
  length: number;
  /** its first bytes, as the sniffer's snaplen kept them */
  data: Uint8Array;
}

function hexBytes(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length >> 1);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(2 * i, 2 * i + 2), 16);
  return out;
}

/** A sniffer's `capture`: one frame a line, "seq time_ps w|b MHz RSSI length hex". */
export function parseCapture(text: string): CapturedFrame[] {
  const out: CapturedFrame[] = [];
  for (const line of text.split('\n')) {
    const m = /^(\d+) (\d+) ([wb]) (\d+) (-?\d+) (\d+) ([0-9a-f]*)$/.exec(line);
    if (m) out.push({ seq: Number(m[1]), atPs: BigInt(m[2]!), kind: m[3] === 'b' ? 'ble' : 'wifi', mhz: Number(m[4]), rssi: Number(m[5]), length: Number(m[6]), data: hexBytes(m[7]!) });
  }
  return out;
}

/** Merges a capture's latest lines into the frames gathered so far: the new
 * ones are appended, `missed` counts those that left the part's window
 * before they were read, `limit` keeps the newest. */
export function gatherCapture(frames: CapturedFrame[], latest: CapturedFrame[], limit = 5000): { frames: CapturedFrame[]; missed: number } {
  const last = frames.length ? frames[frames.length - 1]!.seq : 0;
  const fresh = latest.filter((f) => f.seq > last);
  if (fresh.length === 0) return { frames, missed: 0 };
  const missed = Math.max(0, fresh[0]!.seq - last - 1);
  const all = frames.concat(fresh);
  return { frames: all.length > limit ? all.slice(all.length - limit) : all, missed };
}

export interface FrameSummary {
  protocol: string;
  source: string;
  destination: string;
  info: string;
}

const mac = (d: Uint8Array, at: number) =>
  Array.from(d.subarray(at, at + 6), (b) => b.toString(16).padStart(2, '0')).join(':');
/** a BLE address: little-endian on the air */
const bleAddr = (d: Uint8Array, at: number) =>
  Array.from(d.subarray(at, at + 6), (b) => b.toString(16).padStart(2, '0')).reverse().join(':');
const ip = (d: Uint8Array, at: number) => Array.from(d.subarray(at, at + 4)).join('.');
const BROADCAST = 'ff:ff:ff:ff:ff:ff';

function ssidOf(d: Uint8Array, at: number): string | null {
  for (let i = at; i + 2 <= d.length; i += 2 + d[i + 1]!) {
    if (d[i] === 0) {
      const n = d[i + 1]!;
      if (i + 2 + n > d.length) return null;
      return n === 0 ? '' : new TextDecoder().decode(d.subarray(i + 2, i + 2 + n));
    }
  }
  return null;
}

const MGMT: Record<number, string> = {
  0: 'Association Request',
  1: 'Association Response',
  2: 'Reassociation Request',
  3: 'Reassociation Response',
  4: 'Probe Request',
  5: 'Probe Response',
  8: 'Beacon',
  10: 'Disassociation',
  11: 'Authentication',
  12: 'Deauthentication',
  13: 'Action',
};

function summarizeIp(d: Uint8Array, at: number): Pick<FrameSummary, 'protocol' | 'info'> {
  if (at + 20 > d.length) return { protocol: 'IPv4', info: 'IPv4 (cut by the snapshot length)' };
  const hl = (d[at]! & 15) * 4;
  const proto = d[at + 9]!;
  const src = ip(d, at + 12);
  const dst = ip(d, at + 16);
  const l4 = at + hl;
  const port = (o: number) => (l4 + o + 2 <= d.length ? (d[l4 + o]! << 8) | d[l4 + o + 1]! : NaN);
  if (proto === 6) {
    const flags = l4 + 13 < d.length ? d[l4 + 13]! : 0;
    const names = [['SYN', 2], ['ACK', 16], ['FIN', 1], ['RST', 4], ['PSH', 8]] as const;
    const set = names.filter(([, b]) => flags & b).map(([n]) => n);
    const sp = port(0);
    const dp = port(2);
    const http = sp === 80 || dp === 80;
    return { protocol: http ? 'HTTP/TCP' : 'TCP', info: `${src}:${sp} → ${dst}:${dp} [${set.join(', ')}]` };
  }
  if (proto === 17) {
    const sp = port(0);
    const dp = port(2);
    const what = sp === 53 || dp === 53 ? 'DNS' : sp === 67 || sp === 68 || dp === 67 || dp === 68 ? 'DHCP' : 'UDP';
    return { protocol: what, info: `${src}:${sp} → ${dst}:${dp}` };
  }
  if (proto === 1) {
    const type = l4 < d.length ? d[l4]! : -1;
    return { protocol: 'ICMP', info: `${src} → ${dst} ${type === 8 ? 'Echo (ping) request' : type === 0 ? 'Echo (ping) reply' : `type ${type}`}` };
  }
  return { protocol: 'IPv4', info: `${src} → ${dst} protocol ${proto}` };
}

/** an EAPOL-Key frame's place in the 4-way handshake, from its Key
 * Information (IEEE 802.11-2020 12.7.2): Ack without MIC is message 1,
 * Ack with MIC 3; MIC without Ack is 4 when Secure, else 2 */
function eapolInfo(d: Uint8Array, at: number): string {
  if (at + 7 > d.length || d[at + 1] !== 3) return 'EAPOL';
  const info = (d[at + 5]! << 8) | d[at + 6]!;
  if (!(info & 0x0008)) return 'Key (Group Message)';
  const ack = (info & 0x0080) !== 0;
  const mic = (info & 0x0100) !== 0;
  const secure = (info & 0x0200) !== 0;
  const n = ack ? (mic ? 3 : 1) : mic ? (secure ? 4 : 2) : 0;
  return n ? `Key (Message ${n} of 4)` : 'Key';
}

function summarizeWifi(d: Uint8Array): FrameSummary {
  if (d.length < 10) return { protocol: '802.11', source: '', destination: '', info: 'Malformed' };
  const type = (d[0]! >> 2) & 3;
  const subtype = d[0]! >> 4;
  const flags = d[1]!;
  const destination = mac(d, 4);
  const source = d.length >= 16 ? mac(d, 10) : '';
  if (type === 0) {
    const name = MGMT[subtype] ?? `Management ${subtype}`;
    const body = subtype === 8 || subtype === 5 ? 36 : subtype === 4 ? 24 : subtype === 0 ? 28 : subtype === 2 ? 34 : -1;
    const ssid = body >= 0 ? ssidOf(d, body) : null;
    const info = ssid === null ? name : `${name}, SSID=${ssid === '' ? (subtype === 4 ? 'Wildcard (Broadcast)' : '(hidden)') : `"${ssid}"`}`;
    return { protocol: '802.11', source, destination: destination === BROADCAST ? 'Broadcast' : destination, info: flags & 0x40 ? `${info} (protected)` : info };
  }
  if (type === 1) return { protocol: '802.11', source, destination, info: `Control ${subtype}` };
  if (type === 2) {
    const qos = (subtype & 8) !== 0;
    const nodata = (subtype & 4) !== 0;
    const name = `${qos ? 'QoS ' : ''}${nodata ? 'Null function' : 'Data'}`;
    const dest = destination === BROADCAST ? 'Broadcast' : destination;
    if (nodata) return { protocol: '802.11', source, destination: dest, info: name };
    if (flags & 0x40) return { protocol: '802.11', source, destination: dest, info: `${name}, protected` };
    const llc = 24 + (qos ? 2 : 0) + ((flags & 3) === 3 ? 6 : 0);
    if (llc + 8 <= d.length && d[llc] === 0xaa && d[llc + 1] === 0xaa && d[llc + 2] === 0x03) {
      const ethertype = (d[llc + 6]! << 8) | d[llc + 7]!;
      const at = llc + 8;
      if (ethertype === 0x888e) return { protocol: 'EAPOL', source, destination: dest, info: eapolInfo(d, at) };
      if (ethertype === 0x0806 && at + 28 <= d.length)
        return { protocol: 'ARP', source, destination: dest, info: d[at + 7] === 1 ? `Who has ${ip(d, at + 24)}? Tell ${ip(d, at + 14)}` : `${ip(d, at + 14)} is at ${mac(d, at + 8)}` };
      if (ethertype === 0x0800) return { source, destination: dest, ...summarizeIp(d, at) };
      return { protocol: '802.11', source, destination: dest, info: `${name}, ethertype 0x${ethertype.toString(16).padStart(4, '0')}` };
    }
    return { protocol: '802.11', source, destination: dest, info: name };
  }
  return { protocol: '802.11', source, destination, info: 'Reserved' };
}

const BLE_PDU: Record<number, string> = {
  0: 'ADV_IND',
  1: 'ADV_DIRECT_IND',
  2: 'ADV_NONCONN_IND',
  3: 'SCAN_REQ',
  4: 'SCAN_RSP',
  5: 'CONNECT_IND',
  6: 'ADV_SCAN_IND',
  7: 'ADV_EXT_IND',
};

function bleName(d: Uint8Array, at: number, end: number): string | null {
  let short: string | null = null;
  for (let i = at; i + 1 < end; ) {
    const l = d[i]!;
    if (!l || i + 1 + l > end) break;
    const t = d[i + 1]!;
    const text = () => new TextDecoder().decode(d.subarray(i + 2, i + 1 + l));
    if (t === 0x09) return text();
    if (t === 0x08) short = text();
    i += 1 + l;
  }
  return short;
}

function summarizeBle(d: Uint8Array): FrameSummary {
  if (d.length < 6 + 6) return { protocol: 'LE LL', source: '', destination: '', info: 'Malformed' };
  const h = d[4]!;
  const type = h & 15;
  const len = d[5]!;
  const name = BLE_PDU[type] ?? `PDU type ${type}`;
  const end = Math.min(d.length, 6 + len);
  if (type === 3 || type === 5) return { protocol: 'LE LL', source: bleAddr(d, 6), destination: bleAddr(d, 12), info: name };
  const source = bleAddr(d, 6);
  const destination = type === 1 ? bleAddr(d, 12) : 'Broadcast';
  const n = type === 1 ? null : bleName(d, 12, end);
  return { protocol: 'LE LL', source, destination, info: n ? `${name}, "${n}"` : name };
}

/** A frame as Wireshark's packet list summarises it. */
export function summarizeFrame(f: CapturedFrame): FrameSummary {
  return f.kind === 'ble' ? summarizeBle(f.data) : summarizeWifi(f.data);
}

/** The frames as a pcap file (nanosecond timestamps) of one link type. */
export function captureToPcap(frames: CapturedFrame[], kind: AirKind): Uint8Array {
  const linktype = kind === 'ble' ? 256 : 127;
  const hdr = kind === 'ble' ? 10 : 15;
  const ours = frames.filter((f) => f.kind === kind);
  const size = 24 + ours.reduce((n, f) => n + 16 + hdr + f.data.length, 0);
  const out = new Uint8Array(size);
  const v = new DataView(out.buffer);
  v.setUint32(0, 0xa1b23c4d, true); // nanosecond resolution
  v.setUint16(4, 2, true);
  v.setUint16(6, 4, true);
  v.setUint32(16, 65535, true);
  v.setUint32(20, linktype, true);
  let at = 24;
  for (const f of ours) {
    const ns = f.atPs / 1000n;
    v.setUint32(at, Number(ns / 1_000_000_000n), true);
    v.setUint32(at + 4, Number(ns % 1_000_000_000n), true);
    v.setUint32(at + 8, hdr + f.data.length, true);
    v.setUint32(at + 12, hdr + f.length, true);
    at += 16;
    if (kind === 'wifi') {
      // radiotap: Flags (FCS at the end), Channel (2 GHz), dBm antenna signal
      v.setUint16(at + 2, 15, true);
      v.setUint32(at + 4, 0x2a, true);
      out[at + 8] = 0x10;
      v.setUint16(at + 10, f.mhz, true);
      v.setUint16(at + 12, 0x0080, true);
      v.setInt8(at + 14, f.rssi);
    } else {
      // the RF channel (0 to 39 from 2402 MHz), the signal power, the
      // advertising access address, the flags: dewhitened, signal power
      // and reference access address valid, CRC checked and valid, LE 1M
      out[at] = Math.round((f.mhz - 2402) / 2);
      v.setInt8(at + 1, f.rssi);
      v.setInt8(at + 2, -128);
      v.setUint32(at + 4, 0x8e89bed6, true);
      v.setUint16(at + 8, 0x0c13, true);
    }
    at += hdr;
    out.set(f.data, at);
    at += f.data.length;
  }
  return out;
}
