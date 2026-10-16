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

Not run: `plic-interrupts` (the Arty's buttons, which the HiFive1 lacks),
`example-privilege-level` (supervisor mode, which the FE310 lacks),
`timer-interrupt` and `software-interrupt` (they report only through
`main`'s return value, which freedom-metal's shutdown on this board
discards), `example-buserror` (no bus-error device); `example-rtc`,
`example-watchdog` and `example-pmp` wait on the always-on domain's RTC and
watchdog and on the PMP, and `example-pwm`, `example-spi` and `example-i2c`
on peripherals, that EmbSIM does not model yet (STATUS.md).

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
`metal.default.lds`. Two things differ, both for the toolchain (Ubuntu
24.04's riscv64-unknown-elf-gcc 13.2 and picolibc): GCC 12 and later name
the CSR and fence.i extensions (`-march=rv32imac_zicsr_zifencei`), and
`picolibc_stdio.c` gives picolibc's stdio freedom-metal's tty, as
freedom-metal's gloss gives newlib's (SiFive's toolchain carries newlib).

CI's linux-gcc job runs it against freedom-e-sdk 4518a22 and freedom-metal
1cec4a2.
