# Vendor frameworks

Firmware written for a vendor's own framework, built with that framework
unchanged and run on EmbSIM's boards under scenarios (01 §8.2). The
frameworks are fetched, not vendored; CI's linux-gcc job pins them, and
its esp-idf job ESP-IDF with its toolchain.

| Directory | Framework | Board |
| --- | --- | --- |
| `arduino/` | ArduinoCore-avr (Blink, a tour of the core) | `emblink/arduino_uno` |
| `stm32cube/` | ST's STM32F1 HAL and CMSIS device files | `emblink/bluepill` |
| `stm32cubef4/` | STM32CubeF4's own examples for the NUCLEO-F446RE (GPIO, HAL time bases, RCC, RTC, timers, UART, PWR's low-power modes, I2C between two boards, EEPROM emulation in flash) | `emblink/nucleo_f446re` |
| `pico-sdk/` | The Raspberry Pi Pico SDK | `emblink/pico` |
| `freedom-metal/` | SiFive's freedom-metal and freedom-e-sdk's examples | `emblink/hifive1_revb` |
| `nrfx/` | Nordic's nrfx drivers and MDK | `emblink/nrf52840dk` |
| `esp-idf/` | ESP-IDF v5.4's own examples (hello_world, blink, LEDC, the temperature sensor, the ADC, an I2C EEPROM, the general-purpose timer, a UART echo, FreeRTOS's run-time statistics), built by `idf.py` into a flash image | `emblink/esp32c3_devkitm1` |

`vendor_run.py` is what their runners share.
