// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
import js from '@eslint/js';
import embsim from 'eslint-plugin-embsim';
import reactHooks from 'eslint-plugin-react-hooks';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  { ignores: ['**/node_modules/**', '**/dist/**', '**/playwright-report/**', '**/test-results/**', '**/*.d.ts'] },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['**/*.{ts,tsx}'],
    languageOptions: { globals: { ...globals.browser, ...globals.node } },
    plugins: { 'react-hooks': reactHooks, embsim },
    rules: {
      'react-hooks/rules-of-hooks': 'error',
      'react-hooks/exhaustive-deps': 'warn',
      'embsim/no-simulation-imports': 'error',
      'embsim/io-only-in-protocol': 'error',
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      '@typescript-eslint/consistent-type-imports': ['error', { fixStyle: 'inline-type-imports' }],
      'no-restricted-imports': [
        'error',
        {
          paths: [
            { name: 'ws', message: 'Transports live in packages/protocol.' },
            { name: '@tauri-apps/api', message: 'Use the platform adapter (apps/workbench/src/platform).' },
          ],
          patterns: [{ group: ['@tauri-apps/*'], message: 'Only the platform adapter may import Tauri APIs.' }],
        },
      ],
    },
  },
  {
    // The platform adapter is the one place Tauri APIs are imported.
    files: ['apps/workbench/src/platform/tauri.ts'],
    rules: { 'no-restricted-imports': 'off' },
  },
  {
    files: ['**/*.js', '**/*.config.{js,ts}', 'tools/**'],
    languageOptions: { globals: { ...globals.node } },
  },
);
