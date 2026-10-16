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

M1 runs the STM32F103C8 (the Blue Pill board): see the MCU description's
fidelity for what is modelled. `examples/bluepill-button-led` is a complete
project with its firmware.

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
host's speed. Instruction timing is estimated (ADR-015); peripheral timing
(baud rates, timer periods) follows the clock tree exactly.

## Building

```sh
embsim toolchains                      # which profiles are installed
embsim build examples/bluepill-button-led/project.json
embsim build PROJECT --toolchain embcc  # another profile
```

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
