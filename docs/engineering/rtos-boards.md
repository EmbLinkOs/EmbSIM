# The RTOS boards: NUCLEO-F446RE, then ESP32-C3-DevKitM-1

**Status:** the owner's order of work from 2026-10-19. Each board is finished
completely, in its own pull request, before the next one starts. Work that
does not serve the board in progress waits until it is done.

EmbLinkRTOS needs one complete Cortex-M board and one complete RISC-V board
to be developed against. The Cortex-M board is the one EmbLinkRTOS chose
for its own port: the STM32F4, as the NUCLEO-F446RE (its ADR-025). The
RISC-V board is the ESP32-C3, which is already EmbSIM's choice for the
flagship's ESP32-class board (Q11). Both answer ADR-030's five questions
like any other model.

## Definition of done, for each board

A board is done when all of the following hold:

1. **Its MCU.** Every peripheral of the MCU that the vendor's SDK, its usual
   libraries, or an RTOS port touches is modelled. That includes every mode
   and edge case the reference manual describes. Anything left out is
   declared in the part's fidelity and coverage table with the reason.
   `docs/coverage.md` shows no undeclared gap.
2. **Its board.** Every on-board device behaves as the schematic says:
   - LEDs, buttons and their RC filters;
   - the crystal or clock source;
   - the USB-serial bridge, as a host serial port;
   - the power path;
   - the headers, as ports.

   The board's `reference.json` qualifies the artwork against the vendor's
   mechanical drawing.
3. **Vendor code, unmodified.** The vendor's own examples run as they are and
   pass scenarios: STM32CubeF4's NUCLEO-F446RE examples for the first board,
   ESP-IDF's examples for the second. They are built with the vendor's own
   toolchain flags and startup files.
4. **The RTOS.** EmbLinkRTOS's conformance suite passes on the board in
   EmbSIM, in EmbLinkRTOS's CI, as it already does on the Uno (SIM-002).
5. **Evidence and docs.** The standard gates are met:
   - every preset and vendor suite passes;
   - STATUS, CHANGELOG, the MCU's and the board's fidelity, `docs/coverage.md`
     and a gallery screenshot are updated;
   - CI is green.

## 1. NUCLEO-F446RE (STM32F446RE, Cortex-M4F)

Order of work. Each step lands with its tests, and the workbench works
after each one.

1. **Cortex-M4F core completeness.**
   - **The MPU (PMSAv7).** Eight regions, subregion disables, `PRIVDEFENA`,
     MemManage faults and `MMFAR`.
   - **The FPU's lazy stacking** (`FPCCR`, `FPCAR`, the extended frame).
   - **Privileged and unprivileged execution** (`CONTROL.nPRIV`, `SPSEL`),
     SVCall and PendSV.
   - **The configurable faults** with their status registers (`CFSR`,
     `HFSR`, `BFAR`).
   - **Proof:** these are what an RTOS port depends on, so they are checked
     against QEMU's mps2-an386 differential runs and the Armv7-M ARM.
2. **The STM32F446 register file.** Converted from ST's SVD (licence read
   under ADR-029) and checked against RM0390.
3. **Clocks and power.**
   - RCC with HSI 16 MHz, HSE (8 MHz from the ST-Link's MCO), the main PLL
     and PLLSAI/PLLI2S.
   - The bus prescalers to 180 MHz.
   - FLASH wait states and the ART accelerator.
   - PWR voltage scaling and over-drive.
   - **Proof:** the HAL's `SystemClock_Config` at 180 MHz runs unmodified.
   - *Done:* `tests/unit/test_stm32f4.c` drives the registers in the HAL's
     order; STM32CubeF4's RCC_ClockConfig clock functions (HSI and HSE
     bypass to 180 MHz with over-drive, and back) run unmodified on a bench
     board, and become a scenario with the board in step 10. The wait
     states and the ART accelerator are checked (a warning), not timed:
     timing stays estimated (ADR-015).
4. **GPIO, SYSCFG and EXTI.**
   - GPIO with the F4's MODER, OTYPER, OSPEEDR, PUPDR and AFR registers on
     the pins.
   - SYSCFG's EXTI routing.
   - EXTI: edges, software trigger, pending.
   - *Done:* `stm32f4-gpio` (with LCKR's lock sequence and pin speed),
     `stm32f4-syscfg` (EXTICR, the boot remap, CMPCR), EXTI's 23 lines with
     events, the alternate-function table from ST's open pin data, and
     RCC's peripheral reset bits; tests/unit/test_stm32f4.c.
5. **Timers and SysTick.**
   - TIM1/TIM8 (advanced), TIM2-5 (general, 32-bit), TIM6/7 (basic) and
     TIM9-14.
   - Time base, output compare, PWM on the pins, input capture, one-pulse
     mode and encoder mode.
   - *Done:* `stm32-tim` rewritten as the whole timer (counting modes,
     repetition, capture with filters, complementary outputs with dead
     time and break, LOCK, COM, slave modes, encoder, external clocks,
     TRGO between timers) for both families, and SysTick's HCLK/8
     reference; the DMA requests come with step 7.
6. **USART and UART.** USART1-3, UART4-5 and USART6: frames on the pins,
   oversampling 8 and 16, interrupts. USART2 is the board's ST-Link virtual
   COM port.
   *Done:* `stm32-usart` rewritten (formats, OVER8, errors, IDLE, breaks
   and LIN, mute mode, flow control, half-duplex, synchronous clock) and
   the runtime's frame formats both ways; smartcard and IrDA declared.
7. **DMA.**
   - DMA1/DMA2 streams and channels, FIFO, circular and double-buffer modes.
   - Used by the UART, SPI, ADC and I2C HAL examples.
   - *Done:* `stm32f4-dma` with RM0390's request mapping (direct and FIFO
     modes, circular, double buffer, memory-to-memory, the flags and
     interrupts) and the USARTs' and timers' requests; the ADC's, SPI's,
     I2C's and DAC's come with their models in step 8.
8. **ADC, DAC, SPI, I2C and the watchdogs.**
   - ADC1-3: regular and injected groups, scan, continuous.
   - The DAC.
     *Done:* `stm32f4-adc` and `stm32f4-adc-common` (both groups, the
     triggers, DMA, the watchdog, the internal channels against the
     part's calibration words, the dual and triple modes) and `stm32f4-dac`
     (triggers, waves, DMA, the buffer), the timers' and EXTI's triggers.
   - SPI1-4 as master and slave.
     *Done:* `stm32-spi`, shared with the F1 (it replaces `stm32f1-spi`):
     master and slave, receive-only and bidirectional, NSS and MODF, the
     TI mode, CRC, DMA requests with the CRC after a stream's last item.
   - I2C1-3 as master.
     *Done:* `stm32-i2c`, shared with the F1 (it replaces `stm32f1-i2c`):
     master and slave, 10-bit addresses, stretching, arbitration, BUSY and
     BERR from the bus, PEC, SMBus, DMA with LAST.
   - IWDG and WWDG.
   - The RTC with the LSE.
     *Done:* `stm32-iwdg` and `stm32-wwdg` (both families), `stm32f4-rtc`
     (calendar, alarms, wakeup, timestamp and tamper, RTC_OUT,
     calibration, backup registers), RCC_CSR's reset flags, and the
     backup domain kept through resets.
   - CAN, SDIO, QSPI, SAI, SPDIF, HDMI-CEC and USB OTG are declared as
     omissions until a later step takes them up.
9. **The board (MB1136).**
   - LD2 on PA5, B1 on PC13 with its RC.
   - LD1 (ST-Link comms) and LD3 (power).
   - The 8 MHz MCO and the 32.768 kHz LSE crystal.
   - The power path: U5V, E5V and VIN through the LD39050 regulator.
   - The Arduino Uno V3 and ST morpho headers, as ports.
   - The ST-Link's virtual COM port as a host serial port.
   - Artwork and `reference.json`.
   *Done:* `emblink/nucleo_f446re` (MB1136 C-03) with LD2, B1 and its
   filter, B2, LD3, the MCO and X2, the power paths through T2, JP5, U3, D4,
   U4 and JP6 (IDD, with an ammeter's place), both header sets as ports,
   the VCP as the console and as a host serial port (no DTR reset), the
   artwork, the linker script and the toolchain profiles;
   `examples/nucleo-f446re-blink-serial`. LD1 and the ST-LINK's own
   behaviour are declared omissions. `reference.json` waits for a
   dimensioned drawing (UM1724's is an image) to place the headers exactly.
10. **Vendor examples.** STM32CubeF4's own examples for the NUCLEO-F446RE
    (Projects/STM32446E-Nucleo), unmodified, each under a scenario:
    - GPIO_IOToggle; HAL_TimeBase_TIM, _RTC_ALARM and _RTC_WKUP;
    - RCC_ClockConfig; RTC_Calendar; TIM_PWMInput;
    - UART_Printf and UART_ReceptionToIdle_CircularDMA;
    - PWR_CurrentConsumption in each of its six low-power modes, which
      needs the F4's Stop and Standby;
    - the four I2C_TwoBoards examples on two boards wired together;
    - EEPROM_Emulation, which needs the flash controller.
    *Done:* `tests/vendor/stm32cubef4/run.py` builds them as their
    STM32CubeIDE projects do; all fourteen pass, PWR_CurrentConsumption in
    its six modes (21 scenarios). It took Stop and Standby
    (`stm32f4-pwr`), the flash interface (`stm32f4-flash`), probes of
    registers, memory and variables, two cores on one circuit running a
    quantum apart rather than an instruction, and I2C's STOPF and DR as
    the manual has them.
11. **EmbLinkRTOS on the board.** Its `nucleo_f446re` board and the
    conformance suite in its CI on EmbSIM.
    *Done:* EmbLinkRTOS's `boards/st/nucleo_f446re` takes the part from
    HSI to 180 MHz from the ST-LINK's MCO (HSE bypass, the PLL,
    over-drive, five wait states) and writes its console to USART2. Its
    ten conformance images pass on `emblink/nucleo_f446re` (preset
    `cm4-nucleo-embsim`), in its `cm4-embsim` CI job too.

Before step 10, **the core's speed.** The devices' clocks become lazy on
Cortex-M as on the AVR (SysTick and the timers told their cycles when
their next event comes), and decoded instructions are kept, so that an
RTOS at full speed is usable on the bench.
*Done:*
- The STM32s' device clocks run lazily.
- The Cortex-M core keeps its decoded instructions per region generation
  and runs in bursts.
- A busy firmware at 180 MHz runs about 31 million instructions a second
  on a release build (21 million before), a sixth of the part's speed.
- Firmware that sleeps in WFI runs faster than real time.
- Real time for busy code needs a translating core, which is later work.

## 2. ESP32-C3-DevKitM-1 (ESP32-C3, RISC-V)

Started only once the NUCLEO-F446RE is done.

1. **The core.**
   - RV32IMC, the existing RV32IMAC core with the A extension off as the part
     has it.
   - Espressif's CPU interrupt controller (31 interrupts, priorities,
     threshold).
   - The interrupt matrix routing peripheral sources to CPU interrupts.
   - The debug trigger and PMP regions the part has.

   *Done:* the ESP-RV core (`esp-rv32imc`) with the part's CSRs, counter,
   triggers and PMP, and the interrupt matrix and controller
   (`esp32c3-intmtx`), on `espressif/esp32c3`'s register file converted
   from Espressif's SVD (`test_esp32c3`).
2. **Boot and memory.**
   - The boot ROM's behaviour as observed from outside: strapping pins, the
     reset reasons, loading the ESP image's segments.
   - The ROM functions ESP-IDF calls, written from Espressif's ROM linker
     scripts.
   - The external SPI flash behind the cache and MMU (execute in place).
   - The eFuses (MAC, chip revision).

   *Done:* Espressif's own boot ROM runs from the reset vector and boots
   the in-package flash (an XMC XM25QH32C, `nor.c`) through SPI1
   (`esp32c3-spimem`), checking the image's SHA-256 on the SHA accelerator
   (`esp32c3-sha`); the cache and MMU (`esp32c3-cache`) map the flash on
   both buses; the RTC watchdog's flash-boot protection and the PC logging
   (`esp32c3-assist`) give the ROM's `RTCWDT_RTC_RST` and `Saved PC`. A
   project's `bin` firmware is a flash image (`test_esp32c3`). The ROM's
   functions are its own code, so nothing is written from the linker
   scripts. Moved to step 3: the eFuse controller (its values are the
   register file's reset values for now).
3. **Clocks and timers.**
   - The SYSTEM and RTC_CNTL clocks: PLL at 160 MHz, XTAL 40 MHz, RTC slow
     clock.
   - SYSTIMER, the RTOS tick.
   - Timer groups 0/1 with their watchdogs.
   - The RTC watchdog and super watchdog.

   *Done:* SYSTEM's clocks, enables, resets and software interrupts
   (`esp32c3-system`), the system timer (`esp32c3-systimer`), the timer
   groups with their MWDTs and the RTC_SLOW_CLK calibration
   (`esp32c3-timg`), and the eFuse controller moved here from step 2
   (`esp32c3-efuse`); the RTC watchdog came with step 2. The super
   watchdog's own timeout waits for a value: the TRM gives none.
4. **GPIO and serial.**
   - GPIO with the GPIO matrix and IO MUX on the pins.
   - UART0/1.
   - USB Serial/JTAG as a host serial port.

   *Done:* the GPIO matrix and IO MUX on the pins (`esp32c3-gpio`,
   `esp32c3-iomux`: each pad's reset configuration from the TRM's table
   5.12-1, output and input routing, the interrupts, the UARTs' TX and RX
   wherever the matrix puts them); UART0 and UART1 came with step 1. The
   USB Serial/JTAG controller (`esp32c3-usbjtag`) is the DevKitM-1's: no
   host on GPIO18 and GPIO19, so its buffer fills and the ROM's and
   ESP-IDF's consoles time out on it as on the board; the host serial port
   it would be moved to ADR-031's native USB devices. With it, what
   ESP-IDF's start needs: the analog blocks' I2C master (`regi2c.c`), the
   random number generator (`esp32c3-apbctrl`), the radio's register
   blocks (`radio.c`), the ROM's layout pointer, the performance counter as
   the ROM starts it, the timer group's periodic calibration and a CPU
   reset clearing the permission control. ESP-IDF v5.4's hello_world,
   built by its own tools and flashed as `idf.py` merges it, runs
   unmodified: the ROM, the second-stage bootloader (partition table,
   six segments checked and loaded), the app's start at 160 MHz,
   FreeRTOS, `Hello world!`, and `esp_restart` back to the ROM with
   `rst:0xc (RTC_SW_CPU_RST)`, again and again.
5. **The rest.**
   - LEDC (PWM), RMT (the board's WS2812 on GPIO8), I2C, GP-SPI2, the SAR ADC
     and the temperature sensor.
   - TWAI is declared.
   - WiFi and Bluetooth answer as stubs, a scripted access point (Q11).

   *Done* but for the radios' stubs: the GPIO matrix carries every modelled
   peripheral's signals both ways; RMT (`esp32c3-rmt`), LEDC
   (`esp32c3-ledc`), the I2C controller as master and slave
   (`esp32c3-i2c`), GP-SPI2 as a master (`esp32c3-spi2`) with the GDMA
   (`esp32c3-gdma`), the SAR ADCs and the temperature sensor
   (`esp32c3-adc`), each from its TRM chapter and checked against what
   ESP-IDF's HAL does with it (`test_esp32c3`). TWAI, I2S, UHCI, the crypto
   accelerators but SHA and GP-SPI2's slave mode are declared. The radios
   stay declared too: ESP-IDF drives their MAC and PHY from binary
   libraries through registers Espressif does not document, so a stub that
   answers them (a scripted access point) is its own piece of work, after
   step 7 shows what ESP-IDF's examples need.
6. **The board.**
   - The WS2812 RGB LED on GPIO8 and the BOOT button on GPIO9.
   - The CP2102 bridge on UART0, as a host serial port with
     DTR/RTS auto-reset into download mode.
   - The 4 MB flash.
   - The power path and the headers.

   *Done, before step 5,* so that the chip runs in a project, in the
   workbench and under `embsim run --target` as the RTOS's runner needs:
   `emblink/esp32c3_devkitm1` from Espressif's V1.0 schematic and dimension
   drawing. The ESP32-C3-MINI-1 is a package of the chip (`mini1`); the
   micro-USB's VBUS feeds 5 V through a 1N5819 and the SGM2212-3.3 makes
   3V3 (a preset from its datasheet); R5 and C6 hold CHIP_PU low for some
   14 ms at power-up, RST (SW2) pulls it down and the chip starts with
   `rst:0x1 (POWERON)`, BOOT (SW1) held through it gives `boot:0x4
   (DOWNLOAD(USB/UART0/1))` and `waiting for download`; R20 holds GPIO8
   high. The LED is not a WS2812 but an SK68XXMINI-HS (OPSCO's figures, a
   new part, `emblink/sk6812mini_hs`), run from 3.3 V below its rated
   3.7 V as Espressif runs it. The CP2102N is the console's bridge on UART0
   and a host serial port; its DTR/RTS auto-program circuit waits for
   modem lines a pseudo-terminal does not have, so esptool's reset into
   download mode is not reachable yet (BOOT and RST are). `run --target`
   now plugs a board into a USB port. ESP-IDF v5.4's hello_world runs on
   the board from its flash image (`embsim run --target
   emblink/esp32c3_devkitm1 --image flash.bin --console`).
7. **Vendor examples.** ESP-IDF's examples, unmodified: hello_world, blink
   with led_strip on RMT, gptimer, uart_echo, i2c, ledc, and the FreeRTOS
   it ships. Then Arduino-ESP32's basics.

   *In progress:* `tests/vendor/esp-idf/` builds ESP-IDF v5.4's examples
   with `idf.py` as a user does and runs the flash image under scenarios:
   hello_world, blink (the SK6812 through RMT and the registry's
   led_strip), ledc_basic, temp_sensor, adc_oneshot_read, i2c_eeprom
   (whose EEPROM, a 24LC256, `emblink/eeprom_24lc256`, is new), gptimer,
   uart_echo (to a terminal on IO4 and IO5) and real_time_stats (the
   FreeRTOS ESP-IDF ships, its run-time statistics). Running them found a
   runtime fault every board had (a sleeping core's devices got no quantum
   for an event an input gave them) and one in the GPIO matrix (a UART's RX
   pin given by IN_SEL was not the runtime's).
   The example waits one FreeRTOS tick for the EEPROM's write cycle, so
   with a part at its datasheet's 5 ms the read is NACKed, as on a bench;
   the project's part writes in 2 ms. CI's esp-idf job fetches ESP-IDF at
   v5.4 with its toolchain and runs them. Still to come: Arduino-ESP32's
   basics.
8. **EmbLinkRTOS on the board.** Its RISC-V port moved from QEMU `virt`
   (CLINT and PLIC) to the ESP32-C3 (SYSTIMER and the interrupt matrix),
   with the conformance suite in its CI.
