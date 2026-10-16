// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// Opening a board or a composite part on its own sheet, from the schematic
// (double-click) or the inspector.

import { insideSource, type InsideSource } from './inside';
import { useLayout } from './layout';
import { useProject } from './project';

/** What instance `id` holds inside, or null when it is a plain part. */
export function insideOf(id: string): InsideSource | null {
  const { project, catalog } = useProject.getState();
  const ref = project?.components.find((c) => c.id === id)?.component ?? project?.boards?.find((b) => b.id === id)?.board;
  return ref ? insideSource(catalog[ref]?.description) : null;
}

/** Loads the inside's parts from the catalog, then shows it. False when there is no inside. */
export async function openInside(id: string): Promise<boolean> {
  const src = insideOf(id);
  if (!src) return false;
  await useProject.getState().ensureCatalog(src.components.map((c) => c.component));
  useLayout.getState().setInside(id);
  return true;
}
