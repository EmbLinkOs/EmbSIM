// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { webcrypto } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { assemblyCsv, assemblyLines, demoManifest, verifyManifest } from './engineering';
import type { ComponentDescription, EngineeringManifestData, ManifestDocument, Project } from './types';
const subtle = webcrypto.subtle as SubtleCrypto;
async function seal(value: unknown): Promise<ManifestDocument> {
  const json = JSON.stringify(value),
    hash = await subtle.digest('SHA-256', new TextEncoder().encode(json));
  return {
    json,
    sha256: 'sha256:' + Array.from(new Uint8Array(hash), (b) => b.toString(16).padStart(2, '0')).join(''),
  };
}
const project: Project = { schema_version: 1, kind: 'project', name: 'Assembly', components: [], nets: {} };
const description = (id: string, part?: ComponentDescription['part']): ComponentDescription => ({
  schema_version: 1,
  kind: 'component',
  id,
  version: '1.0.0',
  name: 'Part, "reviewed"',
  category: 'analog',
  pins: [],
  symbol: { svg: '', width: 10, height: 10, pins: {} },
  fidelity: {},
  provenance: { license: 'Apache-2.0' },
  ...(part ? { part } : {}),
});
async function manifest(): Promise<EngineeringManifestData> {
  const desc = description('test/ic', {
    manufacturer: 'Vendor',
    mpn: 'IC-1',
    package: 'PDIP-8',
    datasheet: 'https://example.org/ic.pdf',
  });
  return {
    schema_version: 1,
    kind: 'engineering_manifest',
    source: 'native',
    engine: '0.1.0',
    abi: 1,
    policy: 'electrical',
    now_ps: '9007199254740993',
    project: await seal(project),
    libraries: [],
    models: [],
    catalog: {
      'component:test/ic': await seal(desc),
      'component:emblink/gnd': await seal(description('emblink/gnd')),
    },
    instances: [
      {
        path: 'B.U1',
        sheet: 'B',
        id: 'U1',
        kind: 'component',
        reference: 'test/ic',
        catalog: 'component:test/ic',
        resolution: 'native',
        params: { gain: 10, enabled: true },
      },
      {
        path: 'A.U1',
        sheet: 'A',
        id: 'U1',
        kind: 'component',
        reference: 'test/ic',
        catalog: 'component:test/ic',
        resolution: 'native',
        params: { enabled: true, gain: 10 },
      },
      {
        path: 'C.U1',
        sheet: 'C',
        id: 'U1',
        kind: 'component',
        reference: 'test/ic',
        catalog: 'component:test/ic',
        resolution: 'native',
        params: { gain: 20 },
        requested_version: '2.0.0',
      },
      {
        path: 'G',
        sheet: '',
        id: 'G',
        kind: 'component',
        reference: 'emblink/gnd',
        catalog: 'component:emblink/gnd',
        resolution: 'native',
        params: {},
      },
    ],
  };
}
describe('engineering assembly snapshots', () => {
  it('verifies exact native bytes including numeric spelling and nested documents', async () => {
    const data = await manifest(),
      envelope = await seal(data);
    expect((await verifyManifest(envelope, subtle)).now_ps).toBe('9007199254740993');
    const changed = structuredClone(envelope);
    changed.json = changed.json.replace('9007199254740993', '9007199254740994');
    await expect(verifyManifest(changed, subtle)).rejects.toThrow('hash mismatch');
    data.catalog['component:test/ic']!.json += ' ';
    await expect(verifyManifest(await seal(data), subtle)).rejects.toThrow('hash mismatch');
    await expect(verifyManifest(envelope, null)).rejects.toThrow();
  });
  it('groups repeated configured packages once while preserving different parameter groups', async () => {
    const rows = assemblyLines(await manifest());
    const packages = rows.filter((r) => r.reference === 'test/ic');
    expect(packages).toHaveLength(2);
    expect(packages[0]!.paths).toEqual(['A.U1', 'B.U1']);
    expect(packages[1]!.notes).toContain('C.U1: requested 2.0.0, loaded 1.0.0');
    expect(rows.find((r) => r.reference === 'emblink/gnd')!.simulationFixture).toBe(true);
  });
  it('exports full references and properly escaped CSV without simulation fixtures by default', async () => {
    const rows = assemblyLines(await manifest()),
      text = assemblyCsv(rows);
    expect(text).toContain('"2","A.U1; B.U1"');
    expect(text).toContain('"Part, ""reviewed"""');
    expect(text).not.toContain('emblink/gnd');
    expect(assemblyCsv(rows, true)).toContain('simulation-fixture');
    expect(text.endsWith('\r\n')).toBe(true);
  });
  it('retains boards as assemblies and declares unavailable metadata/parameters', async () => {
    const data = await manifest();
    data.instances = [
      {
        path: 'B1',
        sheet: '',
        id: 'B1',
        kind: 'board',
        reference: 'test/board',
        catalog: 'board:test/board',
        resolution: 'board-assembly',
        params: {},
      },
      {
        path: 'X',
        sheet: '',
        id: 'X',
        kind: 'component',
        reference: 'missing/part',
        catalog: 'component:missing/part',
        resolution: 'unavailable',
        params: {},
      },
    ];
    const rows = assemblyLines(data);
    expect(rows).toHaveLength(2);
    expect(rows.find((r) => r.reference === 'missing/part')!.notes).toContain(
      'Native resolved parameters unavailable',
    );
    expect(assemblyCsv(rows)).toContain('board-assembly');
  });
  it('keeps demo hierarchy counts without labeling symbolic/default values native evidence', async () => {
    const p: Project = {
      ...project,
      components: [
        { id: 'A', component: 'block/leaf' },
        { id: 'B', component: 'block/leaf' },
      ],
      blocks: {
        leaf: {
          name: 'Leaf',
          ports: {},
          nets: {},
          components: [{ id: 'R', component: 'emblink/resistor', params: { resistance: '1k' } }],
        },
      },
    };
    const data = await verifyManifest(
      demoManifest(p, (id) => description(id), { policy: 'timed', now_ps: '123' }),
      subtle,
    );
    expect(data.source).toBe('demo');
    expect(data.policy).toBe('timed');
    expect(data.now_ps).toBe('123');
    expect(assemblyLines(data)[0]!.paths).toEqual(['A.R', 'B.R']);
    expect(assemblyLines(data)[0]!.resolution).toBe('demo');
  });
  it('does not merge unsafe integer previews or export executable spreadsheet text', async () => {
    const data = await manifest();
    data.instances = data.instances.slice(0, 2).map((i) => ({ ...i, params: { seed: 1e20 } }));
    const desc = JSON.parse(data.catalog['component:test/ic']!.json);
    desc.name = '=SUM(1,2)';
    data.catalog['component:test/ic']!.json = JSON.stringify(desc);
    const lines = assemblyLines(data);
    expect(lines).toHaveLength(2);
    expect(lines.every((line) => line.unsafeIntegerPreview)).toBe(true);
    const csv = assemblyCsv(lines);
    expect(csv).toContain("'=SUM(1,2)");
    expect(csv).toContain('Refer to native manifest for exact large integers');
    expect(csv).not.toContain('100000000000000000000');
  });
});
