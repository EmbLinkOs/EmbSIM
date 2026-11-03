# Raspberry Pi Pico with a Raspberry Pi Debug Probe

The bundled Raspberry Pi Pico running
[pico-blink-serial's firmware](../pico-blink-serial/README.md), with a
Raspberry Pi Debug Probe wired to it the way Raspberry Pi's guide wires one:

| Debug Probe | Pico |
|---|---|
| D port SC (orange) | the SWD pads' SWCLK |
| D port SD (yellow) | the SWD pads' SWDIO |
| D port GND (black) | the SWD pads' GND |
| U port TX (orange) | GP1 (UART0 RX) |
| U port RX (yellow) | GP0 (UART0 TX) |
| U port GND (black) | GND |

The probe is a CMSIS-DAP v2 device (2e8a:000c) with a serial port. Served
over USB/IP, it is on the computer's own USB stack, and the tools that drive
a real Debug Probe drive it:

```sh
embsim serve examples/pico-debug-probe/project.json --usbip --run &
sudo modprobe vhci-hcd
sudo usbip attach -r 127.0.0.1 -b 1-2
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 1000"
```

OpenOCD wakes the RP2040's serial wire from dormant, selects core 0's and
core 1's debug ports with TARGETSEL, and serves GDB for each core on ports
3333 and 3334. The probe's serial port (`/dev/ttyACM0` on Linux) carries the
Pico's UART at the rate the terminal sets; what it sends comes back in upper
case. See [the Debug Probe guide](../../docs/user/pico-debug-probe.md).
