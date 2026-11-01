// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { formatTime, type Time } from '@embsim/protocol';
import { useEffect, useRef, useState } from 'react';
import { uiText } from '@embsim/design';

export interface SerialLine {
  at: Time;
  direction: 'rx' | 'tx';
  text: string;
  /** the port (a serial probe's target without `serial:`) */
  target?: string;
  /** received text that has not ended with a newline yet */
  open?: boolean;
}

/** `lines` with `text` received on `target` at `at`: the text goes on the
 * port's open line until a newline ends it (CR is dropped), so a line
 * delivered in pieces shows as one, stamped with its first piece's time. */
export function appendSerialText(lines: SerialLine[], target: string, at: Time, text: string, limit = 2000): SerialLine[] {
  const out = lines.slice();
  let open = -1;
  for (let i = out.length - 1; i >= 0; i--) {
    const l = out[i]!;
    if (l.target === target && l.direction === 'rx') {
      if (l.open) open = i;
      break;
    }
  }
  const parts = text.replace(/\r/g, '').split('\n');
  parts.forEach((part, k) => {
    const last = k === parts.length - 1;
    if (k === 0 && open >= 0) {
      const l = out[open]!;
      out[open] = { ...l, text: l.text + part, open: last };
    } else if (!last || part.length) {
      out.push({ at, direction: 'rx', target, text: part, open: last });
    }
  });
  return out.length > limit ? out.slice(-limit) : out;
}

/** The port as the host's programs see it (ADR-031): a pseudo-terminal a
 * serial monitor, screen or a script opens. */
export interface HostPortView {
  path: string;
  /** a program has it open (on the Uno, opening it resets the sketch) */
  connected: boolean;
  /** the program's line rate, and the sketch's */
  baud: number;
  sketchBaud: number;
}

export interface SerialTerminalProps {
  target: string | null;
  targets: string[];
  onTargetChange?: (target: string) => void;
  lines: SerialLine[];
  onSend?: (target: string, text: string) => void;
  disabled?: boolean;
  disabledReason?: string;
  /** the target's port on the host, when it has one */
  hostPort?: HostPortView | null;
  /** why it cannot have one (the demo, Windows) */
  hostPortError?: string;
  /** put the port on the host, or take it away */
  onHostPort?: (open: boolean) => void;
}

/** a mismatch a UART does not tolerate (3 %) */
function baudsDiffer(a: number, b: number): boolean {
  return a > 0 && b > 0 && Math.abs(a - b) > 0.03 * b;
}

/** A simple serial console: received text with timestamps, a send line with history. */
export function SerialTerminal({ target, targets, onTargetChange, lines, onSend, disabled, disabledReason, hostPort, hostPortError, onHostPort }: SerialTerminalProps) {
  const [text, setText] = useState('');
  const [history, setHistory] = useState<string[]>([]);
  const [histIdx, setHistIdx] = useState(-1);
  const logRef = useRef<HTMLDivElement>(null);
  const [stick, setStick] = useState(true);

  useEffect(() => {
    const el = logRef.current;
    if (el && stick) el.scrollTop = el.scrollHeight;
  }, [lines, stick]);

  const send = () => {
    if (!target || !text || disabled) return;
    onSend?.(target, text + '\n');
    setHistory((h) => [text, ...h].slice(0, 50));
    setHistIdx(-1);
    setText('');
  };

  return (
    <div className="es-terminal">
      <div className="es-terminal__bar">
        <label className="es-terminal__label">
          Port
          <select className="es-terminal__select" value={target ?? ''} onChange={(e) => onTargetChange?.(e.target.value)} disabled={targets.length === 0}>
            {targets.length === 0 && <option value="">{uiText('serial.none', 'none')}</option>}
            {targets.map((t) => (
              <option key={t} value={t}>
                {t}
              </option>
            ))}
          </select>
        </label>
        <span className="es-terminal__hint">{disabled ? disabledReason ?? 'unavailable' : `${lines.length} lines`}</span>
      </div>
      {onHostPort && target && !disabled && (
        <div className="es-terminal__host" aria-label={uiText('serial.hostPort', 'Port on the host')}>
          {hostPort ? (
            <>
              <span className="es-terminal__host-path" title={uiText('serial.hostHint', 'Open it with any serial program: screen, minicom, the Arduino IDE\'s monitor, pyserial')}>
                <code>{hostPort.path}</code>
              </span>
              <span className={`es-terminal__host-state${hostPort.connected ? ' es-terminal__host-state--on' : ''}`}>
                {hostPort.connected ? `open at ${hostPort.baud || '?'} baud` : 'no program has it open'}
              </span>
              {hostPort.connected && baudsDiffer(hostPort.baud, hostPort.sketchBaud) && (
                <span className="es-terminal__host-warn" role="status">
                  the sketch runs at {hostPort.sketchBaud}: both sides read garbage
                </span>
              )}
              <button type="button" className="es-button es-button--sm es-button--ghost" onClick={() => onHostPort(false)}>
                Remove
              </button>
            </>
          ) : (
            <>
              <button type="button" className="es-button es-button--sm" onClick={() => onHostPort(true)}>
                Open on this computer
              </button>
              <span className="es-terminal__hint">{hostPortError ?? "a serial port the host's own tools open"}</span>
            </>
          )}
        </div>
      )}
      <div
        ref={logRef}
        className="es-terminal__log"
        role="log"
        aria-live="polite"
        aria-label={uiText('serial.output', 'Serial output')}
        onScroll={(e) => {
          const el = e.currentTarget;
          setStick(el.scrollHeight - el.scrollTop - el.clientHeight < 4);
        }}
      >
        {lines.length === 0 && <div className="es-terminal__empty">{disabled ? disabledReason ?? 'No serial probe in this project' : 'Waiting for data…'}</div>}
        {lines.map((l, i) => (
          <div key={i} className={`es-terminal__line es-terminal__line--${l.direction}`}>
            <span className="es-terminal__time">{formatTime(l.at, { padZeros: false })}</span>
            <span className="es-terminal__dir">{l.direction === 'rx' ? '←' : '→'}</span>
            <span className="es-terminal__text">{l.text}</span>
          </div>
        ))}
      </div>
      <form
        className="es-terminal__input"
        onSubmit={(e) => {
          e.preventDefault();
          send();
        }}
      >
        <input
          aria-label={uiText('serial.toSend', 'Text to send')}
          placeholder={disabled ? 'No serial port' : 'Type and press Enter to send'}
          value={text}
          disabled={disabled || !target}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'ArrowUp' && history.length) {
              e.preventDefault();
              const i = Math.min(history.length - 1, histIdx + 1);
              setHistIdx(i);
              setText(history[i] ?? '');
            } else if (e.key === 'ArrowDown') {
              e.preventDefault();
              const i = Math.max(-1, histIdx - 1);
              setHistIdx(i);
              setText(i < 0 ? '' : (history[i] ?? ''));
            }
          }}
        />
        <button type="submit" className="es-button es-button--sm" disabled={disabled || !target || !text}>
          Send
        </button>
      </form>
    </div>
  );
}
