# M4 core families and boards, part 1: what was built and how it was checked

This page records the first part of M4: two more core families, each with
an MCU and a board that run real firmware, and projects with several
boards. It is the source for the M4 rows of [STATUS.md](../STATUS.md);
update both together. PIC24, PIC32, the nRF52840 DK, the Raspberry Pi Pico
and the Explorer 16/32 are the rest of M4 and are not here.

## What exists

| Area | Files | Contract |
|---|---|---|
| AVR core | `core/mcu/avr/avr.c`, `cost.h`, `disasm.c` | SPEC-009 §3: EmbCC's AVR5 core with its state a parameter; SREG exactly; cycle-exact timing (ADR-015); interrupts through the vector table; SLEEP and a jump to itself as the runtime's sleep |
| ATmega328P | `core/mcu/avr/port.c`, `exint.c`, `timer.c`, `usart.c`, `adc.c`, `eeprom.c`, `cpu.c`; `catalog/mcus/microchip/atmega328p` | SPEC-009 §6: ports with pull-ups, INT0/INT1 and pin-change, the three timers in every waveform mode with their OC pins, USART0, the ADC, the EEPROM, the clock prescaler; registers written from the datasheet |
| RISC-V core | `core/mcu/riscv/riscv.c`, `cost.h`, `disasm.c` | SPEC-009 §3: EmbCC's RV32IMAC core with its state a parameter; machine-mode traps; interrupt lines from the devices; WFI as the runtime's sleep |
| FE310-G002 | `core/mcu/riscv/clint.c`, `plic.c`, `gpio.c`, `uart.c`, `prci.c`; `catalog/mcus/sifive/fe310_g002` | SPEC-009 §6: the CLINT's mtime at the 32.768 kHz RTC, the PLIC's claim and complete, the GPIO's interrupts and IOFs, the UARTs, the PRCI's clocks; registers written from the manual |
| Boards | `catalog/boards/arduino_uno`, `catalog/boards/hifive1_revb` | SPEC-006 §3: artwork, ports, LEDs, the reset button, a power fixture, toolchain entries and linker scripts |
| Runtime and register file | `core/mcu/mach.h`, `build.c`, `rf.c`, `mcu.c`, `dwarf.c`, `session/debug.c` | backends chosen by the description; 8-bit registers and 24-bit offsets; pins named GPIOn; a pin's voltage for an ADC; a disassembler per core; DWARF with 2-byte addresses; registers of any width |
| Multi-board | `examples/two-boards-uart`, `tests/protocol/test_multiboard.py` | two MCUs of different families on one session's time, connected by nets |
| Workbench | `ui/packages/scene/src/svg.ts`, `BoardView.tsx`, `apps/workbench/src/perspectives/BoardPanel.tsx` | each board's artwork scoped (its styles and gradient ids), an LED's colour from its parameter, registers shown at their width |

## How it was checked

| Check | Where | What it shows |
|---|---|---|
| AVR | `test_avr` (also under ASan and UBSan, Clang and TinyCC) | flags and cycle counts of ADD, a counted loop, MUL; Timer0's CTC interrupt ten times in 10 ms; a USART frame's 16,640 cycles at 9600 baud; ports, the pull-up, PINx toggling; the ADC's 512 at 2.5 V; the disassembler |
| RISC-V | `test_riscv` (the same builds) | ALU, MUL, DIV and compressed cycles; an ECALL's trap; the CLINT's timer interrupt at 14,400,000 cycles, one second of the RTC at 14.4 MHz; a UART frame's bit time; the disassembler |
| The Uno | `examples/uno-blink-serial` 7/7 | firmware built by clang and by EmbCC: the banner, ADC readings of a potentiometer, a button on INT0, the L LED's 1 Hz from Timer0, D9's PWM at 976.6 Hz, sleep between events |
| The HiFive1 | `examples/hifive1-blink` 7/7 | firmware built by clang and by EmbCC: the PLL bypassed to the crystal, the green LED's 1 Hz from the CLINT, a button through the PLIC, a terminal's text echoed in upper case |
| Builds | `firmware_build` | `embsim build` with clang and EmbCC for the Blue Pill, the Uno and the HiFive1, each image's scenario passing |
| Two boards | `multiboard` | the Uno and the Blue Pill exchange ping and PING at their real 117647 and 115200 baud and 5 V and 3.3 V levels; two runs agree on every event and analog step |
| Debug | by hand through the protocol | breakpoints by symbol and line, registers, disassembly and the peripheral view on the AVR and the RISC-V core |
| Gallery | `scripts/screenshots.mjs` | the [M4 shots](../gallery/README.md): the boards' artwork live, the Debug perspective stopped in an AVR and a RISC-V interrupt handler, two boards on one schematic |

## Known gaps

PIC24, PIC32, the nRF52840 DK, the Pico and the Explorer 16/32; EmbCC's
QEMU-refereed golden fixtures for the AVR and RISC-V are not run here (QEMU
is not installed), the cores' own tests above stand for them. The
ATmega328P's SPI, TWI, watchdog and analog comparator, and the FE310's
always-on domain, SPI, PWM and I2C, are register files without behaviour.
The register view lists registers numbered below 64, so a RISC-V core's
CSRs are not shown; the memory view starts at 0x20000000 whatever the core.
A supply symbol (`emblink/vcc`) on a net a regulator's output drives is not
added as a source: power a board's regulated rail from a voltage source.
