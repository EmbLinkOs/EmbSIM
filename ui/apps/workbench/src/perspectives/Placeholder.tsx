// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// What a perspective or panel needs before it can show anything: a centred
// card naming the view, what it does and a checklist of its requirements,
// each met or missing. Availability is tied to the server's capability flags
// and to the presence of boards, never faked.

import type { ReactNode } from 'react';
import { CheckCircle2, Circle } from 'lucide-react';
import { Badge } from '@embsim/design';
import type { Capability } from '@embsim/protocol';
import { useSession } from '../stores/session';
import { t as tr } from '../i18n';

export interface PlaceholderProps {
  title: string;
  /** What the panel will show when available. */
  description: string;
  /** Capabilities the server must advertise. */
  requires?: Capability[];
  /** Needs at least one board with an MCU. */
  needsBoard?: boolean;
  /** Further steps, shown as unmet requirements while the panel is unavailable and as hints once it is. */
  actions?: string[];
  icon?: ReactNode;
}

function Requirement({ met, children }: { met: boolean; children: ReactNode }) {
  return (
    <li className={met ? 'is-met' : undefined}>
      {met ? <CheckCircle2 aria-hidden="true" /> : <Circle aria-hidden="true" />}
      <span>{children}</span>
      <span className="es-sr-only">{met ? ' (met)' : ' (missing)'}</span>
    </li>
  );
}

export function Placeholder({ title, description, requires = [], needsBoard, actions = [], icon }: PlaceholderProps) {
  const capabilities = useSession((s) => s.capabilities);
  const boards = useSession((s) => s.boards);
  const missing = requires.filter((c) => !capabilities.includes(c));
  const noBoard = needsBoard && boards.length === 0;
  const available = missing.length === 0 && !noBoard;
  return (
    <div className="wb-placeholder" role="status">
      <div className="wb-placeholder__card">
        {icon && <div className="wb-placeholder__icon">{icon}</div>}
        <div className="wb-placeholder__heading">
          <span className="wb-placeholder__title">{title}</span>
          {!available && <Badge tone="warning">{tr('ph.notAvailable')}</Badge>}
        </div>
        <p className="wb-placeholder__description">{description}</p>
        {(requires.length > 0 || needsBoard || actions.length > 0) && (
          <ul className="wb-placeholder__needs" aria-label={available ? 'Next steps' : 'What this needs'}>
            {requires.map((c) => (
              <Requirement key={c} met={!missing.includes(c)}>
                Requires server capability <code>{c}</code>.
              </Requirement>
            ))}
            {needsBoard && <Requirement met={!noBoard}>{tr('ph.requiresMcu')}</Requirement>}
            {actions.map((a) => (
              <Requirement key={a} met={false}>{a.charAt(0).toUpperCase() + a.slice(1)}</Requirement>
            ))}
          </ul>
        )}
      </div>
    </div>
  );
}
