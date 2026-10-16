// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { hex32, parseAddress, useDebug } from './debug';

describe('debug addresses', () => {
  it('reads hexadecimal and decimal addresses, with digit grouping', () => {
    expect(parseAddress('0x080002f4')).toEqual({ address: 0x080002f4 });
    expect(parseAddress(' 0x0800_02F4 ')).toEqual({ address: 0x080002f4 });
    expect(parseAddress('134218484')).toEqual({ address: 134218484 });
  });

  it('keeps a symbol as written, underscores included', () => {
    expect(parseAddress('EXTI0_IRQHandler')).toEqual({ symbol: 'EXTI0_IRQHandler' });
    expect(parseAddress('main')).toEqual({ symbol: 'main' });
    expect(parseAddress('   ')).toEqual({});
  });

  it('formats 32-bit values', () => {
    expect(hex32(0x414)).toBe('0x00000414');
    expect(hex32(-1)).toBe('0xffffffff');
  });
});

const client = vi.hoisted(() => ({ debug: { memory: vi.fn(), peripherals: vi.fn() } }));
vi.mock('../connection', () => ({ tryClient: () => client }));
function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: Error) => void;
  const promise = new Promise<T>((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
describe('debug board isolation', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useDebug.getState().setBoard(null);
    useDebug.getState().setBoard('uno');
  });
  it('clears inspected state when selecting another board', () => {
    useDebug.setState({ memory: new Uint8Array([42]), previous: { r0: 42 },
      lineBreakpoints: ['main.c:4'], error: 'old failure', memChosen: true });
    useDebug.getState().setBoard('bluepill');
    expect(useDebug.getState()).toMatchObject({ memory: null, previous: {},
      lineBreakpoints: [], error: null, memChosen: false, busy: false });
  });
  it('discards late memory after switching away and back', async () => {
    const old = deferred<{ address: number; base64: string }>();
    client.debug.memory.mockReturnValueOnce(old.promise);
    const request = useDebug.getState().loadMemory(256);
    useDebug.getState().setBoard('bluepill');
    useDebug.getState().setBoard('uno');
    old.resolve({ address: 256, base64: 'Kg==' });
    await request;
    expect(useDebug.getState().memory).toBeNull();
    expect(useDebug.getState().memChosen).toBe(false);
  });
  it('tracks overlapping requests and ignores errors from a previous board', async () => {
    const old = deferred<never>();
    client.debug.memory.mockReturnValueOnce(old.promise);
    const stale = useDebug.getState().loadMemory();
    useDebug.getState().setBoard('bluepill');
    const memory = deferred<{ address: number; base64: string }>();
    const peripherals = deferred<{ peripherals: [] }>();
    client.debug.memory.mockReturnValueOnce(memory.promise);
    client.debug.peripherals.mockReturnValueOnce(peripherals.promise);
    const first = useDebug.getState().loadMemory();
    const second = useDebug.getState().loadPeripherals();
    old.reject(new Error('old board disconnected'));
    await stale;
    expect(useDebug.getState().error).toBeNull();
    memory.resolve({ address: 0x20000000, base64: 'Kg==' });
    await first;
    expect(useDebug.getState().busy).toBe(true);
    peripherals.resolve({ peripherals: [] });
    await second;
    expect(useDebug.getState().busy).toBe(false);
    expect(useDebug.getState().memory).toEqual(new Uint8Array([42]));
  });
});
