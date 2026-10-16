/* SPDX-License-Identifier: Apache-2.0 */
/* nrf52840.ld -- the nRF52840's memory: 1 MiB of flash from 0, the vector
 * table first; 256 KiB of Data RAM at 0x20000000. .data is copied from flash
 * by the startup. */
ENTRY(reset_handler)
MEMORY {
  flash (rx)  : ORIGIN = 0x00000000, LENGTH = 1024K
  ram   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > flash
  .ARM.exidx : {
    *(.ARM.exidx*)
  } > flash
  .data : {
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram);
}
