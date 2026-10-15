// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { createClient } from './client';
import {
  base64ToBytes,
  bytesToBase64,
  decodeBinaryFrame,
  decodeMessage,
  encodeBinaryFrame,
  encodeError,
  encodeRequest,
  encodeResult,
  JsonRpcError,
} from './jsonrpc';
import { TransportBase, type Transport } from './transport';

describe('json-rpc framing', () => {
  it('encodes requests with ids and params', () => {
    expect(JSON.parse(encodeRequest(7, 'control.run', { speed: 1 }))).toEqual({
      jsonrpc: '2.0',
      id: 7,
      method: 'control.run',
      params: { speed: 1 },
    });
    expect(JSON.parse(encodeRequest('a', 'session.close', undefined))).toEqual({ jsonrpc: '2.0', id: 'a', method: 'session.close' });
  });

  it('classifies frames', () => {
    expect(decodeMessage(encodeResult(1, { ok: true })).kind).toBe('result');
    expect(decodeMessage(encodeError(1, -32601, 'unknown method')).kind).toBe('error');
    expect(decodeMessage('{"jsonrpc":"2.0","method":"observe.update","params":{"now_ps":"1"}}').kind).toBe('notification');
    expect(decodeMessage('{"jsonrpc":"2.0","id":3,"method":"x"}').kind).toBe('request');
    expect(decodeMessage('[]').kind).toBe('invalid');
    expect(decodeMessage('{"id":1,"result":2}').kind).toBe('invalid');
    expect(decodeMessage('nope').kind).toBe('invalid');
  });

  it('round-trips binary frame headers', () => {
    const samples = new Uint8Array([0, 1, 2, 3]);
    const frame = encodeBinaryFrame(42, 9, 2n ** 60n + 5n, samples);
    const decoded = decodeBinaryFrame(frame);
    expect(decoded.streamId).toBe(42);
    expect(decoded.seq).toBe(9);
    expect(decoded.firstTimePs).toBe(2n ** 60n + 5n);
    expect(Array.from(new Uint8Array(decoded.samples))).toEqual([0, 1, 2, 3]);
    expect(() => decodeBinaryFrame(new ArrayBuffer(3))).toThrow();
  });

  it('round-trips base64', () => {
    for (const len of [0, 1, 2, 3, 4, 5, 31]) {
      const bytes = new Uint8Array(len).map((_, i) => (i * 37 + 11) & 0xff);
      const text = bytesToBase64(bytes);
      expect(text).toBe(Buffer.from(bytes).toString('base64'));
      expect(Array.from(base64ToBytes(text))).toEqual(Array.from(bytes));
    }
  });
});

/** A loopback transport that answers requests through a handler. */
class LoopbackTransport extends TransportBase implements Transport {
  readonly label = 'loopback';
  readonly sent: string[] = [];
  constructor(private readonly handler: (frame: string, reply: (text: string) => void) => void) {
    super();
  }
  async connect(): Promise<void> {
    this.setState('open');
  }
  close(): void {
    this.setState('closed');
  }
  send(frame: string): void {
    this.sent.push(frame);
    this.handler(frame, (text) => this.emitText(text));
  }
  push(text: string): void {
    this.emitText(text);
  }
}

describe('session client', () => {
  it('matches responses to requests by id and surfaces errors', async () => {
    const transport = new LoopbackTransport((frame, reply) => {
      const req = JSON.parse(frame) as { id: number; method: string; params: unknown };
      if (req.method === 'control.pause') reply(encodeResult(req.id, { now_ps: '123' }));
      else reply(encodeError(req.id, -32601, 'unknown method'));
    });
    const client = createClient(transport, { timeoutMs: 0 });
    await client.connect();
    const a = client.control.pause({});
    const b = client.call('session.info', {});
    await expect(a).resolves.toEqual({ now_ps: '123' });
    await expect(b).rejects.toBeInstanceOf(JsonRpcError);
    expect(transport.sent.map((f) => (JSON.parse(f) as { id: number }).id)).toEqual([1, 2]);
  });

  it('dispatches notifications to listeners', async () => {
    const transport = new LoopbackTransport(() => undefined);
    const client = createClient(transport);
    await client.connect();
    const seen: string[] = [];
    client.on('observe.update', (p) => seen.push(p.now_ps));
    transport.push(JSON.stringify({ jsonrpc: '2.0', method: 'observe.update', params: { now_ps: '99' } }));
    expect(seen).toEqual(['99']);
  });

  it('rejects pending requests when the transport closes', async () => {
    const transport = new LoopbackTransport(() => undefined);
    const client = createClient(transport, { timeoutMs: 0 });
    await client.connect();
    const p = client.control.pause({});
    transport.close();
    await expect(p).rejects.toThrow(/closed/);
  });
});
