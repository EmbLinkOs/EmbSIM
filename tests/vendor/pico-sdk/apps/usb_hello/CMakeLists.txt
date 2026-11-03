# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(usb_hello C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
add_executable(usb_hello main.c)
target_link_libraries(usb_hello pico_stdlib)
# stdio over USB (TinyUSB's CDC ACM), as the SDK's hello_usb example
pico_enable_stdio_usb(usb_hello 1)
pico_enable_stdio_uart(usb_hello 0)
