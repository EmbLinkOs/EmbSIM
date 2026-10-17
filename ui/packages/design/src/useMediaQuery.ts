// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { useEffect, useState } from 'react';

/** Whether a CSS media query matches now, following it as the window changes
 * (false where there is no window, as in tests without matchMedia). */
export function useMediaQuery(query: string): boolean {
  const get = () => typeof window !== 'undefined' && typeof window.matchMedia === 'function' && window.matchMedia(query).matches;
  const [matches, setMatches] = useState(get);
  useEffect(() => {
    if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return;
    const mq = window.matchMedia(query);
    const on = () => setMatches(mq.matches);
    on();
    mq.addEventListener('change', on);
    return () => mq.removeEventListener('change', on);
  }, [query]);
  return matches;
}
