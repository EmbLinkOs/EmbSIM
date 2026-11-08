# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
idf_component_register(SRCS "cycle_probe.c" INCLUDE_DIRS "."
                       REQUIRES esp_hw_support freertos esp_system)
