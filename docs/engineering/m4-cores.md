# M4 core families and boards: what was built and how it was checked

This page records M4 as it is built: more core families, each with an MCU
and a board that run real firmware, and projects with several boards. It
is the source for the M4 rows of [STATUS.md](../STATUS.md); update both
together. Part 1 is the AVR, the RISC-V core and two boards on one line;
part 2 (below) the Raspberry Pi Pico and the nRF52840 DK; part 3 the
PIC32MX795F512L and the Explorer 16/32; part 4 the PIC24FJ1024GB610 on the
same board; part 5 the RP2040's second core.

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

PIC24, PIC32, the nRF52840 DK, the Pico and the Explorer 16/32 (since
added: parts 2 to 4 below); EmbCC's
QEMU-refereed golden fixtures for the AVR and RISC-V are not run here (QEMU
is not installed), the cores' own tests above stand for them. The
ATmega328P's SPI, TWI, watchdog and analog comparator, and the FE310's
always-on domain, SPI, PWM and I2C, are register files without behaviour.
The register view lists registers numbered below 64, so a RISC-V core's
CSRs are not shown; the memory view starts at 0x20000000 whatever the core.
A supply symbol (`emblink/vcc`) on a net a regulator's output drives is not
added as a source: power a board's regulated rail from a voltage source.

## Part 2: the Raspberry Pi Pico and the nRF52840 DK

| Area | Files | Contract |
|---|---|---|
| Register file | `core/mcu/rf.c`, `rf.h`, `build.c` | SPEC-009 §5: the MCU description's `register_file.bus_rules` (`atomic_aliases`: each register again at +0x1000 XOR, +0x2000 set, +0x3000 clear; `wide_writes`: a byte or halfword written across the word); a peripheral held in reset reads its reset values and ignores writes (`mcu.held_in_reset`) |
| RP2040 | `core/mcu/rp2040/`; `catalog/mcus/raspberrypi/rp2040` | SPEC-009 §6: RESETS, the clocks (XOSC, ROSC, PLL_SYS, PLL_USB, the generators, FC0; clk_sys is the core's clock), PSM, the watchdog's tick and reset, the timer, IO_BANK0 and the pads, the SIO, the UARTs, the SSI, from EmbCC; a stand-in for the bootrom that checks boot2's CRC, copies it to 0x20041f00 and enters it; registers converted from RP2040.svd (BSD-3-Clause, `LICENSES/`) |
| nRF52840 | `core/mcu/nrf52/`; `catalog/mcus/nordic/nrf52840` | SPEC-009 §6: Nordic's tasks, events and interrupts; CLOCK, the GPIO ports, the UARTs with EasyDMA, TIMER, RTC from EmbCC, GPIOTE for EmbSIM; registers converted from nrf52840.svd (BSD-3-Clause, `LICENSES/`), the peripherals that share an address space merged into one |
| Boards | `catalog/boards/pico`, `catalog/boards/nrf52840dk` | SPEC-006 §3: artwork, ports, LEDs, buttons, a power fixture, toolchain entries and linker scripts |
| Runtime | `core/mcu/mcu.c`, `core/engine/engine.c`, `core/project/loader.c` | a UART's RX pin found again when a pin's function changes; two weak pulls that disagree are not contention; a component's own parameters win over its preset's |
| Workbench | `DebugPerspective.tsx`, `session/debug.c` | a peripheral held in reset shown as such (`debug.peripherals`' `reset`) |

| Check | Where | What it shows |
|---|---|---|
| RP2040 | `test_rp2040` (705 checks) | the boot through boot2 and the USB-boot wait on a bad CRC; the aliases and byte writes; RESETS holding and releasing UART0; PLL_SYS at 125 MHz and FC0's measure of it; a timer alarm at 1000 ticks of the 1 µs tick; the divider; a UART frame's 1085-cycle bit at 115200 baud |
| nRF52840 | `test_nrf52` (117 checks) | FICR's words; the HFXO's 256 µs debounce; the ports' directions, the disconnected input buffer, pulls, GPIOTE's falling edge and task toggle; a UARTE buffer of two frames, TXDRDY and ENDTX; TIMER0's compare after 100 µs; RTC0's compare after 10 counts |
| The Pico | `examples/pico-blink-serial` 7/7 | firmware built by clang and by EmbCC that boots its own boot2: clk_sys 125000 kHz from FC0, the LED's 1 Hz from the timer's alarm, a button through IO_BANK0, a terminal's text echoed in upper case |
| The DK | `examples/nrf52840dk-blink` 7/7 | firmware built by clang and by EmbCC: the part number from FICR and sqrt(2) on the FPU, LED1's 1 Hz from RTC1, a button through GPIOTE, the terminal echoed through UARTE0's EasyDMA |
| Builds | `firmware_build` | the five boards' examples built by clang and by EmbCC, each scenario passing |
| Debug | `scripts/screenshots.mjs` | the Debug perspective stopped in the Pico's timer interrupt and the DK's RTC interrupt (the [M4 part 2 shots](../gallery/README.md)) |

Known gaps: the RP2040's bootrom's function table (core 1 since added: part
5 below), PIO, DMA,
SPI, I2C, PWM, the ADC, USB and the XIP cache; the nRF52840's radio, PPI,
the serial buses, SAADC, PWM and USB; Pico SDK and nrfx images are not run
(the examples use the registers directly).

## Part 3: the PIC32MX795F512L and the Explorer 16/32

| Area | Files | Contract |
|---|---|---|
| MIPS32 core | `core/mcu/mips/` | SPEC-009 §2: MIPS32r2 with MIPS16e from EmbCC, its state a parameter; the M4K (`pic32mx`: EIC mode, two register sets, fixed mapping, the core timer) and the 24Kc (`24kc`: compatibility-mode interrupts, the TLB); WAIT and an idle loop sleep for the runtime; the disassembler; images at their kseg0/kseg1 addresses through the core's `phys` hook |
| Register file | `core/mcu/rf.c`, `rf.h` | SPEC-009 §5: CLR, SET and INV registers (`clr_set_inv`), the description's interrupt `vectors` and pin functions (`pins`) |
| Importer | `tools/importers/edc2regs.py` | a PIC32 EDC from Microchip's Apache-2.0 device pack to a register description and a package; the vectors from the pack's ATDF |
| PIC32MX | `core/mcu/pic32/`; `catalog/mcus/microchip/pic32mx795f512l` | SPEC-009 §6: the EVIC (shared vectors, priorities, the shadow set), the oscillator from the configuration words with the PLL and SYSKEY's lock (SYSCLK is the core's clock), Timer1-5, ports A-G with the change notices, UART1-6 timed at their baud rate on their pins, from EmbCC; registers and the TQFP100 converted from Microchip.PIC32MX_DFP 1.7.380 (`LICENSES/`) |
| Board | `catalog/boards/explorer16-32` | SPEC-006 §3: the Explorer 16/32 with the PIC32MX795F512L PIM: D3-D10, S3, S4, S6, MCLR, UART2 on the USB-UART bridge, the crystals, a power fixture, the linker script |
| Runtime and tools | `core/project/loader.c`, `core/mcu/mcu.c`, `catalog/toolchains/`, `tools/validate.py` | PIC pin names (RA0) are GPIO; clang and EmbCC build `mipsel-none-elf`; a scenario may press a board's own button (`B1.S3`) |

| Check | Where | What it shows |
|---|---|---|
| MIPS32 | `test_mips` (168 checks) | ALU results and a delay slot, a syscall in a delay slot (EPC, Cause.BD), an address error and eret, the core timer in compatibility mode at Compare x 2 cycles and in EIC mode through a controller's vector, MIPS16e through jalx and EXTEND, ll and sc, the M4K's PRId and Config, the disassembler |
| PIC32MX | `test_pic32` | CLR, SET and INV on LATA and TRISA and the pins they drive; the clock from erased and set configuration words (4 MHz, 80 MHz), OSCCON locked and unlocked; Timer1's flag at 800 cycles; a change notice from the board; UART2's 696-cycle bit at 115200 baud and its persistent transmit interrupt; the example's image |
| The Explorer | `examples/explorer16-pic32mx` 8/8 | firmware built by clang and by EmbCC: SYSCLK 80000 kHz and PBCLK 40000 kHz from OSCCON and the configuration words, D3's 1 Hz from Timer1's interrupt, S3 through the change notice counted on D4, a terminal's text echoed in upper case through UART2's receive interrupt |
| Builds | `firmware_build` | the six boards' examples built by clang and by EmbCC, each scenario passing |

Known gaps: the PIC32MX's ADC, SPI, I2C, input capture, output compare,
PMP, RTCC, DMA, USB, Ethernet, CAN and the watchdog (plain registers); the
prefetch cache and the bus matrix's partitions; the FPU, DSP ASE and
microMIPS of the PIC32MZ's core; the Explorer's LCD, EEPROM, potentiometer
and temperature sensor. XC32 and Harmony images are not run here (the
example uses the registers directly).

## Part 4: the PIC24FJ1024GB610 on the Explorer 16/32

| Area | Files | Contract |
|---|---|---|
| PIC24 core | `core/mcu/pic24/pic24.c`, `dsp.c`, `disasm.c` | SPEC-009 §2: EmbCC's PIC24F core (and its dsPIC33E mode with the DSP engine) with its state a parameter: the instruction set and addressing modes, REPEAT and the iterative divide, the Extended Data Space and program space visibility, table reads, traps (`mcu.trap`) and interrupts through the vector table, DISI; PWRSAV and a branch to itself sleep for the runtime; program memory at twice its address (the `phys` hook), the data space at 0x2000000; the disassembler |
| Register file | `core/mcu/rf.c`, `rf.h` | SPEC-009 §5: the description's `config_words` and `pps_outputs` |
| Importer | `tools/importers/edc2regs.py` | a PIC24's EDC (its data space; the core's registers left out; the UARTs one a peripheral; the PPS functions; the configuration words) from Microchip's Apache-2.0 device pack |
| PIC24FJ1024GB610 | `core/mcu/pic24/intc.c`, `osc.c`, `tmr.c`, `gpio.c`, `uart.c`; `catalog/mcus/microchip/pic24fj1024gb610` | SPEC-009 §6: the interrupt controller (priorities, the natural order, INTTREG, the trap flags), the oscillator (FNOSC from FOSCSEL, OSCCON's unlock sequences and the switch, the PLL's 32 MHz and CPDIV: FCY is the core's clock) with the PPS lock, Timer1-5, ports A-G with ANS, the pulls and the interrupt-on-change, from EmbCC; UART1-6 written for EmbSIM, timed at their baud rate on the pins the PPS gives them; registers and the TQFP100 converted from Microchip.PIC24F-GA-GB_DFP 1.11.479 (`LICENSES/`) |
| Board | `catalog/boards/explorer16-32-pic24` | SPEC-006 §3: the Explorer 16/32 with the PIC24FJ1024GB610 PIM (MA240023): the same LEDs, buttons and USB-UART bridge, the XC16 target |
| Toolchains | `catalog/toolchains/xc16.json`, `core/project/toolchain.c` | the XC16 target `pic24fj1024gb610` with the part's linker script; a profile's `link_flags` may name the build's variables (`-Wl,-Map=${map}`) |
| Debug | `core/mcu/dwarf.c`, `scripts/screenshots.mjs` | XC16's DWARF (its debug sections hold a byte in each 16-bit unit) read packed, so the source view follows the PIC24; the [M4 part 4 shots](../gallery/README.md) |

| Check | Where | What it shows |
|---|---|---|
| PIC24 | `test_pic24` (1465 checks) | instructions as xc16-as assembles them and their cycles (GOTO, REPEAT and DIV.U's 18 passes: 30 cycles to the branch to itself, then asleep); program memory's layout; an address error trap through its vector with INTCON1.ADDRERR; Timer1's interrupt taken from PWRSAV ten times in 1000 cycles, INTTREG; FCY from erased and set configuration words (4 and 16 MHz), CPDIV, OSCCON locked and unlocked; Timer1's flag at 100 cycles and at 1:256; the interrupt-on-change from the board, IOCFx cleared only; UART2 on RF5 and RF4 through the PPS, its 140-cycle bit at 115200 baud, IOLOCK; the disassembler; the example's image and its line table |
| The Explorer | `examples/explorer16-pic24` 8/8 | firmware built by XC16 v2.10: FOSC 32000 kHz and FCY 16000 kHz from OSCCON and CLKDIV, FOSCSEL read back by a table read, D3's 1 Hz from Timer1's interrupt, S3 through the interrupt-on-change counted on D4, a terminal's text echoed in upper case through UART2's receive interrupt, the core idle between them |
| Builds | by hand | `embsim build` with XC16 v2.10 (free mode) here; CI has no XC16, so the committed image is what `firmware_build` does not rebuild |

Known gaps: the PIC24FJ1024GB610's ADC, SPI, I2C, input capture, output
compare, the MCCPs and SCCPs, the EPMP, the RTCC, DMA, USB, the
comparators, the CRC, the CLCs, the CTMU, HLVD and the reference clock
(plain registers); the watchdog and deep sleep; flash programming and the
dual partitions; the PLL's 4x, 6x and 8x modes and the DCO; the external
interrupt pins. A data-space segment with contents in an image is loaded as
program memory (XC16 puts initial data in `.dinit`, so its images have
none). No dsPIC33 part is in the catalog, and the core's dsPIC33E mode
(the DSP engine, the DO loops) is not tested here.

## Part 5: the RP2040's second core

| Area | Files | Contract |
|---|---|---|
| Cores | `core/mcu/cortexm/smp.c`, `cortexm.c`, `thumb.c`, `systick.c`, `build.c` | SPEC-009 §3: `core.cores` cores of one model on one machine, each on its own time and the machine on the earliest running core's; each core's NVIC, SCB, SysTick and DWT reached by its own accesses; WFE and SEV with the event register (a single core's WFE after its own SEV goes on at once); a core asleep alone; cores past core 0 wait to be launched; the debugger sees the core that runs next |
| RP2040 | `core/mcu/rp2040/sio.c`, `io.c`, `clocks.c`; `catalog/mcus/raspberrypi/rp2040` (`cores` 2) | SPEC-009 §6: CPUID, a divider and the FIFOs per core, SIO_IRQ_PROC0 and PROC1 to their own core; IO_BANK0's PROC0 and PROC1 interrupt summaries to their own core; PSM's FRCE_OFF.PROC1 powers core 1 off and on; core 1's bootrom answered by the SIO: out of reset it sends core 0 a 0, then echoes each word and starts core 1 after 0, 0, 1, the vector table, the stack and the entry |
| Example | `examples/pico-dual-core` | firmware on the registers that launches core 1 as the pico-sdk's `multicore_launch_core1` does |
| Debug | `core/session/debug.c`, `DebugPerspective.tsx` | `debug.where` names the core (`core`) on a part with two; the toolbar shows it ([M4 part 5 shots](../gallery/README.md)) |

| Check | Where | What it shows |
|---|---|---|
| Core 1 | `test_rp2040` (825 checks) | core 1 waiting at reset with its 0 in core 0's FIFO; the launch sequence echoed word by word; core 1's CPUID 1; its WFE ended by core 0's SEV; the PSM powering it off and on into its bootrom |
| The Pico | `examples/pico-dual-core` 9/9 | firmware built by clang and by EmbCC: core 1 launched, 2000 from both cores adding to one counter under spinlock 31, three answers from core 1's divider while core 0 prints with its own, the LED's 1 Hz from core 1's timer alarm, the button through IO_BANK0's PROC1 interrupt, the terminal echoed by core 0 |
| Builds | `firmware_build` | the dual-core example built by clang and by EmbCC, its scenario passing |

Known gaps: the debugger cannot pick a core (it sees the one that runs
next, so a breakpoint stops either); a device another core accessed in the
same instruction's time may see the two cores' times differ by an
instruction; the SIO's FIFO interrupt is raised
when VLD, WOF or ROE sets, not held while they stay set; the
interpolators; Pico SDK images still need the bootrom's function table.
