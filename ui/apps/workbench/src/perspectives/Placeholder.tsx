// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Structured "not available in M0" panels for the Debug and Verify
// perspectives. Availability is tied to the server's capability flags and
// to the presence of boards, never faked.

import { Badge } from '@embsim/design';
import type { Capability } from '@embsim/protocol';
import { useSession } from '../stores/session';

export interface PlaceholderProps {
  title: string;
  /** What the panel will show when available. */
  description: string;
  /** Capabilities the server must advertise. */
  requires?: Capability[];
  /** Needs at least one board with an MCU. */
  needsBoard?: boolean;
  actions?: string[];
}

export function Placeholder({ title, description, requires = [], needsBoard, actions = [] }: PlaceholderProps) {
  const capabilities = useSession((s) => s.capabilities);
  const boards = useSession((s) => s.boards);
  const missing = requires.filter((c) => !capabilities.includes(c));
  const noBoard = needsBoard && boards.length === 0;
  const available = missing.length === 0 && !noBoard;
  return (
    <div className="wb-placeholder" role="status">
      <div className="es-row">
        <span className="wb-placeholder__title">{title}</span>
        <Badge tone={available ? 'accent' : 'warning'}>{available ? 'available' : 'not available in M0'}</Badge>
      </div>
      <div>{description}</div>
      {!available && (
        <div>
          {missing.length > 0 && (
            <div>
              Requires server capability {missing.map((m) => <code key={m}>{m}</code>).reduce<React.ReactNode[]>((acc, el, i) => (i ? [...acc, ', ', el] : [el]), [])}.
            </div>
          )}
          {noBoard && <div>Requires a board with an MCU in the project.</div>}
        </div>
      )}
      {actions.length > 0 && (
        <ul>
          {actions.map((a) => (
            <li key={a}>{a}</li>
          ))}
        </ul>
      )}
    </div>
  );
}
