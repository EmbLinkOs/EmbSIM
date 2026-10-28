/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_VERSION_H
#define ESK_VERSION_H
/* Bounded SemVer precedence and conjunction ranges. 1 match, 0 mismatch,
 * -1 malformed/unsupported. See engineering/library-versions.md for grammar. */
int esk_version_satisfies(const char *version, const char *range);
#endif
