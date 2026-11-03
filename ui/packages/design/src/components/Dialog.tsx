// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import * as RadixDialog from '@radix-ui/react-dialog';
import { X } from 'lucide-react';
import type { ReactNode } from 'react';
import { cx } from '../cx';
import { IconButton } from './Button';
import { uiText } from '../uiText';

export interface DialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: ReactNode;
  description?: ReactNode;
  children?: ReactNode;
  footer?: ReactNode;
  className?: string;
}

/** A modal dialog (focus trapped, Escape closes, labelled by its title). */
export function Dialog({ open, onOpenChange, title, description, children, footer, className }: DialogProps) {
  return (
    <RadixDialog.Root open={open} onOpenChange={onOpenChange}>
      <RadixDialog.Portal>
        <RadixDialog.Overlay className="es-dialog__overlay" />
        <RadixDialog.Content className={cx('es-dialog__content', className)}>
          <div className="es-dialog__header">
            <RadixDialog.Title className="es-dialog__title">{title}</RadixDialog.Title>
            <RadixDialog.Close asChild>
              <IconButton label={uiText('close', 'Close')} variant="ghost" size="sm">
                <X />
              </IconButton>
            </RadixDialog.Close>
          </div>
          {description ? (
            <RadixDialog.Description className="es-dialog__description">{description}</RadixDialog.Description>
          ) : (
            <RadixDialog.Description className="es-sr-only">{typeof title === 'string' ? title : 'Dialog'}</RadixDialog.Description>
          )}
          {children && <div className="es-dialog__body">{children}</div>}
          {footer && <div className="es-dialog__footer">{footer}</div>}
        </RadixDialog.Content>
      </RadixDialog.Portal>
    </RadixDialog.Root>
  );
}
