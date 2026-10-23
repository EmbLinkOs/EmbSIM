// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { captureToPcap, gatherCapture, parseBleList, parseCapture, parseWifiList, signalBars, summarizeFrame } from './air';

// lines as the engine's parts write them (tests/unit/test_engine.c checks the same)
const WIFI = '02:e5:00:00:00:01 ch 6 -20dBm WPA2 "Lab"\n02:e5:00:00:00:02 ch 11 -32dBm Open "Guest"\n02:e5:00:00:00:03 ch 1 -91dBm WPA3 "" hidden\n';
const BLE =
  '7c:df:a1:45:6d:64 (public) ADV_IND -40dBm connectable "nimble-bleprph"\n' +
  'c0:11:22:33:44:55 (public) ADV_IND -40dBm connectable "" iBeacon 2D7A9F0C-E0E8-4CC9-A71B-A21DB2D034A1 major 5 minor 88 -59dBm@1m\n';
const CAPTURE =
  '1 1192000000 b 2402 -40 23 d6be898e000e646d45a1df7c0201060409746167bec180\n' +
  '2 3304000000 w 2437 -20 30 0802000001020304050602e50000000202e5000000021000aabbd9ff642a\n';

describe('the air panel parsers', () => {
  it('reads a Wi-Fi analyzer list', () => {
    const n = parseWifiList(WIFI);
    expect(n).toHaveLength(3);
    expect(n[0]).toEqual({ bssid: '02:e5:00:00:00:01', channel: 6, rssi: -20, security: 'WPA2', ssid: 'Lab', hidden: false });
    expect(n[2]!.hidden).toBe(true);
    expect(signalBars(-20)).toBe(4);
    expect(signalBars(-91)).toBe(0);
  });

  it('reads a BLE scanner list with an iBeacon', () => {
    const d = parseBleList(BLE);
    expect(d).toHaveLength(2);
    expect(d[0]).toMatchObject({ address: '7c:df:a1:45:6d:64', random: false, pdu: 'ADV_IND', rssi: -40, connectable: true, name: 'nimble-bleprph' });
    expect(d[1]!.beacon).toEqual({ uuid: '2D7A9F0C-E0E8-4CC9-A71B-A21DB2D034A1', major: 5, minor: 88, power: -59 });
  });

  it('reads a capture and dissects its frames', () => {
    const f = parseCapture(CAPTURE);
    expect(f).toHaveLength(2);
    expect(f[0]).toMatchObject({ seq: 1, atPs: 1192000000n, kind: 'ble', mhz: 2402, rssi: -40, length: 23 });
    expect(summarizeFrame(f[0]!)).toEqual({ protocol: 'LE LL', source: '7c:df:a1:45:6d:64', destination: 'Broadcast', info: 'ADV_IND, "tag"' });
    expect(summarizeFrame(f[1]!)).toMatchObject({ protocol: '802.11', source: '02:e5:00:00:00:02', destination: '01:02:03:04:05:06', info: 'Data' });
  });

  it('names a beacon by its SSID and an IPv4 packet by its ports', () => {
    const beacon = new Uint8Array([0x80, 0, 0, 0, 255, 255, 255, 255, 255, 255, 2, 0xe5, 0, 0, 0, 1, 2, 0xe5, 0, 0, 0, 1, 0, 0, ...new Array(12).fill(0), 0, 3, 76, 97, 98]);
    expect(summarizeFrame({ seq: 1, atPs: 0n, kind: 'wifi', mhz: 2437, rssi: -30, length: beacon.length, data: beacon })).toEqual({
      protocol: '802.11',
      source: '02:e5:00:00:00:01',
      destination: 'Broadcast',
      info: 'Beacon, SSID="Lab"',
    });
    const syn = new Uint8Array([
      0x08, 0x01, 0, 0, 2, 0xe5, 0, 0, 0, 1, 0x58, 0xcf, 0x79, 0, 0, 1, 2, 0xe5, 0, 0, 0, 1, 0, 0,
      0xaa, 0xaa, 0x03, 0, 0, 0, 0x08, 0x00,
      0x45, 0, 0, 40, 0, 1, 0, 0, 64, 6, 0, 0, 192, 168, 1, 2, 93, 184, 215, 14,
      0xc0, 0x00, 0, 80, 0, 0, 0, 1, 0, 0, 0, 0, 0x50, 0x02, 0x16, 0x80, 0, 0, 0, 0,
    ]);
    expect(summarizeFrame({ seq: 1, atPs: 0n, kind: 'wifi', mhz: 2437, rssi: -30, length: syn.length, data: syn })).toMatchObject({
      protocol: 'HTTP/TCP',
      info: '192.168.1.2:49152 → 93.184.215.14:80 [SYN]',
    });
  });

  it('names the 4-way handshake\'s messages', () => {
    // a data frame from the AP with LLC/SNAP and EAPOL-Key: Key Information 0x008a (Pairwise, Ack: message 1)
    const head = [0x08, 0x02, 0, 0, 0x7c, 0xdf, 0xa1, 0x45, 0x6d, 0x62, 2, 0xe5, 0, 0, 0, 2, 2, 0xe5, 0, 0, 0, 2, 0, 0, 0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e];
    const msg = (info: number) => {
      const d = new Uint8Array([...head, 2, 3, 0, 95, 2, info >> 8, info & 255]);
      return summarizeFrame({ seq: 1, atPs: 0n, kind: 'wifi', mhz: 2437, rssi: -30, length: d.length, data: d });
    };
    expect(msg(0x008a)).toMatchObject({ protocol: 'EAPOL', info: 'Key (Message 1 of 4)' });
    expect(msg(0x010a).info).toBe('Key (Message 2 of 4)');
    expect(msg(0x13ca).info).toBe('Key (Message 3 of 4)');
    expect(msg(0x030a).info).toBe('Key (Message 4 of 4)');
  });

  it('gathers new lines and counts the frames it missed', () => {
    const f = parseCapture(CAPTURE);
    let g = gatherCapture([], f.slice(0, 1));
    expect(g.frames).toHaveLength(1);
    g = gatherCapture(g.frames, f); // the first again, then the second
    expect(g.frames.map((x) => x.seq)).toEqual([1, 2]);
    expect(g.missed).toBe(0);
    const later = { ...f[1]!, seq: 7 };
    g = gatherCapture(g.frames, [later]);
    expect(g.missed).toBe(4);
    expect(gatherCapture(g.frames, [{ ...later, seq: 8 }], 2).frames.map((x) => x.seq)).toEqual([7, 8]);
  });

  it('saves pcap files Wireshark reads', () => {
    const f = parseCapture(CAPTURE);
    const ble = captureToPcap(f, 'ble');
    const v = new DataView(ble.buffer);
    expect(v.getUint32(0, true)).toBe(0xa1b23c4d);
    expect(v.getUint32(20, true)).toBe(256);
    expect(v.getUint32(24 + 4, true)).toBe(1192000); // 1.192 ms in ns
    expect(v.getUint32(24 + 8, true)).toBe(10 + 23);
    expect(ble[24 + 16]).toBe(0); // RF channel 0: 2402 MHz
    expect(v.getUint32(24 + 16 + 4, true)).toBe(0x8e89bed6);
    expect(Array.from(ble.subarray(24 + 16 + 10, 24 + 16 + 14))).toEqual([0xd6, 0xbe, 0x89, 0x8e]);
    const wifi = captureToPcap(f, 'wifi');
    const w = new DataView(wifi.buffer);
    expect(w.getUint32(20, true)).toBe(127);
    expect(w.getUint32(24 + 12, true)).toBe(15 + 30);
    expect(w.getUint16(24 + 16 + 2, true)).toBe(15);
    expect(w.getUint16(24 + 16 + 10, true)).toBe(2437);
    expect(w.getInt8(24 + 16 + 14)).toBe(-20);
    expect(wifi.length).toBe(24 + 16 + 15 + 30);
  });
});
