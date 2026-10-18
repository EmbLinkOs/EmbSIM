// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A board's serial port on the host (ADR-031): the engine puts the board's
// USB-serial bridge on a pseudo-terminal, which the host's own tools open
// (a serial monitor, screen, pyserial). The hook follows the target's port
// through port.list and port.state, and opens or removes it.

import type { HostPort } from '@embsim/protocol';
import type { HostPortView } from '@embsim/instruments';
import { useCallback, useEffect, useState } from 'react';
import { errorMessage } from '../../actions';
import { getClient } from '../../connection';
import { useSession } from '../../stores/session';

const view = (p: HostPort): HostPortView => ({ path: p.path, connected: p.connected, baud: p.baud, sketchBaud: p.sketch_baud });

export function useHostPort(target: string | null) {
  const connected = useSession((s) => s.status === 'open');
  const [port, setPort] = useState<HostPortView | null>(null);
  const [error, setError] = useState<string | undefined>();

  useEffect(() => {
    setPort(null);
    setError(undefined);
    if (!connected || !target) return;
    let gone = false;
    const client = getClient();
    client
      .call('port.list', {})
      .then((r) => {
        const p = r.ports.find((x) => x.target === target);
        if (!gone && p) setPort(view(p));
      })
      .catch(() => undefined);
    const off = client.on('port.state', (p) => {
      if (p.target === target) setPort(view(p));
    });
    return () => {
      gone = true;
      off();
    };
  }, [connected, target]);

  const toggle = useCallback(
    (open: boolean) => {
      if (!target) return;
      const client = getClient();
      if (open) {
        client
          .call('port.open', { target })
          .then((p) => {
            setPort(view(p));
            setError(undefined);
          })
          .catch((e: unknown) => setError(errorMessage(e)));
      } else {
        client
          .call('port.close', { target })
          .then(() => setPort(null))
          .catch((e: unknown) => setError(errorMessage(e)));
      }
    },
    [target],
  );

  return { port, error, toggle };
}
