// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { hierarchyOf, sheetAt } from './hierarchy';
import type {
  ComponentDescription,
  EngineeringManifest,
  EngineeringManifestData,
  ManifestDocument,
  ManifestInstance,
  Project,
} from './types';

/** Native manifests carry exact JSON bytes rather than requiring a matching serializer. */
export async function verifyManifest(
  doc: ManifestDocument,
  subtle: SubtleCrypto | null | undefined = globalThis.crypto?.subtle,
): Promise<EngineeringManifestData> {
  const data = JSON.parse(doc.json) as EngineeringManifestData;
  if (
    data.schema_version !== 1 ||
    data.kind !== 'engineering_manifest' ||
    !['native', 'demo'].includes(data.source)
  )
    throw new Error('Unsupported engineering manifest');
  if (data.source === 'native') {
    if (!subtle) throw new Error('SHA-256 verification needs HTTPS or localhost');
    const verify = async (entry: ManifestDocument) => {
      const bytes = new TextEncoder().encode(entry.json);
      const hash = await subtle.digest('SHA-256', bytes);
      const text =
        'sha256:' + Array.from(new Uint8Array(hash), (b) => b.toString(16).padStart(2, '0')).join('');
      if (text !== entry.sha256) throw new Error('Engineering manifest content hash mismatch');
    };
    await verify(doc);
    await verify(data.project);
    for (const entry of Object.values(data.catalog)) await verify(entry);
  }
  return data;
}

export interface AssemblyLine {
  reference: string;
  version: string;
  name: string;
  manufacturer: string;
  mpn: string;
  package: string;
  paths: string[];
  instances: ManifestInstance[];
  params: Record<string, unknown>;
  unsafeIntegerPreview: boolean;
  simulationFixture: boolean;
  resolution: ManifestInstance['resolution'];
  catalog: string;
  notes: string[];
}
export const FIXTURE_REFERENCES = new Set([
  'emblink/gnd',
  'emblink/vcc',
  'emblink/vsource',
  'emblink/isource',
  'emblink/logic_level',
  'emblink/clock_source',
  'emblink/logic_probe',
  'emblink/serial_terminal',
  'emblink/voltmeter',
  'emblink/ammeter',
  'emblink/scope_input',
  'emblink/function_generator',
]);
export function stableJson(value: unknown): string {
  const normalize = (v: unknown): unknown =>
    Array.isArray(v)
      ? v.map(normalize)
      : v && typeof v === 'object'
        ? Object.fromEntries(
            Object.entries(v)
              .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))
              .map(([k, x]) => [k, normalize(x)]),
          )
        : v;
  return JSON.stringify(normalize(value));
}

function unsafeInteger(value: unknown): boolean {
  if (typeof value === 'number') return Number.isInteger(value) && !Number.isSafeInteger(value);
  if (Array.isArray(value)) return value.some(unsafeInteger);
  return Boolean(value && typeof value === 'object' && Object.values(value).some(unsafeInteger));
}

/** Group actual configured packages, never the implementation primitives inside them. */
export function assemblyLines(data: EngineeringManifestData): AssemblyLine[] {
  const groups = new Map<string, AssemblyLine>();
  const descriptions = new Map(
    Object.entries(data.catalog).map(([key, value]) => [key, JSON.parse(value.json) as ComponentDescription]),
  );
  for (const item of data.instances) {
    const desc = descriptions.get(item.catalog),
      version = desc?.version ?? '';
    const unsafe = unsafeInteger(item.params);
    const key = stableJson([
      item.kind,
      item.reference,
      data.catalog[item.catalog]?.sha256 ?? '',
      item.resolution,
      item.params,
      unsafe ? item.path : null,
    ]);
    let group = groups.get(key);
    if (!group) {
      const notes: string[] = [];
      if (unsafe) notes.push('Large integer parameters require the native manifest for exact values');
      if (!desc) notes.push('Loaded description unavailable');
      if (item.resolution === 'unavailable' || item.resolution === 'demo')
        notes.push('Native resolved parameters unavailable');
      if (!desc?.part?.mpn) notes.push('Procurement part not assigned');
      group = {
        reference: item.reference,
        version,
        name: desc?.name ?? item.reference,
        manufacturer: desc?.part?.manufacturer ?? '',
        mpn: desc?.part?.mpn ?? '',
        package: desc?.part?.package ?? desc?.bench?.package ?? '',
        paths: [],
        instances: [],
        params: item.params,
        unsafeIntegerPreview: unsafe,
        simulationFixture: item.kind === 'component' && FIXTURE_REFERENCES.has(item.reference),
        resolution: item.resolution,
        catalog: item.catalog,
        notes,
      };
      groups.set(key, group);
    }
    if (item.requested_version && item.requested_version !== version && item.version_satisfied !== true) {
      const warning = `${item.path}: requested ${item.requested_version}, loaded ${version || 'unknown'}`;
      if (!group.notes.includes(warning)) group.notes.push(warning);
    }
    group.paths.push(item.path);
    group.instances.push(item);
  }
  for (const group of groups.values()) {
    group.paths.sort();
    group.instances.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  }
  return [...groups.values()].sort((a, b) =>
    a.reference < b.reference
      ? -1
      : a.reference > b.reference
        ? 1
        : stableJson(a.params).localeCompare(stableJson(b.params), 'en'),
  );
}

/** RFC 4180 text; JSON export retains typed configurations and full evidence. */
export function assemblyCsv(lines: AssemblyLine[], includeFixtures = false): string {
  const cell = (v: unknown) => {
    const text = String(v),
      literal = /^[\s]*[=+@-]/.test(text) || /^[\t\r\n]/.test(text) ? "'" + text : text;
    return '"' + literal.replace(/"/g, '""') + '"';
  };
  const rows: unknown[][] = [
    [
      'Quantity',
      'References',
      'Catalog reference',
      'Loaded version',
      'Name',
      'Manufacturer',
      'MPN',
      'Package',
      'Resolved model configuration',
      'Classification',
      'Resolution',
      'Notes',
    ],
  ];
  for (const line of lines.filter((l) => includeFixtures || !l.simulationFixture))
    rows.push([
      line.paths.length,
      line.paths.join('; '),
      line.reference,
      line.version,
      line.name,
      line.manufacturer,
      line.mpn,
      line.package,
      line.unsafeIntegerPreview
        ? 'Refer to native manifest for exact large integers'
        : stableJson(line.params),
      line.simulationFixture
        ? 'simulation-fixture'
        : line.instances[0]?.kind === 'board'
          ? 'board-assembly'
          : 'component',
      line.resolution,
      line.notes.join('; '),
    ]);
  return rows.map((row) => row.map(cell).join(',')).join('\r\n') + '\r\n';
}

/** Demo metadata is explicitly unverified and is never native electrical evidence. */
export function demoManifest(
  p: Project,
  description: (id: string) => ComponentDescription | undefined,
  context: Partial<Pick<EngineeringManifestData, 'policy' | 'now_ps'>> = {},
): EngineeringManifest {
  const doc = (value: unknown): ManifestDocument => ({ json: JSON.stringify(value), sha256: '' });
  const data: EngineeringManifestData = {
    schema_version: 1,
    kind: 'engineering_manifest',
    source: 'demo',
    engine: '0.1.0-mock',
    abi: 1,
    now_ps: context.now_ps ?? '0',
    policy: context.policy ?? p.policy ?? 'electrical',
    project: doc(p),
    catalog: {},
    instances: [],
    models: [],
    libraries: [],
  };
  const add = (
    sheet: string,
    kind: 'component' | 'board',
    id: string,
    ref: string,
    params: Record<string, unknown>,
  ) => {
    const key = `${kind}:${ref}`,
      desc = description(ref);
    if (desc) data.catalog[key] = doc(desc);
    data.instances.push({
      path: sheet ? `${sheet}.${id}` : id,
      sheet,
      id,
      reference: ref,
      kind,
      catalog: key,
      resolution: 'demo',
      params,
    });
  };
  for (const node of hierarchyOf(p))
    for (const c of sheetAt(p, node.path).sheet.components)
      if (!c.component.startsWith('block/')) add(node.path, 'component', c.id, c.component, c.params ?? {});
  for (const board of p.boards ?? []) add('', 'board', board.id, board.board, {});
  return { ...doc(data), schema_version: 1, kind: 'engineering_manifest' };
}
