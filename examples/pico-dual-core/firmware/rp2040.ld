/* SPDX-License-Identifier: Apache-2.0 */
/* rp2040.ld -- the Raspberry Pi Pico's memory: the flash read in place from
 * 0x10000000, its first 256 bytes boot2 (which the bootrom checks, copies
 * to SRAM and enters), the vector table after it; 256 KiB of striped SRAM
 * at 0x20000000 (SRAM4 and SRAM5 above it hold boot2's copy and the
 * bootrom's stack while booting). .data is copied from flash by the
 * startup. */
ENTRY(reset_handler)
MEMORY {
  flash (rx)  : ORIGIN = 0x10000000, LENGTH = 2048K
  ram   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
SECTIONS {
  .boot2 : {
    KEEP(*(.boot2))
  } > flash
  ASSERT(SIZEOF(.boot2) == 256, "boot2 must be 256 bytes")
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
