// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { parseSi, type ParamDescription, type Project } from '@embsim/protocol';
import type { CatalogEntry } from '../../stores/project';

export function commonParameters(project: Project, catalog: Record<string, CatalogEntry>, ids: string[]) {
  const components = ids.map((id) => project.components.find((c) => c.id === id));
  if (!components.length || components.some((c) => !c)) return [];
  const definitions = components.map((c) => catalog[c!.component]?.description.params);
  if (definitions.some((d) => !d)) return [];
  const signature = (p: ParamDescription) => JSON.stringify([p.type, p.unit, p.min, p.max, p.enum]);
  return Object.entries(definitions[0]!).flatMap(([name, param]) => {
    if (param.type === 'table') return [];
    if (!definitions.every((d) => d?.[name] && signature(d[name]!) === signature(param))) return [];
    const values = components.map((c, i) => c!.params?.[name] ?? definitions[i]![name]!.default);
    return [{ name, param, value: values[0], mixed: values.some((v) => v !== values[0]) }];
  });
}

export function parameterValue(param: ParamDescription, text: string): { value?: string | number | boolean; error?: string } {
  if (param.type === 'boolean') return text === 'true' || text === 'false' ? { value: text === 'true' } : { error: 'Choose true or false.' };
  if (param.type === 'enum') return param.enum?.includes(text) ? { value: text } : { error: 'Choose a listed value.' };
  if (param.type === 'number' || param.type === 'integer') {
    const n = parseSi(text);
    if (n === undefined || !Number.isFinite(n) || !text.trim()) return { error: 'Enter a finite number; SI suffixes are supported.' };
    if (param.type === 'integer' && !Number.isInteger(n)) return { error: 'Enter a whole number.' };
    const min = parseSi(param.min), max = parseSi(param.max);
    if ((min !== undefined && n < min) || (max !== undefined && n > max)) return { error: `Value must be between ${param.min ?? '−∞'} and ${param.max ?? '∞'}.` };
    return { value: Number.isFinite(Number(text)) ? Number(text) : text.trim() };
  }
  return { value: text };
}
