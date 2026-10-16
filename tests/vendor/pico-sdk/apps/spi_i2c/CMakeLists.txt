# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# spi_i2c, as a Pico SDK project's own CMakeLists.txt has it
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(spi_i2c C CXX ASM)
pico_sdk_init()
add_executable(spi_i2c main.c)
target_link_libraries(spi_i2c pico_stdlib hardware_spi hardware_i2c)
pico_enable_stdio_uart(spi_i2c 1)
pico_enable_stdio_usb(spi_i2c 0)
