// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// JSON-RPC 2.0 framing (SPEC-008 §1, §3): one JSON object per message, no
// batches. These functions are pure and shared by every transport.

export type JsonRpcId = number | string;

export interface JsonRpcRequest {
  jsonrpc: '2.0';
  id: JsonRpcId;
  method: string;
  params?: unknown;
}

export interface JsonRpcNotification {
  jsonrpc: '2.0';
  method: string;
  params?: unknown;
}

export interface JsonRpcErrorObject {
  code: number;
  message: string;
  data?: unknown;
}

export interface JsonRpcSuccess {
  jsonrpc: '2.0';
  id: JsonRpcId;
  result: unknown;
}

export interface JsonRpcFailure {
  jsonrpc: '2.0';
  id: JsonRpcId | null;
  error: JsonRpcErrorObject;
}

export type JsonRpcResponse = JsonRpcSuccess | JsonRpcFailure;
export type JsonRpcMessage = JsonRpcRequest | JsonRpcNotification | JsonRpcResponse;

/** Standard codes (SPEC-008 §3). Engine statuses are `ENGINE_BASE - es_status`. */
export const JsonRpcCode = {
  PARSE_ERROR: -32700,
  INVALID_REQUEST: -32600,
  METHOD_NOT_FOUND: -32601,
  INVALID_PARAMS: -32602,
  INTERNAL: -32603,
  ENGINE_BASE: -32000,
  PROTOCOL_VERSION: -32001,
} as const;

/** An error returned by the server, or raised by the client for transport faults. */
export class JsonRpcError extends Error {
  readonly code: number;
  readonly data: unknown;
  readonly method: string | undefined;
  constructor(code: number, message: string, data?: unknown, method?: string) {
    super(message);
    this.name = 'JsonRpcError';
    this.code = code;
    this.data = data;
    this.method = method;
  }
  /** True when the code is in the engine-status range (`-32000` and below, excluding the reserved ones). */
  get isEngineStatus(): boolean {
    return this.code <= JsonRpcCode.ENGINE_BASE && this.code > -32099 && this.code !== JsonRpcCode.PROTOCOL_VERSION;
  }
}

export function encodeRequest(id: JsonRpcId, method: string, params: unknown): string {
  const req: JsonRpcRequest = { jsonrpc: '2.0', id, method };
  if (params !== undefined) req.params = params;
  return JSON.stringify(req);
}

export function encodeNotification(method: string, params: unknown): string {
  const n: JsonRpcNotification = { jsonrpc: '2.0', method };
  if (params !== undefined) n.params = params;
  return JSON.stringify(n);
}

export function encodeResult(id: JsonRpcId, result: unknown): string {
  return JSON.stringify({ jsonrpc: '2.0', id, result } satisfies JsonRpcSuccess);
}

export function encodeError(id: JsonRpcId | null, code: number, message: string, data?: unknown): string {
  const error: JsonRpcErrorObject = { code, message };
  if (data !== undefined) error.data = data;
  return JSON.stringify({ jsonrpc: '2.0', id, error } satisfies JsonRpcFailure);
}

export type Decoded =
  | { kind: 'request'; message: JsonRpcRequest }
  | { kind: 'notification'; message: JsonRpcNotification }
  | { kind: 'result'; message: JsonRpcSuccess }
  | { kind: 'error'; message: JsonRpcFailure }
  | { kind: 'invalid'; reason: string; raw: string };

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function isId(v: unknown): v is JsonRpcId {
  return typeof v === 'number' || typeof v === 'string';
}

/** Classifies one text frame. Never throws: malformed input yields `invalid`. */
export function decodeMessage(raw: string): Decoded {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (e) {
    return { kind: 'invalid', reason: `parse error: ${(e as Error).message}`, raw };
  }
  if (!isObject(parsed)) return { kind: 'invalid', reason: 'not an object (batches are not supported)', raw };
  if (parsed['jsonrpc'] !== '2.0') return { kind: 'invalid', reason: 'missing jsonrpc: "2.0"', raw };

  if (typeof parsed['method'] === 'string') {
    if ('id' in parsed && isId(parsed['id'])) {
      return { kind: 'request', message: parsed as unknown as JsonRpcRequest };
    }
    return { kind: 'notification', message: parsed as unknown as JsonRpcNotification };
  }
  if ('error' in parsed) {
    const err = parsed['error'];
    if (!isObject(err) || typeof err['code'] !== 'number' || typeof err['message'] !== 'string') {
      return { kind: 'invalid', reason: 'malformed error object', raw };
    }
    return { kind: 'error', message: parsed as unknown as JsonRpcFailure };
  }
  if ('result' in parsed && isId(parsed['id'])) {
    return { kind: 'result', message: parsed as unknown as JsonRpcSuccess };
  }
  return { kind: 'invalid', reason: 'neither request, notification nor response', raw };
}

/** Monotonic request ids; one per client. */
export function createIdGenerator(start = 1): () => number {
  let next = start;
  return () => next++;
}

/**
 * Decodes the 16-byte header of a binary waveform frame (SPEC-008 §1):
 * `stream_id u32`, `seq u32`, `first_time_ps u64`, little-endian, then samples.
 */
export function decodeBinaryFrame(buffer: ArrayBuffer): {
  streamId: number;
  seq: number;
  firstTimePs: bigint;
  samples: ArrayBuffer;
} {
  if (buffer.byteLength < 16) throw new RangeError('binary frame shorter than its header');
  const view = new DataView(buffer);
  return {
    streamId: view.getUint32(0, true),
    seq: view.getUint32(4, true),
    firstTimePs: view.getBigUint64(8, true),
    samples: buffer.slice(16),
  };
}

/** Builds a binary frame; used by the mock transport and by tests. */
export function encodeBinaryFrame(streamId: number, seq: number, firstTimePs: bigint, samples: ArrayBuffer | Uint8Array): ArrayBuffer {
  const body = samples instanceof Uint8Array ? samples : new Uint8Array(samples);
  const out = new Uint8Array(16 + body.byteLength);
  const view = new DataView(out.buffer);
  view.setUint32(0, streamId, true);
  view.setUint32(4, seq, true);
  view.setBigUint64(8, firstTimePs, true);
  out.set(body, 16);
  return out.buffer;
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/** base64 encoding without relying on `btoa` (which is absent in some workers). */
export function bytesToBase64(bytes: Uint8Array): string {
  let out = '';
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const n = ((bytes[i] ?? 0) << 16) | ((bytes[i + 1] ?? 0) << 8) | (bytes[i + 2] ?? 0);
    out += B64[(n >> 18) & 63]! + B64[(n >> 12) & 63]! + B64[(n >> 6) & 63]! + B64[n & 63]!;
  }
  const rest = bytes.length - i;
  if (rest === 1) {
    const n = (bytes[i] ?? 0) << 16;
    out += B64[(n >> 18) & 63]! + B64[(n >> 12) & 63]! + '==';
  } else if (rest === 2) {
    const n = ((bytes[i] ?? 0) << 16) | ((bytes[i + 1] ?? 0) << 8);
    out += B64[(n >> 18) & 63]! + B64[(n >> 12) & 63]! + B64[(n >> 6) & 63]! + '=';
  }
  return out;
}

export function base64ToBytes(text: string): Uint8Array {
  const clean = text.replace(/[^A-Za-z0-9+/]/g, '');
  const len = Math.floor((clean.length * 3) / 4);
  const out = new Uint8Array(len);
  let o = 0;
  let acc = 0;
  let bits = 0;
  for (const ch of clean) {
    acc = (acc << 6) | B64.indexOf(ch);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o < len) out[o++] = (acc >> bits) & 0xff;
    }
  }
  return out.subarray(0, o);
}
