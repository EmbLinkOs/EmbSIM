# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Thin wrapper over the CMake presets for Unix users. `make CC=tcc` builds with TinyCC.
PRESET ?= dev
CC ?= cc
BUILD = build/$(PRESET)

.PHONY: all test clean examples format
all:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="$(CC)" >/dev/null
	cmake --build $(BUILD) --parallel

test: all
	ctest --test-dir $(BUILD) --output-on-failure --no-tests=error

examples: all
	python3 tools/ci/run_examples.py --build $(BUILD)

clean:
	rm -rf build
