// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { formatTime, type Time } from '@embsim/protocol';
import { useEffect, useRef, useState } from 'react';

export interface SerialLine {
  at: Time;
  direction: 'rx' | 'tx';
  text: string;
}

export interface SerialTerminalProps {
  target: string | null;
  targets: string[];
  onTargetChange?: (target: string) => void;
  lines: SerialLine[];
  onSend?: (target: string, text: string) => void;
  disabled?: boolean;
  disabledReason?: string;
}

/** A simple serial console: received text with timestamps, a send line with history. */
export function SerialTerminal({ target, targets, onTargetChange, lines, onSend, disabled, disabledReason }: SerialTerminalProps) {
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
            {targets.length === 0 && <option value="">none</option>}
            {targets.map((t) => (
              <option key={t} value={t}>
                {t}
              </option>
            ))}
          </select>
        </label>
        <span className="es-terminal__hint">{disabled ? disabledReason ?? 'unavailable' : `${lines.length} lines`}</span>
      </div>
      <div
        ref={logRef}
        className="es-terminal__log"
        role="log"
        aria-live="polite"
        aria-label="Serial output"
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
          aria-label="Text to send"
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
