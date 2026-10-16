# STM32 HAL applications on the unmodified HAL

Evidence for 01 §8.2 (a board runs its vendor's examples): firmware written
against ST's STM32F1 HAL, as a CubeMX project is, runs on `emblink/bluepill`
and passes its scenario.

| Application | What it checks |
| --- | --- |
| `hal_tour` | `HAL_RCC_OscConfig` and `HAL_RCC_ClockConfig` to 72 MHz from the 8 MHz crystal through the PLL (flash latency 2, APB1 at 36 MHz), as the HAL then reports them; `HAL_Delay` on SysTick; `HAL_GPIO_TogglePin` on PC13; a button on PA0 through EXTI0 (`HAL_GPIO_EXTI_Callback`); TIM2's update interrupt at 10 Hz (`HAL_TIM_PeriodElapsedCallback`); USART1 at 115200 baud, `HAL_UART_Transmit` out and `HAL_UART_Receive_IT` in |

ST's and Arm's files are not vendored; `run.py` takes a directory holding
checkouts of
[stm32f1xx_hal_driver](https://github.com/STMicroelectronics/stm32f1xx_hal_driver),
[cmsis_device_f1](https://github.com/STMicroelectronics/cmsis_device_f1) and
[CMSIS_5](https://github.com/ARM-software/CMSIS_5) (its `CMSIS/Core/Include`
is all it reads):

```sh
mkdir /tmp/st && cd /tmp/st
git clone --depth 1 https://github.com/STMicroelectronics/stm32f1xx_hal_driver
git clone --depth 1 https://github.com/STMicroelectronics/cmsis_device_f1
git clone --depth 1 https://github.com/ARM-software/CMSIS_5
python3 tests/vendor/stm32cube/run.py --st /tmp/st [--embsim build/dev/bin/embsim]
```

Each `apps/<name>/` holds what a CubeMX project's own files would: `main.c`
(with the MSP hooks, the callbacks and the interrupt handlers) and
`stm32f1xx_hal_conf.h`. They are compiled with the HAL's sources and
`system_stm32f1xx.c`, assembled with `startup_stm32f103xb.s`, and linked
with `STM32F103XB_FLASH.ld` and newlib-nano, all unchanged, with a CubeMX
Makefile's flags (`-mcpu=cortex-m3 -mthumb -Og`, `-specs=nano.specs`,
`--gc-sections`). `project.json` (the Blue Pill, a button on PA0, a
terminal on PA9/PA10) then runs the image under the application's
`scenario.json`. Without arm-none-eabi-gcc or the checkouts it skips.

CI's linux-gcc job runs it against stm32f1xx_hal_driver bd368aa,
cmsis_device_f1 c8e9a4a and CMSIS_5 55b1983, with Ubuntu 24.04's GCC
13.2.
