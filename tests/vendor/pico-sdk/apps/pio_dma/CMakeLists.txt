# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# pio_dma, as a Pico SDK project's own CMakeLists.txt has it: PIO programs
# assembled by the SDK's pioasm, hardware_pio and hardware_dma
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pio_dma C CXX ASM)
pico_sdk_init()
add_executable(pio_dma main.c)
pico_generate_pio_header(pio_dma ${CMAKE_CURRENT_LIST_DIR}/serial.pio)
target_link_libraries(pio_dma pico_stdlib hardware_pio hardware_dma hardware_adc)
pico_enable_stdio_uart(pio_dma 1)
pico_enable_stdio_usb(pio_dma 0)
