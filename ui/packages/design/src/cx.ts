// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
/** Joins class names, skipping falsy values. */
export function cx(...parts: unknown[]): string {
  return parts.filter((p): p is string => typeof p === 'string' && p.length > 0).join(' ');
}
