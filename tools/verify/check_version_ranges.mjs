// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
// Optional independent oracle: use an installed npm semver package, no download.
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { resolve } from 'node:path';
const require = createRequire(import.meta.url);
const modulePath = process.argv[2];
if (!modulePath) throw new Error('usage: node tools/verify/check_version_ranges.mjs PATH_TO_NPM_SEMVER_PACKAGE');
const semver = require(resolve(modulePath));
const version = require(resolve(modulePath, 'package.json')).version;
const vectors = JSON.parse(readFileSync(new URL('../../tests/fixtures/version-ranges.json', import.meta.url), 'utf8'));
let checked = 0;
for (const vector of vectors) {
  if (vector.expected < 0) continue; // EmbSIM deliberately bounds/restricts syntax.
  const normalized = vector.range.replaceAll(',', ' ').replace(/([0-9a-z])([<>=])/gi, '$1 $2');
  const actual = Number(semver.satisfies(vector.version, normalized));
  if (actual !== vector.expected) throw new Error(JSON.stringify({...vector, actual, normalized}));
  checked++;
}
console.log(`${checked} supported range cases agree with npm semver ${version}; conjunction separators normalized`);
