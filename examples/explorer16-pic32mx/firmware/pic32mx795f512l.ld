/* SPDX-License-Identifier: Apache-2.0 */
/* pic32mx795f512l.ld -- the PIC32MX795F512L's memory, for a program linked
 * at its virtual addresses: the reset vector and the startup in boot flash
 * (kseg1 0xBFC00000), the configuration words in its last 16 bytes
 * (0xBFC02FF0: DEVCFG3, DEVCFG2, DEVCFG1, DEVCFG0); the program and its
 * exception and interrupt vectors in program flash (kseg0 0x9D000000; EBASE
 * is the .vectors section's 4 KiB-aligned start, each vector 32 bytes from
 * EBASE + 0x200); data, the heap and the stack in RAM (kseg0 0x80000000).
 * .data is copied from flash by the startup. */
ENTRY(_reset)
MEMORY {
  boot   (rx)  : ORIGIN = 0xBFC00000, LENGTH = 0x2FF0
  config (r)   : ORIGIN = 0xBFC02FF0, LENGTH = 16
  flash  (rx)  : ORIGIN = 0x9D000000, LENGTH = 512K
  ram    (rwx) : ORIGIN = 0x80000000, LENGTH = 128K
}
SECTIONS {
  .reset : {
    KEEP(*(.reset))
    *(.boot*)
  } > boot
  .config : {
    KEEP(*(.config))
  } > config
  .vectors : ALIGN(4096) {
    __ebase = .;
    . = __ebase + 0x180;
    KEEP(*(.vector_general))
    . = __ebase + 0x200;
    KEEP(*(.vectors))
  } > flash
  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > flash
  .got : {
    *(.got*)
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
    *(.bss*)
    *(.sbss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > ram
  __stack_top = ORIGIN(ram) + LENGTH(ram);
  /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.gnu.attributes) }
}
