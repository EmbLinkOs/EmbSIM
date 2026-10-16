// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { describe, expect, it } from 'vitest';
import type { Project, ComponentDescription } from '@embsim/protocol';
import { commonParameters, parameterValue } from './parameterEditing';
import type { CatalogEntry } from '../../stores/project';

describe('shared property editing', () => {
  it('offers only compatible definitions and keeps mixed values explicit', () => {
    const p: Project = { schema_version: 1, kind: 'project', name: 't', components: [{ id: 'R1', component: 'r1', params: { resistance: '1k' } }, { id: 'R2', component: 'r2' }], nets: {} };
    const entry = (max: number, value: string): CatalogEntry => ({ description: { params: { resistance: { type: 'number', unit: 'ohm', default: value, min: 0, max }, table: { type: 'table' } } } as unknown as ComponentDescription, files: {} });
    const catalog = { r1: entry(10000, '1k'), r2: entry(10000, '2k') };
    expect(commonParameters(p, catalog, ['R1', 'R2'])).toMatchObject([{ name: 'resistance', value: '1k', mixed: true }]);
    catalog.r2 = entry(20000, '2k');
    expect(commonParameters(p, catalog, ['R1', 'R2'])).toEqual([]);
    expect(commonParameters(p, catalog, ['R1', 'BOARD'])).toEqual([]);
  });
  it('validates SI bounds, integers and explicit boolean/enum choices', () => {
    expect(parameterValue({ type: 'number', min: 1, max: '10k' }, '4.7k')).toEqual({ value: '4.7k' });
    expect(parameterValue({ type: 'number', max: '10k' }, '11k').error).toBeTruthy();
    expect(parameterValue({ type: 'integer' }, '1.5').error).toBeTruthy();
    expect(parameterValue({ type: 'number' }, 'Infinity').error).toBeTruthy();
    expect(parameterValue({ type: 'number' }, '').error).toBeTruthy();
    expect(parameterValue({ type: 'boolean' }, '').error).toBeTruthy();
    expect(parameterValue({ type: 'boolean' }, 'false')).toEqual({ value: false });
    expect(parameterValue({ type: 'enum', enum: ['red', 'green'] }, 'blue').error).toBeTruthy();
  });
});
