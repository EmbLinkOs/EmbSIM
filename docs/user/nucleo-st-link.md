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

## VS Code (Cortex-Debug)

Cortex-Debug starts OpenOCD itself. The configuration names OpenOCD's
files and adds the backend command:

```json
{
  "name": "NUCLEO-F446RE (EmbSIM)",
  "type": "cortex-debug",
  "request": "launch",
  "servertype": "openocd",
  "executable": "${workspaceFolder}/firmware/blink-serial.elf",
  "configFiles": ["interface/stlink.cfg", "target/stm32f4x.cfg"],
  "openOCDLaunchCommands": ["hla_stlink_backend tcp 7184"],
  "runToEntryPoint": "main"
}
```

The commands are the ones above. This configuration has not itself been
run in VS Code here.

## What is not there yet

- Tools that only open a USB device do not see the probe: `st-flash` and
  `st-util`, and STM32CubeProgrammer without stlink-server. That needs the
  USB/IP step of ADR-031.
- The probe's mass-storage drive (`NODE_F446RE`), which flashes a `.bin`
  copied onto it.
- SWO trace.
- The other boards' probes: the Pico's debug probe, the nRF52840 DK's
  J-Link OB, the Explorer 16/32's PICkit.

See `docs/engineering/debug-probes.md` for the plan.
