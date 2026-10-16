// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import { CommandStack } from './commands';

describe('command stack', () => {
  it('executes, undoes and redoes in order', async () => {
    const stack = new CommandStack();
    const log: string[] = [];
    const cmd = (n: string) => ({ label: n, redo: () => void log.push(`+${n}`), undo: () => void log.push(`-${n}`) });
    await stack.execute(cmd('a'));
    await stack.execute(cmd('b'));
    expect(stack.canUndo).toBe(true);
    expect(stack.undoLabel).toBe('b');
    await stack.undo();
    await stack.undo();
    expect(stack.canUndo).toBe(false);
    await stack.redo();
    await stack.execute(cmd('c'));
    expect(stack.canRedo).toBe(false);
    expect(log).toEqual(['+a', '+b', '-b', '-a', '+a', '+c']);
  });

  it('serialises asynchronous commands', async () => {
    const stack = new CommandStack();
    const order: number[] = [];
    const slow = (n: number, ms: number) => ({
      label: String(n),
      redo: () => new Promise<void>((r) => setTimeout(() => (order.push(n), r()), ms)),
      undo: () => undefined,
    });
    void stack.execute(slow(1, 20));
    await stack.execute(slow(2, 1));
    expect(order).toEqual([1, 2]);
  });
});
