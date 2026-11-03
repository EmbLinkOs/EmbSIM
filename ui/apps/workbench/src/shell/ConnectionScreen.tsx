// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// What the workbench shows instead of a perspective while no session is open:
// connecting, the connection lost, or a connection that could not be made.
// Each says what happened in words and offers the way back.
import { Button } from '@embsim/design';
import { Loader2, PlugZap, RefreshCw, XOctagon } from 'lucide-react';
import { useT } from '../i18n';
import { useSession } from '../stores/session';
import { useSimTimeFormat } from './simState';

export function ConnectionScreen() {
  const t = useT();
  const status = useSession((s) => s.status);
  const error = useSession((s) => s.error);
  const where = useSession((s) => s.transportLabel) || window.location.host;
  const now = useSession((s) => s.now);
  const fmt = useSimTimeFormat();
  const reconnect = () => window.location.reload();

  if (status === 'closed') {
    return (
      <div className="wb-connscreen wb-connscreen--closed" role="alert" data-testid="connection-screen" data-status="closed">
        <div className="wb-connscreen__card">
          <PlugZap className="wb-connscreen__icon" aria-hidden="true" />
          <h1 className="wb-connscreen__title">{t('conn.closed.title')}</h1>
          <p className="wb-connscreen__body">{t('conn.closed.body', { where, time: fmt(now) })}</p>
          <div className="wb-connscreen__actions">
            <Button variant="primary" icon={<RefreshCw />} onClick={reconnect}>
              {t('conn.reconnect')}
            </Button>
          </div>
          <p className="wb-connscreen__hint es-mono">{t('conn.howTo')}</p>
        </div>
      </div>
    );
  }
  if (status === 'error') {
    return (
      <div className="wb-connscreen wb-connscreen--error" role="alert" data-testid="connection-screen" data-status="error">
        <div className="wb-connscreen__card">
          <XOctagon className="wb-connscreen__icon" aria-hidden="true" />
          <h1 className="wb-connscreen__title">{t('conn.error.title')}</h1>
          <p className="wb-connscreen__body">{t('conn.error.body')}</p>
          {error && (
            <details className="wb-connscreen__details" open>
              <summary>{t('conn.details')}</summary>
              <pre className="es-mono">{error}</pre>
            </details>
          )}
          <div className="wb-connscreen__actions">
            <Button variant="primary" icon={<RefreshCw />} onClick={reconnect}>
              {t('conn.reconnect')}
            </Button>
          </div>
          <p className="wb-connscreen__hint es-mono">{t('conn.howTo')}</p>
        </div>
      </div>
    );
  }
  return (
    <div className="wb-connscreen wb-connecting" role="status" data-testid="connection-screen" data-status="connecting">
      <div className="wb-connscreen__card">
        <Loader2 className="wb-connscreen__icon wb-spin" aria-hidden="true" />
        <h1 className="wb-connscreen__title">{t('conn.connecting.title')}</h1>
        <p className="wb-connscreen__body">{t('conn.connecting.body', { where })}</p>
      </div>
    </div>
  );
}
