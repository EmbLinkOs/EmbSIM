/* SPDX-License-Identifier: Apache-2.0 */
/* atmega328p.ld -- the ATmega328P's memory for clang and lld: flash at 0,
 * the data space at 0x800000 as the AVR toolchains number it, SRAM from
 * 0x100. Read-only data goes with .data into SRAM (copied from flash by the
 * startup code), since a pointer dereference reads the data space. */
ENTRY(__vectors)
MEMORY {
  flash (rx)  : ORIGIN = 0, LENGTH = 32K
  ram   (rw!x) : ORIGIN = 0x800100, LENGTH = 2K
}
SECTIONS {
  .text : {
    KEEP(*(.vectors))
    *(.text*)
    *(.progmem*)
    . = ALIGN(2);
  } > flash
  .data : {
    __data_start = .;
    *(.data*)
    *(.rodata*)
    . = ALIGN(2);
    __data_end = .;
  } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  } > ram
  __stack = 0x8008ff;
}
