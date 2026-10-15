#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Apply .clang-format to the C sources (CS-25). Pass --check to verify only.
set -e
cd "$(dirname "$0")/.."
files=$(find include core cli tests -name '*.c' -o -name '*.h' | grep -v third_party)
if [ "$1" = "--check" ]; then
  clang-format --dry-run --Werror $files
else
  clang-format -i $files
fi
