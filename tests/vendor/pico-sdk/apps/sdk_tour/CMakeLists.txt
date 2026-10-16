# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# sdk_tour, as a Pico SDK project's own CMakeLists.txt has it
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(sdk_tour C CXX ASM)
pico_sdk_init()
add_executable(sdk_tour main.c)
target_link_libraries(sdk_tour pico_stdlib pico_multicore hardware_flash)
pico_enable_stdio_uart(sdk_tour 1)
pico_enable_stdio_usb(sdk_tour 0)
