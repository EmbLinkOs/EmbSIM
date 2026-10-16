# nrfx applications on Nordic's unmodified drivers

Evidence for 01 §8.2 (a board runs its vendor's examples): firmware written
against Nordic's nrfx drivers and MDK, as a bare-metal nRF5 application is,
runs on `emblink/nrf52840dk` and passes its scenario.

| Application | What it checks |
| --- | --- |
| `nrfx_tour` | `nrfx_clock` starting the HFXO and the LFXO, with an event for each; `nrfx_uarte` on UARTE0 at 115200 baud on the virtual COM port's pins, blocking transmits out and characters in through the driver's continuous RX buffers (`NRFX_UARTE_EVT_RX_BUF_REQUEST`, `RX_DONE`); `nrfx_timer` on TIMER1 at 1 MHz, 32 bits wide, with a compare every 100 ms and its CLEAR short, toggling LED1; `nrfx_rtc` on RTC1 from the LFCLK, its counter and a compare at one second; `nrfx_gpiote` with a channel on a button on D2 (P1.03, pulled up), its falling edge |

Nordic's and Arm's files are not vendored; `run.py` takes checkouts of
[nrfx](https://github.com/NordicSemiconductor/nrfx) (which carries the MDK
under `bsp/stable/mdk`) and [CMSIS_5](https://github.com/ARM-software/CMSIS_5)
(its `CMSIS/Core/Include` is all it reads):

```sh
git clone --depth 1 https://github.com/NordicSemiconductor/nrfx /tmp/nrfx
git clone --depth 1 https://github.com/ARM-software/CMSIS_5 /tmp/CMSIS_5
python3 tests/vendor/nrfx/run.py --nrfx /tmp/nrfx --cmsis /tmp/CMSIS_5 [--embsim build/dev/bin/embsim]
```

Each `apps/<name>/` holds what an nrfx application supplies itself:
`main.c` (with the interrupt handlers, which call the drivers'),
`nrfx_config.h` (the drivers and instances it enables, over nrfx's
templates), `nrfx_glue.h` (CMSIS's NVIC, PRIMASK critical sections, GCC's
atomics) and `nrfx_log.h` (no logging). They are compiled with the
drivers' sources, the MDK's `system_nrf52840.c` and
`gcc_startup_nrf52840.S`, all unchanged, for `NRF52840_XXAA`
(`-mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2`), and linked with
the MDK's `nrf52840_xxaa.ld` and newlib-nano. `project.json` (the DK on USB
power, a button on D2, a terminal on P0.06/P0.08) then runs the image under
the application's `scenario.json`. Without arm-none-eabi-gcc or the
checkouts it skips.

`system_nrf52840.c`'s workaround for errata 66 copies FICR's calibration
into the TEMP peripheral's linearity registers, and TEMP has no behaviour
here (its registers keep what is written): that is one
`mcu.unsupported_access` warning.

CI's linux-gcc job runs it against nrfx 4.6.0 (d1f2c35, a sparse checkout
of what the build reads) and CMSIS_5 55b1983, with Ubuntu 24.04's GCC
13.2.
