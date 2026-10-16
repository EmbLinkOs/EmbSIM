# Pico SDK applications on the unmodified SDK

Evidence for 01 §8.2 (a board runs its vendor's examples): firmware built
with the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk),
by the SDK's own CMake, runs on `emblink/pico` and passes its scenario.

| Application | What it checks |
| --- | --- |
| `sdk_tour` | `runtime_init`'s clocks (`clock_get_hz(clk_sys)` 125 MHz from the crystal through PLL_SYS); `sqrtf`, `sinf`, `sqrt`, `exp` and `printf`'s `%f` through the bootrom's float and double tables; the SIO divider; `multicore_launch_core1` and the FIFO; `flash_range_erase` and `flash_range_program` through the bootrom, read back through the XIP; `add_repeating_timer_ms` toggling the LED; `gpio_set_irq_enabled_with_callback` on a button; stdio on UART0 at 115200 baud, `printf` out and `getchar_timeout_us` in |

The SDK runs as it is because EmbSIM's ROM carries the bootrom's documented
functions, done by the host (ADR-021): the bootrom's own code is not
shipped.

The SDK is not vendored; `run.py` takes a checkout of it (no submodules:
the application uses no USB):

```sh
git clone --depth 1 --branch 2.3.1 https://github.com/raspberrypi/pico-sdk /tmp/pico-sdk
python3 tests/vendor/pico-sdk/run.py --sdk /tmp/pico-sdk [--embsim build/dev/bin/embsim]
```

Each `apps/<name>/` is an SDK project (`CMakeLists.txt` with
`pico_sdk_import.cmake`, its sources). `run.py` configures it with
`PICO_BOARD=pico` and `CMAKE_BUILD_TYPE=Release`, builds it with Ninja and
arm-none-eabi-gcc, and runs the ELF in `project.json` (the Pico on USB
power, a button on GP15, a terminal on GP0/GP1) under the application's
`scenario.json`. Without the tools or a checkout it skips.

The SDK's `runtime_init_usb_power_down` reads and writes USB's `SIE_CTRL`
while RESETS holds USB in reset, which the part ignores; EmbSIM does the
same and says so once (`mcu.held_in_reset`).

CI's linux-gcc job runs it against the SDK's 2.3.1 release (079c6f3) with
Ubuntu 24.04's GCC 13.2.
