# M1 firmware: what was built and how it was checked

This page records the M1 implementation: a board's MCU runs its firmware
inside the circuit. It is the source for the M1 rows of
[STATUS.md](../STATUS.md); update both together.

## What exists

| Area | Files | Contract |
|---|---|---|
| Machine | `core/mcu/mach.{h,c}`, `bus.c`, `image.c`, `semihost.c` | SPEC-009 §2 to §4: EmbCC's simulator made per instance; regions, aliases, bit-band, devices, watchpoints, the debugger's accesses; ELF and Intel HEX from a buffer; semihosting |
| Cortex-M core | `core/mcu/cortexm/` | SPEC-009 §3: Thumb-2, NVIC, SCB, SysTick, DWT, FPU; the cost table (ADR-015); a Thumb disassembler |
| Register files | `core/mcu/rf.{h,c}`, `build.{h,c}`, `models.c`, `schemas/registers.schema.json`, `tools/importers/svd2regs.py` | SPEC-009 §5, ADR-020: EmbSIM JSON descriptions, the access table, clock gates, bindings to models |
| STM32F1 models | `core/mcu/stm32/` | SPEC-009 §6: RCC, GPIO, AFIO, EXTI, USART, TIM time base, FLASH and PWR registers |
| Runtime | `core/mcu/mcu.{h,c}`, `core/project/loader.c` | SPEC-009 §7 to §9, ADR-019: quanta on the scheduler, pins as signal events, serial on pins and streams, NRST and BOOT0, firmware loading |
| Debug | `core/session/debug.c`, `gdb.c`, `core/mcu/dwarf.{h,c}` | SPEC-009 §10: breakpoints that stop the session, `debug.*` methods, the GDB server, DWARF line tables |
| Toolchains | `core/project/toolchain.c`, `core/host/host_*.c` (`esk_host_run`), `catalog/toolchains/` | SPEC-010 subset: detection, builds from the profiles |
| CLI | `cli/commands.c` | `gdb`, `build`, `toolchains` |
| Workbench | `ui/apps/workbench/src/perspectives/debug/`, `stores/debug.ts`, `ui/packages/protocol` | the Debug perspective |
| Catalog | `catalog/mcus/st/stm32f103c8/registers.json`, `mcu.json`, `catalog/boards/bluepill/` | the STM32F103C8's register description from RM0008; bindings; the board's linker script |

## How it was checked

| Check | Where | What it shows |
|---|---|---|
| EmbCC's Blue Pill golden fixture | `tests/firmware/bluepill-golden`, `test_mcu` | the extracted core and models give EmbCC's output, with five USART timing deltas documented in `deltas.json` |
| SVD cross-check | run by hand with ST's SVD converted by `svd2regs.py` | the converted description gives the same fixture output as the RM0008 one |
| Breakpoint neutrality (MCU-006) | `test_mcu`: `test_breakpoint_continue_equivalence` | a run stopped at breakpoints and continued equals an uninterrupted one, instruction for instruction |
| The acceptance circuit | `examples/bluepill-button-led`, `tools/ci/run_examples.py` | 16 assertions: the regulated rail, EXTI on the button, PC13, the LED current, the transistor saturated, USART text both ways |
| GDB server | `tests/protocol/test_gdb.py`; LLDB by hand | registers, memory, breakpoints, step and continue over the remote protocol |
| Builds | `tests/protocol/test_build.py`; EmbCC and Clang by hand | the profiles' commands build the example's firmware |
| Workbench | Vitest; Chromium by hand | the debug store; a breakpoint on `EXTI0_IRQHandler` hit from a button press in the browser, stepped, shown at its source line |

## Known gaps

Analog inputs reach the core at quantum boundaries (ADR-019); the ADC, SPI,
I2C, DMA and timer output models; DWARF variables; `debug.gdb` over the
session protocol; the gcc-arm and other profiles beyond EmbCC and Clang have
not been exercised; Windows process launching is exercised only by CI.
