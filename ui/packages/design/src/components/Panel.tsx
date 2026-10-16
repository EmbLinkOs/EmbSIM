// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { forwardRef, type HTMLAttributes, type ReactNode } from 'react';
import { cx } from '../cx';

export interface PanelProps extends Omit<HTMLAttributes<HTMLElement>, 'title'> {
  title: ReactNode;
  icon?: ReactNode;
  actions?: ReactNode;
  /** Shown at the start of the header (a drag handle in a dock). */
  leading?: ReactNode;
  /** Remove scrolling and padding (for canvases). */
  flush?: boolean;
  padded?: boolean;
  headingLevel?: 2 | 3 | 4;
  contentRef?: React.Ref<HTMLDivElement>;
}

/** A titled region: header with actions, scrollable content. Announced as a labelled region. */
export const Panel = forwardRef<HTMLElement, PanelProps>(function Panel(
  { title, icon, actions, leading, flush, padded, headingLevel = 2, className, children, contentRef, ...rest },
  ref,
) {
  const Heading = `h${headingLevel}` as 'h2';
  return (
    <section ref={ref} className={cx('es-panel', className)} aria-label={typeof title === 'string' ? title : undefined} {...rest}>
      <header className="es-panel__header">
        {leading}
        <Heading className="es-panel__title">
          {icon}
          {title}
        </Heading>
        {actions && <div className="es-panel__actions">{actions}</div>}
      </header>
      <div ref={contentRef} className={cx('es-panel__content', flush && 'es-panel__content--flush', padded && 'es-panel__content--padded')}>
        {children}
      </div>
    </section>
  );
});
