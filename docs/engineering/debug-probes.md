# Debug probes: the ST-Link on a NUCLEO, the core's own debug, the IDEs

Owner's direction (2026-10-18, 01 §10 item 1, ADR-031 "Programming"): a
board is flashed and debugged the way the real one is, through "the
on-board ST-Link or PICkit", from "the tools that drive them"; ADR-032 asks
for IDE integration. This plan says how, in the order the work lands.

## 1. How it works on a real bench

A NUCLEO-F446RE carries an ST-LINK/V2-1 (an STM32F103CBT6 running ST's
firmware, MB1136's U2). Plugged into a computer it is one USB device,
0483:374B, with three functions:

- the **debug interface**: two bulk endpoints carrying ST-Link's command
  protocol (a 16-byte command, then data in or out). ST publishes no
  specification; the protocol is documented by the open tools that speak
  it, OpenOCD's `src/jtag/drivers/stlink_usb.c` first;
- the **virtual COM port** (CDC ACM), wired to the target's USART2 (PA2,
  PA3), already modelled as the board's console bridge;
- a **mass-storage drive** (`NODE_F446RE`, with `MBED.HTM` and
  `DETAILS.TXT`): a `.bin` copied onto it is programmed into the target's
  flash.

Between the probe and the target are SWDIO (PA13), SWCLK (PA14), NRST and
the target's voltage sense. Through SWD the probe reaches the target's
**debug port** (ADIv5 SW-DP, IDCODE 0x2BA01477 on a Cortex-M4) and its
**MEM-AP**, which reads and writes the target's memory map as a bus master.
Everything a debugger does to a Cortex-M is a memory access through it:
halting and stepping by DHCSR, the core's registers by DCRSR and DCRDR,
reset vector catch by DEMCR, breakpoints by the FPB, watchpoints by the
DWT's comparators, a reset by AIRCR.SYSRESETREQ or NRST.

A halted core stops; the clocks, timers and peripherals go on (unless
DBGMCU's freeze bits stop them). This is the main difference from
EmbSIM's existing debugger, which pauses the whole simulation.

The tools: OpenOCD (`interface/stlink.cfg`, then GDB on port 3333),
STM32CubeIDE and STM32CubeProgrammer (through ST's stlink-server), the
`stlink` tools (`st-flash`, `st-util`), pyOCD. VS Code's Cortex-Debug and
PlatformIO drive OpenOCD or pyOCD.

## 2. What is observable from outside

- The USB device's identity, version (`V2J37M26`-style: the JTAG/SWD
  firmware 37, mass storage and VCP 26), the target voltage it measures.
- The command protocol's answers, byte for byte as OpenOCD expects them,
  including its status codes (0x80 OK, 0x81 fault, SWD wait and fault).
- The target as a debugger sees it: DP IDCODE, the ROM table, DBGMCU_IDCODE
  (0x10006421 on the F446), the flash size word, the core's Debug state.
- Timing that tools wait on: a halt takes effect within a few cycles, a
  flash program takes the flash's time, NRST's pulse.

## 3. How the host reaches the simulated probe

A real probe is a USB device; EmbSIM's has to reach unmodified tools
without a kernel driver. The first route is ST's own: **stlink-server**,
the TCP service STM32CubeIDE uses to share a probe, whose protocol
OpenOCD 0.12 also speaks (`st-link backend tcp`, port 7184). The session
serves that protocol for each ST-Link on the bench, so

    openocd -f interface/stlink.cfg -c "st-link backend tcp" -f target/stm32f4x.cfg

finds the NUCLEO's probe, halts the core, programs the flash with its own
loader running on the target, and serves GDB. USB/IP (ADR-031's last
step) later adds the tools that only open USB (`st-flash`, CubeProgrammer
without stlink-server).

The mass-storage drive is reached as the Pico's RPI-RP2 drive is: through
a USB host part's copy job.

## 4. Steps

1. **The core's debug (ARMv7-M ARM C1, ARMv6-M ARM C1).** In
   `core/mcu/cortexm`: DHCSR (C_DEBUGEN, C_HALT, C_STEP, C_MASKINTS,
   S_REGRDY, S_HALT, S_SLEEP, S_LOCKUP, S_RETIRE_ST, S_RESET_ST and their
   sticky clears), DCRSR and DCRDR for every core register (and the FP
   registers with an FPU), DEMCR's vector catches and TRCENA, DFSR, a BKPT
   halting the core when C_DEBUGEN is set, the FPB (FP_CTRL, FP_REMAP,
   FP_COMPn: v1 on the M3 and M4, the BPU on the M0+), the DWT's
   comparators as watchpoints, AIRCR's SYSRESETREQ and VECTRESET, DBGMCU's
   freeze bits on the STM32s. A halted core executes nothing while time
   goes on. The session's own debugger stays as it is (it pauses the
   simulation); a probe uses the architecture's.
2. **The debug port.** An ADIv5 SW-DP and MEM-AP as transactions (DP
   IDCODE, CTRL/STAT with its power-up handshake, SELECT, RDBUFF; the AP's
   CSW, TAR, DRW, BD0-3, IDR, BASE, auto-increment within 1 KiB; the ROM
   table), the debug lines' state taken from the pins (SWDIO and SWCLK
   given to the debug port, NRST). Inside: transactions, not SWCLK edges
   (as USB is packets); the outside, what a tool reads, is the same.
3. **The ST-LINK/V2-1.** A part (`emblink/st_link_v21`) on the NUCLEO:
   the command set OpenOCD's driver sends to a V2-1 (version, modes,
   SWD entry, frequencies, IDCODEs, memory 8/16/32-bit, core and debug
   registers, NRST, the last-transfer status, DAP register access), the
   target voltage from its sense pin, and the stlink-server TCP service.
   Proven by OpenOCD 0.12, unmodified, connecting, halting, reading
   registers, programming a CubeF4 example into flash and running it, and
   by GDB through OpenOCD setting a breakpoint and stepping.
4. **The drive.** `NODE_F446RE` on the probe's USB: a `.bin` copied onto
   it programmed into flash and the target reset.
5. **The IDEs.** `embsim ide` writes a project's launch configurations:
   VS Code with Cortex-Debug (OpenOCD on the probe, and EmbSIM's own GDB
   server for boards without a probe), PlatformIO's `debug_tool`. Proven
   by running the same commands the configurations run.
6. **The other boards.** The Blue Pill's external ST-Link V2 clone (the
   same protocol, 0483:3748), the Pico's SWD through a Raspberry Pi Debug
   Probe (CMSIS-DAP: published by Arm), the nRF52840 DK's J-Link OB
   (SEGGER's protocol: unpublished; its CMSIS-DAP alternative firmware).
7. **The PICkit.** The Explorer 16/32's PKOB4 speaks a protocol Microchip
   does not publish, to MPLAB X and IPE only; no open tool drives it. Its
   ICSP programming of the PIC24 and PIC32 (the programming executive,
   published in their flash programming specifications) can be modelled
   when a host route exists; until then it is a declared omission.

## 5. Where it stands (2026-11-02)

Steps 1, 2, 3 and 5 are in. A blank NUCLEO is programmed by unmodified OpenOCD
through the simulated ST-LINK and stlink-server, and GDB debugs it
through OpenOCD (`tests/protocol/test_stlink.py`;
`docs/user/nucleo-st-link.md` holds the commands and a VS Code
Cortex-Debug configuration). Inside, step 2's debug port is the MEM-AP
access the MCU runtime gives a probe (`esk_mcu_dap_mem`) and the DP and
AP registers the ST-LINK answers itself, not a separate SW-DP model. The
server answers one command per client per run slice, as the probe's
full-speed USB takes a 1 ms frame for each. This also lets a core that a
command restarted run before the next command looks at it: OpenOCD's
step writes DHCSR three times in a row.
Step 5 is in: `embsim ide` writes VS Code's
configurations, Cortex-Debug through the probe with OpenOCD and on EmbSIM's
GDB server, and `tests/protocol/test_ide.py` carries them out as VS Code
does. PlatformIO's `debug_tool` is not done yet. SWO trace is in: the
ARMv7-M cores' ITM and TPIU (`cortexm/itm.c`), PB3's TRACESWO under
DBGMCU's TRACE_IOEN, and the probe's trace receiver, which OpenOCD's
`tpiu` and `itm` commands read (`test_stlink.py`). Step 4 is in: the
probe's own USB device on the NUCLEO's CN1 (0483:374B, composite: the
debug interface, the NODE_F446RE drive, the virtual COM port's
interfaces), a .bin copied onto the drive programmed through the target's
flash interface over SWD at the flash's times, then the part reset and the
drive re-enumerated (`test_stlink_drive.py`). The virtual COM port's data
is not on USB yet (the console bridge stands for it). Steps 6 and 7
remain, and USB/IP for the tools that open the probe's USB device.

## 6. Not planned

ST's firmware itself (proprietary); the ST-Link's firmware upgrade (DFU);
JTAG (the NUCLEO's ST-Link is SWD only).
