# Debugging a NUCLEO through its ST-LINK

A NUCLEO-F446RE carries an ST-LINK/V2-1 (the board's `U2`, part
`emblink/st_link_v21`). On the bench it is the probe that programs and
debugs the STM32: its SWD lines go to PA13 and PA14, it can pull NRST low,
and it senses the target's VDD. In EmbSIM the computer reaches it the way
STM32CubeIDE reaches a probe it shares: through **stlink-server**, a TCP
service the session runs. OpenOCD 0.12 speaks that protocol, so OpenOCD,
GDB and the IDEs that drive them work unmodified.

## Start the session with the probe shared

```sh
embsim serve examples/nucleo-f446re-blink-serial/project.json --stlink --run
# embsim: stlink-server on 127.0.0.1:7184 (OpenOCD: -f interface/stlink.cfg -c "hla_stlink_backend tcp 7184")
```

`--stlink` takes an optional port (default 7184, stlink-server's own).
`--run` runs the session in real time from the start: the probe answers
while the session runs, as a real one answers while its board is powered.
From a client of the session protocol, `stlink.serve` does the same
(SPEC-008).

## Program the board with OpenOCD

```sh
openocd -f interface/stlink.cfg -c "hla_stlink_backend tcp 7184" \
        -f target/stm32f4x.cfg \
        -c "program firmware/blink-serial.elf verify reset exit"
```

OpenOCD reports the probe as `STLINK V2J37M26 (API v2) VID:PID 0483:374B`,
the target voltage (3.3 V), a Cortex-M4 r0p1 with 6 breakpoints and 4
watchpoints, then the STM32F446's device id 0x10006421 and its 512 KiB of
flash. It halts the core, runs its flash loader in the target's SRAM,
verifies what it wrote and resets the part. A board whose image is not
built yet starts blank. Its core locks up on the erased vectors, as a new
part does, and OpenOCD programs it the same way.

## Debug with GDB

Start OpenOCD as a GDB server (it listens on port 3333):

```sh
openocd -f interface/stlink.cfg -c "hla_stlink_backend tcp 7184" -f target/stm32f4x.cfg
```

then, in another terminal:

```sh
gdb-multiarch firmware/blink-serial.elf    # or arm-none-eabi-gdb
(gdb) target extended-remote :3333
(gdb) monitor reset halt
(gdb) break main
(gdb) continue
(gdb) next
(gdb) watch ticks
(gdb) continue
```

The breakpoint is one of the core's FPB comparators, `next` steps the
core, and the watchpoint is a DWT comparator: the core stops after the
SysTick handler's write to `ticks`. While the core is halted, the clocks
and peripherals go on, as on the board. A timer keeps counting, and a UART
keeps sending what is in its shift register.

## printf over SWO

The ST-LINK also reads the STM32's serial wire output: on the NUCLEO its SWO
input is wired to PB3 (TRACESWO). A program writes text to the ITM's
stimulus port 0, as CMSIS's `ITM_SendChar` does; OpenOCD sets the trace up
and writes what arrives to a file:

```sh
openocd -f interface/stlink.cfg -c "hla_stlink_backend tcp 7184" -f target/stm32f4x.cfg \
        -c init \
        -c "stm32f4x.tpiu configure -protocol uart -output swo.bin -traceclk 16000000 -pin-freq 2000000" \
        -c "stm32f4x.tpiu enable" -c "itm port 0 on"
```

`-traceclk` is the core's clock (HCLK: 16 MHz on the HSI, 180 MHz on the
example's PLL); `-pin-freq` is the SWO rate, at most 2 MHz on this probe.
OpenOCD sets DBGMCU's TRACE_IOEN, the TPIU's NRZ protocol and prescaler, and
the ITM's enables; the file holds the ITM's packets (a header byte, then
the bytes). A TPIU prescaler that does not give the probe's rate within
3 % reaches it as garbage, as on the board: the probe's output
`trace_garbled` counts it. `tests/firmware/nucleo-swo` is such a program.

## VS Code

`embsim ide` writes the launch configurations and the tasks they need into
`.vscode` next to the project:

```sh
embsim ide project.json            # --embsim PATH when embsim is not on the PATH
```

- **"B1 through its ST-LINK/V2-1 (OpenOCD)"** uses Cortex-Debug. Its task
  starts the session in real time with the probe shared
  (`embsim serve project.json --stlink 7184 --run`). Cortex-Debug then
  starts OpenOCD with `interface/stlink.cfg`, `target/stm32f4x.cfg` and
  `hla_stlink_backend tcp 7184`, and runs to `main`.
- **"B1 on EmbSIM's GDB server"** connects to EmbSIM's own GDB server
  (`embsim gdb project.json --board B1 --port 3333`). This works for any
  board, probe or not: Cortex-Debug for a Cortex-M, the C/C++ extension's
  GDB (`gdb-multiarch`) for the other cores.

The files come from the board's description (its `debug` section, SPEC-006).
`embsim ide` will not replace existing ones unless given `--force`.
`tests/protocol/test_ide.py` carries out both configurations the way VS Code
does, from what the files say: the tasks until they are ready, then OpenOCD
and GDB stopping at `main`. VS Code itself is not run.

## What is not there yet

- Tools that only open a USB device do not see the probe: `st-flash` and
  `st-util`, and STM32CubeProgrammer without stlink-server. That needs the
  USB/IP step of ADR-031.
- The probe's mass-storage drive (`NODE_F446RE`), which flashes a `.bin`
  copied onto it.
- The other boards' probes: the Pico's debug probe, the nRF52840 DK's
  J-Link OB, the Explorer 16/32's PICkit.

See `docs/engineering/debug-probes.md` for the plan.
