/* SPDX-License-Identifier: Apache-2.0 */
/* fe310.ld -- the HiFive1 Rev B's memory for user code: the flash read in
 * place from 0x20010000 (the bootloader keeps the first 64 KiB) and the
 * 16 KiB DTIM at 0x80000000. .data is copied from flash by the startup. */
ENTRY(_start)
MEMORY {
  flash (rx)  : ORIGIN = 0x20010000, LENGTH = 4M - 64K
  ram   (rw!x) : ORIGIN = 0x80000000, LENGTH = 16K
}
SECTIONS {
  .text : {
    KEEP(*(.text.init))
    *(.text*)
    *(.rodata*)
    *(.srodata*)
    . = ALIGN(4);
  } > flash
  .data : {
    __data_start = .;
    *(.data*)
    *(.sdata*)
    . = ALIGN(4);
    __data_end = .;
  } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.sbss*)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram);
  __global_pointer$ = __data_start + 0x800;
}
