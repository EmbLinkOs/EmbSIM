// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// A catalog entry's own text (its description, its fidelity's claims and
// omissions) in the interface's language. The catalog carries it beside the
// English, under `i18n.<lang>` (schemas/common.schema.json); what an entry
// does not translate stays English, as a missing interface message does.
// The rest of the declaration (ranges, validation) is not text and stays.
import type { ComponentDescription, FidelityDeclaration, FidelityTier } from '@embsim/protocol';
import type { Language } from '../stores/settings';

type Entry = Pick<ComponentDescription, 'description' | 'fidelity' | 'i18n'>;

/** The entry's description in `lang`, English when it has none. */
export function catalogDescription(entry: Entry | undefined, lang: Language): string | undefined {
  if (!entry) return undefined;
  return (lang !== 'en' && entry.i18n?.[lang]?.description) || entry.description;
}

/** The entry's declaration of `tier` with its claims and omissions in `lang`. */
export function catalogFidelity(entry: Entry | undefined, tier: FidelityTier, lang: Language): FidelityDeclaration | null | undefined {
  const decl = entry?.fidelity[tier];
  if (!decl || lang === 'en') return decl;
  const tr = entry?.i18n?.[lang]?.fidelity?.[tier];
  if (!tr) return decl;
  const omissions = tr.omissions && tr.omissions.length === (decl.omissions?.length ?? 0) ? tr.omissions : decl.omissions;
  return { ...decl, claims: tr.claims || decl.claims, omissions };
}
