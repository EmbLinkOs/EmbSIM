// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import { JsonRpcError } from '@embsim/protocol';
import { expect, it } from 'vitest';
import { errorMessage } from './actions';

it('shows the native diagnostic that explains a rejected edit', () => {
  expect(errorMessage(new JsonRpcError(-32007, 'description failed validation', {code: 'catalog.version.mismatch', message: 'R requests ^2; selected 1.0.0'}))).toBe('R requests ^2; selected 1.0.0 (catalog.version.mismatch)');
  expect(errorMessage(new JsonRpcError(-32602, 'bad request', {message: 'Expected a valid request'}))).toBe('Expected a valid request (-32602)');
});
it('retains protocol and ordinary errors without invented diagnostic details', () => {
  expect(errorMessage(new JsonRpcError(-32602, 'bad request', {message: 42}))).toBe('bad request (-32602)');
  expect(errorMessage(new Error('Missing library'))).toBe('Missing library');
  expect(errorMessage('Unavailable')).toBe('Unavailable');
});
