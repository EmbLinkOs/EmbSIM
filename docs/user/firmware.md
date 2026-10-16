# Firmware: run, build and debug

A board in a project runs firmware when its entry has a `firmware` block
(SPEC-007):

```json
"firmware": {
  "image": "firmware/button-led.elf",
  "format": "elf",
  "toolchain": "clang",
  "sources": ["firmware/startup.c", "firmware/main.c"],
  "flags": ["-ffreestanding", "-fno-builtin"],
  "linker_script": "firmware/stm32f103c8.ld"
}
```

`image` is an ELF or Intel HEX file, relative to the project. Only the image
is needed to run; the other fields are for `embsim build`. Without an image
file the board loads with its core held in reset and a `mcu.no_image`
warning; without a `firmware` block its MCU's pins stay high-impedance
inputs, as a blank part's do.

Three MCUs run firmware; each description's fidelity says what is
modelled:

| Board | MCU | Core | Example |
|---|---|---|---|
| `emblink/bluepill` | STM32F103C8 | Arm Cortex-M3 (estimated timing) | `examples/bluepill-button-led` |
| `emblink/arduino_uno` | ATmega328P | AVR (cycle-exact) | `examples/uno-blink-serial` |
| `emblink/hifive1_revb` | FE310-G002 | RISC-V RV32IMAC (estimated timing) | `examples/hifive1-blink` |

A project may hold several boards, each with its own firmware, running
together on the session's time: `examples/two-boards-uart` connects an
Uno's serial line to a Blue Pill's.

## Running

`embsim run`, `embsim serve` and the workbench run the firmware with the
circuit. The MCU's pins are pins of the circuit: a GPIO output drives its
net through the datasheet's output model, an input reads the resolved net,
a USART's TX pin carries its frames. Inputs and observations:

| Target | What it is |
|---|---|
| `serial:B1` | the board's console: semihosting output and the console USART's bytes |
| `serial:B1.USART2` | one USART's transmitted bytes |
| input `serial` (or `tx_text`) to `B1` or `B1.USART2` | text the firmware receives, at the USART's baud rate |
| input `reset` to `B1` | a reset of the board's MCU |

The firmware's time is the session's virtual time: a 72 MHz core runs 72
million instructions' worth of cycles per simulated second, whatever the
host's speed. The AVR's instruction timing is the datasheet's, cycle for
cycle; the Cortex-M's and RISC-V's are estimated (ADR-015). Peripheral
timing (baud rates, timer periods, a RISC-V CLINT's 32.768 kHz) follows the
clock tree exactly.

## Building

```sh
embsim toolchains                      # which profiles are installed
embsim build examples/bluepill-button-led/project.json
embsim build PROJECT --toolchain embcc  # another profile
```

The Uno, HiFive1, Pico and nRF52840 DK examples build with Clang (its
`avr`, `riscv32-unknown-elf`, `thumbv6m-none-eabi` and
`thumbv7em-none-eabihf` targets, with lld) and with EmbCC.

A Pico image starts with boot2, the 256 bytes the RP2040's bootrom checks
by their CRC-32, copies to SRAM and enters; without a good one the part
waits in its USB boot and the session says so (`mcu.no_boot2`).
`examples/pico-blink-serial/firmware/boot2.c` is a minimal boot2 that hands
over to the vector table at 0x10000100. The bootrom's functions are
there, done by EmbSIM (ADR-021): an image built with the Pico SDK runs
unmodified, its float and double arithmetic, memcpy and flash programming
included (`tests/vendor/pico-sdk`).

Arduino sketches run on `emblink/arduino_uno` as built: compile the core
and the sketch with the IDE (Sketch > Export Compiled Binary keeps the
ELF in the build folder) or with avr-gcc and the flags of the core's
`platform.txt`, then `embsim run --target emblink/arduino_uno --image
sketch.elf --console`. `tests/vendor/arduino` builds two sketches against
the unmodified ArduinoCore-avr and checks `delay`, `millis`,
`analogRead`, `analogWrite`, `attachInterrupt` and `Serial` against
scenarios. `tests/vendor/stm32cube` does the same for ST's STM32F1 HAL on
the Blue Pill: the HAL's own startup file, system file and linker script,
unchanged.

On the nRF52840, EasyDMA reaches RAM only: a UARTE buffer in flash (a
string literal) is a warning (`mcu.easydma_range`) and nothing is sent,
as on the part.

Profiles live in `catalog/toolchains/` (EmbCC, Clang, GNU Arm and others):
each names its compiler, how to detect its version and the compile and link
commands. `embsim build` prints the commands it ran and the compiler's
output; the workbench's build command uses the same profiles
(`toolchain.build`).

## Debugging in the workbench

The Debug perspective shows the selected board's core when it stops: the
source line (when the image has DWARF line information), the disassembly,
the registers with the ones that changed highlighted, breakpoints, memory
and the peripherals' registers. Breakpoints are set by clicking a source
line or an instruction, or by address or symbol (`EXTI0_IRQHandler`). A
breakpoint stops the whole session at that instruction; continuing gives
the same run as if it had never stopped.

## Debugging with GDB or LLDB

```sh
embsim gdb examples/bluepill-button-led/project.json --port 3333
arm-none-eabi-gdb examples/bluepill-button-led/firmware/button-led.elf \
  -ex 'target extended-remote :3333'
lldb examples/bluepill-button-led/firmware/button-led.elf \
  -o 'gdb-remote 3333'
```

The server runs the project's circuit while the debugger continues: the
board's pins, its serial lines and every other component keep working.
Breakpoints, watchpoints, single steps, register and memory reads and
writes, and `monitor reset` are supported. One debugger connects at a time.
