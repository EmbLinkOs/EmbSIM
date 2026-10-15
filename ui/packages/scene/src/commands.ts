// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// An undo/redo command stack. Commands may be asynchronous (they usually go
// through a project transaction on the server); the stack serialises them.

import { useEffect, useState } from 'react';

export interface Command {
  label: string;
  redo: () => void | Promise<void>;
  undo: () => void | Promise<void>;
}

export class CommandStack {
  private done: Command[] = [];
  private undone: Command[] = [];
  private listeners = new Set<() => void>();
  private queue: Promise<void> = Promise.resolve();
  readonly limit: number;

  constructor(limit = 200) {
    this.limit = limit;
  }

  get canUndo(): boolean {
    return this.done.length > 0;
  }

  get canRedo(): boolean {
    return this.undone.length > 0;
  }

  get undoLabel(): string | null {
    return this.done[this.done.length - 1]?.label ?? null;
  }

  get redoLabel(): string | null {
    return this.undone[this.undone.length - 1]?.label ?? null;
  }

  /** Executes and records a command. */
  execute(command: Command): Promise<void> {
    return this.enqueue(async () => {
      await command.redo();
      this.done.push(command);
      if (this.done.length > this.limit) this.done.shift();
      this.undone = [];
    });
  }

  undo(): Promise<void> {
    return this.enqueue(async () => {
      const c = this.done.pop();
      if (!c) return;
      await c.undo();
      this.undone.push(c);
    });
  }

  redo(): Promise<void> {
    return this.enqueue(async () => {
      const c = this.undone.pop();
      if (!c) return;
      await c.redo();
      this.done.push(c);
    });
  }

  clear(): void {
    this.done = [];
    this.undone = [];
    this.emit();
  }

  subscribe(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private enqueue(task: () => Promise<void>): Promise<void> {
    const run = this.queue.then(task).finally(() => this.emit());
    this.queue = run.catch(() => undefined);
    return run;
  }

  private emit(): void {
    for (const l of this.listeners) l();
  }
}

/** Re-renders when the stack changes; returns its current flags. */
export function useCommandStack(stack: CommandStack): { canUndo: boolean; canRedo: boolean; undoLabel: string | null; redoLabel: string | null } {
  const [, bump] = useState(0);
  useEffect(() => stack.subscribe(() => bump((n) => n + 1)), [stack]);
  return { canUndo: stack.canUndo, canRedo: stack.canRedo, undoLabel: stack.undoLabel, redoLabel: stack.redoLabel };
}
