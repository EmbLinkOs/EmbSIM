# Vendor frameworks

Firmware written for a vendor's own framework, built with that framework
unchanged and run on EmbSIM's boards under scenarios (01 §8.2). The
frameworks are fetched, not vendored; CI's linux-gcc job pins them, and
its esp-idf and arduino-esp32 jobs ESP-IDF and Arduino-ESP32 with their
toolchains.

| Directory | Framework | Board |
| --- | --- | --- |
| `arduino/` | ArduinoCore-avr (Blink, a tour of the core) | `emblink/arduino_uno` |
| `stm32cube/` | ST's STM32F1 HAL and CMSIS device files | `emblink/bluepill` |
| `stm32cubef4/` | STM32CubeF4's own examples for the NUCLEO-F446RE (GPIO, HAL time bases, RCC, RTC, timers, UART, PWR's low-power modes, I2C between two boards, EEPROM emulation in flash) and, with `--board nucleo_f446ze`, for the NUCLEO-F446ZE (ADC, CRC, Cortex, DMA, flash erase and program, GPIO, the watchdogs, RTC, seven timer examples, UART, the USB device HID mouse on a computer's port, the USB host HID and MSC demos with Adafruit's 1.8" TFT shield, a mouse and a flash drive; their middleware and components, stm32-mw-usb-device v2.11.4, stm32-mw-usb-host, stm32-mw-fatfs, stm32-st7735 and stm32-bsp-common at the commits CubeF4 pins, fetched like the rest and copied into CubeF4's tree, with its Adafruit_Shield BSP and Utilities' LCD log and fonts) | `emblink/nucleo_f446re`, `emblink/nucleo_f446ze` |
| `pico-sdk/` | The Raspberry Pi Pico SDK | `emblink/pico` |
| `micropython/` | MicroPython v1.24.1's rp2 port (RPI_PICO), built with its own Makefile and copied as firmware.uf2 onto the Pico's BOOTSEL drive; its prompt over its USB serial port: arithmetic, `machine.Pin` lighting the LED, a file in its flash filesystem | `emblink/pico` |
| `freedom-metal/` | SiFive's freedom-metal and freedom-e-sdk's examples | `emblink/hifive1_revb` |
| `nrfx/` | Nordic's nrfx drivers and MDK | `emblink/nrf52840dk` |
| `freertos/` | The FreeRTOS kernel (V11.1.0), its tasks read back through the image's symbols and DWARF (`run --rtos`, scenario `rtos` assertions) | `emblink/nucleo_f446re` |
| `esp-idf/` | ESP-IDF v5.4's own examples (hello_world, blink, LEDC, the temperature sensor, the ADC, an I2C EEPROM, the general-purpose timer, a UART echo, FreeRTOS's run-time statistics; Wi-Fi's scan, station, softAP with a station, ESP-NOW and http_request (hello_world and http_request through a USB power meter); BLE's NimBLE bleprph and blecent and Bluedroid's ble_ibeacon, some on two boards), built by `idf.py` into a flash image | `emblink/esp32c3_devkitm1` |
| `arduino-esp32/` | Arduino-ESP32 3.2.0 through arduino-cli, as the IDE builds for "ESP32C3 Dev Module": the IDE's Blink, DigitalReadSerial and Fade, the core's GetChipID, RepeatTimer, WiFiScan and WiFiClientConnect, the BLE library's Server and iBeacon, HTTPClient's BasicHttpClient | `emblink/esp32c3_devkitm1` |

`vendor_run.py` is what their runners share.

## Experimental Xtensa integer kernel

`xtensa_integer/run.py` uses a separate GNU Xtensa assembler and objcopy, plus
Python arithmetic, to check 5,376 wide integer instructions against the original
C kernel. Run with `--assembler PATH --objcopy PATH` on Linux with a host C
compiler. No toolchain is vendored or linked. LX106 binutils supports this
shared integer subset; it does not qualify the full ESP32 LX6 configuration.
See [scope and reproduction](../../docs/engineering/esp32-dual-core.md).

`xtensa_integer/run_execute.py` additionally uses a separate linker, with
relaxation disabled, for 3,840 flow/memory/density cases and 960 rejected bus
accesses. Supply `--assembler`, `--objcopy` and `--linker` paths. It compares all
registers, PC and exact bus transactions, including failed-access retention.
It does not establish architectural exception delivery or complete ISA fidelity.

`xtensa_integer/run_c.py` compiles unchanged sum/sort/copy C routines using
external CALL0 GCC and runs 96 cases through instruction fetch and the callback
bus. Supply `--compiler`, `--objcopy`, `--linker` and `--nm`. Checks include whole
buffers and guards, source retention, return PC and callee-saved registers. The
reference result used GCC xtensa-lx106 10.2.1 at `-O1`; this tests shared
instructions, not the full LX6 configuration, ESP-IDF boot or a real board.

`xtensa_integer/run_windows_c.py` uses the actual Espressif ESP32 windowed-ABI
compiler. Without vector input, 128 sum/sort/copy/nested-call executions pass.
With `--vectors-source` pointing to the hash-pinned ESP-IDF v4.4.7
`components/freertos/port/xtensa/xtensa_vectors.S`, 224 runs (160 C, 64 assembly)
execute all six vendor window spill/fill bodies via actual bus accesses and
RFWO/RFWU retries. The generated temporary assembly retains the source's
permissive copyright/permission notice; no vendor handler is linked into the
app. The runner validates the source SHA-256 before extraction. See the target
contract for exact toolchain/source hashes and commands. This qualifies selected
window/loop behavior, not general RTOS context switching, ESP-IDF boot or SoC
peripherals. Compiler/linker/objcopy/nm are external tools, not dependencies of
the runnable app.
