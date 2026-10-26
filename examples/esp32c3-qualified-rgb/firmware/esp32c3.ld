/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
ENTRY(_start)
MEMORY {
  IRAM (rx) : ORIGIN = 0x40380000, LENGTH = 16K
  DRAM (rw) : ORIGIN = 0x3FC84000, LENGTH = 368K
}
SECTIONS {
  .text : { KEEP(*(.text.start)) *(.text*) *(.rodata*) } > IRAM
  .data : { *(.sdata*) *(.data*) } > DRAM
  .bss (NOLOAD) : { *(.sbss*) *(.bss*) *(COMMON) } > DRAM
  __stack_top = ORIGIN(DRAM) + LENGTH(DRAM);
}
