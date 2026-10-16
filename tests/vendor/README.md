# Vendor frameworks

Firmware written for a vendor's own framework, built with that framework
unchanged and run on EmbSIM's boards under scenarios (01 §8.2). The
frameworks are fetched, not vendored; CI's linux-gcc job pins them.

| Directory | Framework | Board |
| --- | --- | --- |
| `arduino/` | ArduinoCore-avr (Blink, a tour of the core) | `emblink/arduino_uno` |
| `stm32cube/` | ST's STM32F1 HAL and CMSIS device files | `emblink/bluepill` |
| `pico-sdk/` | The Raspberry Pi Pico SDK | `emblink/pico` |
| `freedom-metal/` | SiFive's freedom-metal and freedom-e-sdk's examples | `emblink/hifive1_revb` |
| `nrfx/` | Nordic's nrfx drivers and MDK | `emblink/nrf52840dk` |

`vendor_run.py` is what their runners share.
