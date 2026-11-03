// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixToast from '@radix-ui/react-toast';
import { AlertTriangle, CheckCircle2, Info, OctagonAlert, X } from 'lucide-react';
import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react';
import { cx } from '../cx';
import { IconButton } from './Button';
import { uiText } from '../uiText';

export type ToastKind = 'info' | 'success' | 'warning' | 'error';

export interface ToastMessage {
  id?: string;
  kind?: ToastKind;
  title: ReactNode;
  description?: ReactNode;
  /** ms; 0 keeps it until dismissed. */
  duration?: number;
}

interface ToastEntry extends Required<Pick<ToastMessage, 'id' | 'kind'>> {
  title: ReactNode;
  description?: ReactNode;
  duration: number;
  open: boolean;
}

interface ToastApi {
  toast: (message: ToastMessage) => string;
  dismiss: (id: string) => void;
}

const ToastContext = createContext<ToastApi | null>(null);

const ICONS: Record<ToastKind, ReactNode> = {
  info: <Info />,
  success: <CheckCircle2 />,
  warning: <AlertTriangle />,
  error: <OctagonAlert />,
};

let counter = 0;

export function ToastProvider({ children }: { children: ReactNode }) {
  const [entries, setEntries] = useState<ToastEntry[]>([]);
  const dismiss = useCallback((id: string) => {
    setEntries((es) => es.map((e) => (e.id === id ? { ...e, open: false } : e)));
    setTimeout(() => setEntries((es) => es.filter((e) => e.id !== id)), 300);
  }, []);
  const toast = useCallback((message: ToastMessage) => {
    const id = message.id ?? `toast-${++counter}`;
    const entry: ToastEntry = {
      id,
      kind: message.kind ?? 'info',
      title: message.title,
      description: message.description,
      duration: message.duration ?? (message.kind === 'error' ? 0 : 5000),
      open: true,
    };
    setEntries((es) => [...es.filter((e) => e.id !== id), entry]);
    return id;
  }, []);
  const api = useMemo(() => ({ toast, dismiss }), [toast, dismiss]);
  return (
    <ToastContext.Provider value={api}>
      <RadixToast.Provider swipeDirection="right" duration={5000}>
        {children}
        {entries.map((e) => (
          <RadixToast.Root
            key={e.id}
            className={cx('es-toast', `es-toast--${e.kind}`)}
            open={e.open}
            duration={e.duration === 0 ? Infinity : e.duration}
            onOpenChange={(open) => {
              if (!open) dismiss(e.id);
            }}
          >
            <span className="es-toast__icon" aria-hidden="true">
              {ICONS[e.kind]}
            </span>
            <div>
              <RadixToast.Title className="es-toast__title">{e.title}</RadixToast.Title>
              {e.description && <RadixToast.Description className="es-toast__description">{e.description}</RadixToast.Description>}
            </div>
            <RadixToast.Close asChild>
              <IconButton label={uiText('dismiss', 'Dismiss')} variant="ghost" size="sm">
                <X />
              </IconButton>
            </RadixToast.Close>
          </RadixToast.Root>
        ))}
        <RadixToast.Viewport className="es-toast__viewport" label={uiText('notifications', 'Notifications')} />
      </RadixToast.Provider>
    </ToastContext.Provider>
  );
}

export function useToast(): ToastApi {
  const api = useContext(ToastContext);
  if (!api) throw new Error('useToast requires a ToastProvider');
  return api;
}
