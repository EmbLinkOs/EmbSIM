// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A transport moves whole frames. The client layers JSON-RPC on top, so the
// WebSocket transport and the in-memory mock share one code path.

export type TransportState = 'idle' | 'connecting' | 'open' | 'closed';

export type TextListener = (frame: string) => void;
export type BinaryListener = (frame: ArrayBuffer) => void;
export type StateListener = (state: TransportState, detail?: string) => void;

export interface Transport {
  readonly state: TransportState;
  /** A human label shown in the status bar ("ws://…", "Demo session"). */
  readonly label: string;
  connect(): Promise<void>;
  close(): void;
  send(frame: string): void;
  onText(listener: TextListener): () => void;
  onBinary(listener: BinaryListener): () => void;
  onState(listener: StateListener): () => void;
}

/** Shared listener bookkeeping for transports. */
export class TransportBase {
  private textListeners = new Set<TextListener>();
  private binaryListeners = new Set<BinaryListener>();
  private stateListeners = new Set<StateListener>();
  protected currentState: TransportState = 'idle';

  get state(): TransportState {
    return this.currentState;
  }

  onText(listener: TextListener): () => void {
    this.textListeners.add(listener);
    return () => this.textListeners.delete(listener);
  }

  onBinary(listener: BinaryListener): () => void {
    this.binaryListeners.add(listener);
    return () => this.binaryListeners.delete(listener);
  }

  onState(listener: StateListener): () => void {
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  }

  protected emitText(frame: string): void {
    for (const l of this.textListeners) l(frame);
  }

  protected emitBinary(frame: ArrayBuffer): void {
    for (const l of this.binaryListeners) l(frame);
  }

  protected setState(state: TransportState, detail?: string): void {
    if (this.currentState === state) return;
    this.currentState = state;
    for (const l of this.stateListeners) l(state, detail);
  }
}
