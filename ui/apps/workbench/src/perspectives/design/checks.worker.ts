// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { checkSchematic, type SceneCatalog } from '@embsim/scene';
import type { Project } from '@embsim/protocol';
self.onmessage = (event: MessageEvent<{ project: Project; catalog: SceneCatalog }>) => {
  try { self.postMessage({ issues: checkSchematic(event.data.project, event.data.catalog) }); }
  catch (e) { self.postMessage({ error: e instanceof Error ? e.message : 'Schematic checks failed.' }); }
};
