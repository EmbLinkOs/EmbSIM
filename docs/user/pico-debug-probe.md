# Debugging a Pico with a Raspberry Pi Debug Probe

The Raspberry Pi Debug Probe (part `emblink/rpi_debug_probe`) is an RP2040
running Raspberry Pi's debugprobe firmware: a CMSIS-DAP v2 probe and a
serial port on one USB cable. In EmbSIM it is wired as on a real bench, and
the computer opens its USB device itself, so OpenOCD, pyOCD, probe-rs and
the IDEs built on them drive it unmodified.

## Wire it

[`examples/pico-debug-probe`](../../examples/pico-debug-probe/README.md)
has the wiring of Raspberry Pi's guide: the D port (SC, SD, GND) on the
Pico's SWD pads (`B1.SWCLK`, `B1.SWDIO`, `B1.SWD_GND`), the U port's TX on
GP1 and RX on GP0. Its target is the MCU on SD's net. Its USB pins
(`USB_DP`, `USB_DM`) need a net, and no host part on it: the session then
serves the device to the computer.

## Put it on the computer

```sh
embsim serve examples/pico-debug-probe/project.json --usbip --run
# embsim: USB/IP on 127.0.0.1:3240 (usbip list -r 127.0.0.1; usbip attach -r 127.0.0.1 -b 1-N)
sudo modprobe vhci-hcd
usbip list -r 127.0.0.1          # 1-2: Raspberry Pi : ... (2e8a:000c)
sudo usbip attach -r 127.0.0.1 -b 1-2
```

`lsusb` then lists `2e8a:000c Raspberry Pi Debug Probe (CMSIS-DAP)`, with its
CDC serial port as `/dev/ttyACM0`. USB/IP is Linux's; on Windows, usbip-win
attaches the same server.

## Debug with OpenOCD

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000"
```

OpenOCD reports `CMSIS-DAP: SWD supported`, firmware 2.0.0, then wakes the
RP2040's serial wire from dormant and selects each core's debug port with
TARGETSEL (`SWD DPIDR 0x0bc12477`, instances 0 and 1): two Cortex-M0+ cores,
each with 4 breakpoints and 2 watchpoints, GDB on ports 3333 (core 0) and
3334 (core 1). Each core halts, steps and runs through its own debug port
while the other goes on. `SYSCFG.PROC_CONFIG`'s instance IDs, if the
firmware moves them, move the ports as on the part.

The Rescue DP is on the same wire (TARGETSEL 0xf1002927): setting its
CTRL/STAT's CDBGPWRUPREQ holds the chip in reset, clearing it lets the chip
start again with CHIP_RESET reading HAD_PSM_RESTART. The bootrom then halts
in its dead loop instead of booting flash, so a debugger can attach to a
part whose program stops it from answering.

## The serial port

The probe's U port is a UART at the rate, data bits, parity and stop bits
the terminal sets on `/dev/ttyACM0`. As on the real probe, it carries data
while the port is open (DTR); a closed port leaves the probe's 32-byte
receive FIFO to fill and drop what follows. A break holds TX low.

## What is not there

The probe's SWCLK edges on the net (the wire is clocked inside the target's
model, bit by bit); the firmware's own time between packets; autobaud; the
reset interface's reboots into BOOTSEL; the LEDs as pins. The component's
fidelity lists them.
