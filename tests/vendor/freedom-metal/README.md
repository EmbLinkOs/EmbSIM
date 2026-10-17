# SiFive's freedom-e-sdk examples on unmodified freedom-metal

Evidence for 01 §8.2 (a board runs its vendor's examples): SiFive's own
example programs from [freedom-e-sdk](https://github.com/sifive/freedom-e-sdk),
built with the unmodified [freedom-metal](https://github.com/sifive/freedom-metal)
library and the HiFive1 Rev B's BSP, run on `emblink/hifive1_revb` and pass
their scenarios. The examples' sources are the SDK's; this directory holds
only their scenarios.

| Example | What it checks |
| --- | --- |
| `hello` | `printf` through freedom-metal's tty on UART0 |
| `sifive-welcome` | the banner; `metal_led_on` for red, green and blue a second each, timed by the CLINT's `mtimecmp` interrupt (freedom-metal drives an LED's pin high to turn it on and the board's LEDs light with their pin low, so the named colour goes dark, on the board as here) |
| `csr` | machine-mode CSRs read, written, read back |
| `uart-interrupt` | UART0's watermark interrupt through the PLIC, debounced by a CLINT timer, toggling the red LED |
| `example-user-mode` | a drop to user mode, an illegal instruction there trapped back to machine mode |
| `minimal-boot` | freedom-metal's boot without the C library's start-up |
| `example-rtc` | the always-on domain's RTC on the 32.768 kHz clock (`metal_rtc_set_compare` a second away, `metal_rtc_run`), its interrupt through the PLIC a second later and not before; exit 0 |
| `example-watchdog` | the always-on watchdog, each write unlocked by its key: `metal_watchdog_set_rate` (32.768 kHz, the nearest to the 1 MHz asked for), a one-second timeout with `wdogzerocmp`, its interrupt through the PLIC a second after it starts, cleared in the handler; exit 0 |
| `example-pwm` | `metal_pwm` on PWM1: 1 Hz from the bus clock (the driver's prescaler and pwmcmp0, zerocmp, deglitch, enalways), channels 1 to 3 at 90, 90 and 20 % on the RGB LED's GPIO 19, 21 and 22 through IOF1 |
| `example-spi` | `metal_spi_transfer` on SPI1 at 100 kHz, mode 0: three frames with CS0 held by csmode HOLD, each waited for on rxdata; exit 0 |
| `example-pmp` | PMP entry 0, locked, NAPOT over a 128-byte array, read-only: machine mode's store to it is a store access fault that the handler steps over, and `main` returns the word it could not change (exit 0, where the store would have made it 6) |
| `timer-interrupt` | `mtimecmp` at 0: the CLINT's timer interrupt taken once on enabling it, its handler moving `mtimecmp` on; exit 0 only when it ran exactly once |
| `software-interrupt` | `msip` through the CLINT: the machine software interrupt taken and cleared; exit 0 only when its handler ran |
| `return-pass` | `main`'s 0 through freedom-metal's exit |

Not run: `plic-interrupts` (the Arty's buttons, which the HiFive1 lacks),
`example-privilege-level` (supervisor mode, which the FE310 lacks),
`example-buserror` (it does not build for the board: no sifive,error0
device), and `example-pwm`, `example-spi` and `example-i2c`, which wait on
peripherals EmbSIM does not model yet (STATUS.md).

Two of the BSP's details meet the models. Its lfrosc clock gives
freedom-metal LFROSCCFG and LFCLKMUX as offsets in the always-on domain
(0x70, 0x7C), which the driver reads as addresses: they fall in the Debug
Module's space at 0, which reads as zeros here (`mcu.json`'s `debug`
region), so the low-frequency clock's rate is the 32.768 kHz the BSP
names. And `metal_rtc_set_compare` writes `rtccfg` before `rtccmp0`, whose
reset value the manual leaves undefined: it is all ones here, so the RTC's
interrupt is not pending from reset.

```sh
git clone --depth 1 https://github.com/sifive/freedom-e-sdk /tmp/freedom-e-sdk   # bsp/ and software/ suffice
git clone https://github.com/sifive/freedom-metal /tmp/freedom-metal
git -C /tmp/freedom-metal checkout 1cec4a23a7ed7350db79a392be65acd51acd5412     # the SDK's pinned commit
python3 tests/vendor/freedom-metal/run.py --sdk /tmp/freedom-e-sdk --metal /tmp/freedom-metal
```

`run.py` follows the SDK's `scripts/standalone.mk` for the board (its
`settings.mk`, the release configuration): the BSP's headers as
`metal/machine.h` and its siblings, freedom-metal's `src` and `gloss` as
`libmetal.a` and `libmetal-gloss.a`, each example linked with
`metal.default.lds`. Three things differ, two for the toolchain (Ubuntu
24.04's riscv64-unknown-elf-gcc 13.2 and picolibc): GCC 12 and later name
the CSR and fence.i extensions (`-march=rv32imac_zicsr_zifencei`), and
`picolibc_stdio.c` gives picolibc's stdio freedom-metal's tty, as
freedom-metal's gloss gives newlib's (SiFive's toolchain carries newlib).
The third is for the scenarios: on this board freedom-metal's
`metal_shutdown` spins and an example's exit status goes nowhere, so
`exit_report.c`, linked with `-Wl,--wrap=metal_shutdown`, prints it on the
tty first (`exit 0`).

CI's linux-gcc job runs it against freedom-e-sdk 4518a22 and freedom-metal
1cec4a2.
