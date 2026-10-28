// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { demoProject } from '@embsim/protocol';
import { describe, expect, it } from 'vitest';
import { diffProject } from './projectDiff';

describe('project diff', () => {
  it('is empty for identical projects', () => {
    expect(diffProject(demoProject(), demoProject())).toEqual([]);
  });

  it('detects added components, connections and layout changes', () => {
    const before = demoProject();
    const after = demoProject();
    after.components.push({ id: 'C1', component: 'emblink/capacitor', layout: { x: 10, y: 20 } });
    after.nets['COLL']!.push('C1.1');
    after.nets['GND']!.push('C1.2');
    after.components[0]!.layout = { x: 999, y: 1 };
    after.layout!.wires!.push({ net: 'COLL', points: [[0, 0], [10, 0]] });
    const edits = diffProject(before, after);
    expect(edits).toContainEqual({ add_component: { id: 'C1', component: 'emblink/capacitor', version: undefined, params: undefined, layout: { x: 10, y: 20 } } });
    expect(edits).toContainEqual({ connect: { net: 'COLL', pin: 'C1.1' } });
    expect(edits).toContainEqual({ connect: { net: 'GND', pin: 'C1.2' } });
    expect(edits).toContainEqual({ set_layout: { id: 'VCC1', layout: { x: 999, y: 1 } } });
    expect(edits.at(-1)).toHaveProperty('set_layout.wires');
  });

  it('detects removals, parameter changes and renames', () => {
    const before = demoProject();
    const after = demoProject();
    after.components = after.components.filter((c) => c.id !== 'R3');
    after.nets['3V3'] = after.nets['3V3']!.filter((p) => p !== 'R3.1');
    after.nets['LED_K'] = after.nets['LED_K']!.filter((p) => p !== 'R3.2');
    after.components.find((c) => c.id === 'R1')!.params = { resistance: '4.7k' };
    after.nets['BASE'] = after.nets['Q1B']!;
    delete after.nets['Q1B'];
    const edits = diffProject(before, after);
    expect(edits).toContainEqual({ remove_component: { id: 'R3' } });
    expect(edits).toContainEqual({ set_param: { id: 'R1', param: 'resistance', value: '4.7k' } });
    expect(edits).toContainEqual({ rename_net: { from: 'Q1B', to: 'BASE' } });
    expect(edits.some((e) => 'connect' in e)).toBe(false);
    expect(edits.some((e) => 'disconnect' in e)).toBe(false);
  });
});

describe('library baseline diff', () => {
  it('pins and removes metadata without synthesizing circuit edits', () => {
    const before = demoProject(), after = structuredClone(before);
    after.library_lock = {schema_version: 1, kind: 'library_lock', libraries: {emblink: {version: '1.0.0', files: {'library.json': `sha256:${'a'.repeat(64)}`}}}, catalog: {}};
    expect(diffProject(before, after)).toEqual([{set_library_lock: after.library_lock}]);
    expect(diffProject(after, before)).toEqual([{set_library_lock: null}]);
    after.blocks = {}; before.blocks = {};
    expect(diffProject(before, after)).toEqual([{set_library_lock: after.library_lock}]);
  });
});
