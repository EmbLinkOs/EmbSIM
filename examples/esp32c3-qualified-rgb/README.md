# Firmware-driven named RGB indicator

An ESP32-C3-DevKitM-1 boots a bare-register flash image through its own ROM.
LEDC drives IO4/IO5/IO6 at 1 kHz through separate 100 Ω limiting resistors into
the named Kingbright WP154A4SUREQBFZGC. The manufacturer's terminals are
1=R, 2=COM cathode, 3=B, 4=G at 1.27 mm pitch. Logical channel routing does
not change that physical order.

The **MODE** push button on IO0 cycles mixed 25/50/75% R/G/B duty, all off,
all on, then mixed again. Firmware reads the physical input while running;
the scenario does not write LED outputs or inject a precomputed waveform.
Bounce is disabled for this acceptance circuit; the example is not a debounce
algorithm or a bootstrapping-pin test.

```sh
embsim run examples/esp32c3-qualified-rgb/project.json \
  --scenario examples/esp32c3-qualified-rgb/scenarios/pwm.json --json
```

The 39 assertions check duty, frequency, mean/RMS channel currents, finite GPIO
loading and physical-button transitions after a 250 ms boot allowance. Digital
probes sample at 1 MHz; current and voltage probes at 200 kHz. The scenario's
time-weighted measurements use recorded sample-and-hold intervals, not GUI frames.
In **Run**, inspect current traces on the scope and PWM duty in the logic analyzer.
The probes are ideal inspection probes, without physical instrument loading.

The GPIO model now applies the MCU description's scalar 20 Ω resistance instead
of the generic family's 25 Ω default. This remains a representative linear driver,
not qualification of drive-strength settings, temperature or guaranteed MCU corners.
At full-on the native currents are approximately R=11.983, G=4.227, B=4.316 mA;
mixed means are R=2.996, G=2.114, B=3.237 mA. Bounds derive from the independently
reviewed source curves, not these observed values.

The LED's checked static envelope remains 25 °C and 2–20 mA on-state current.
PWM checks establish timing and sampled electrical averages of that static model.
Junction charge, turn-on/off transients, thermal coupling and photometric averaging
are not qualified. The 3D emitter still displays instantaneous observations;
a screenshot of its light is not proof of perceived PWM brightness.

## Rebuild

Install Clang/lld with RISC-V support and Python esptool 4.11.0, then run
`sh firmware/build.sh` from this directory. Alternatively set `EMBSIM_ZIG` to a
Zig 0.14.1 executable. The script produces `pwm.elf` and the ROM-loadable `pwm.bin`.
It does not require ESP-IDF or change EmbSIM's boot/reset behavior. The startup
uses no initialized C globals; the linker separates IRAM and aliased DRAM.
The flash image has no function symbols, so coverage/profile require a separate
ELF debugging workflow, which this example does not supply through the session API.

[Measurement contract and evidence](../../docs/engineering/indicator-pwm.md).
