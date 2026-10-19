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
   - SPI1-4 as master and slave.
   - I2C1-3 as master.
   - IWDG and WWDG.
   - The RTC with the LSE.
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
10. **Vendor examples.** STM32CubeF4's NUCLEO-F446RE examples, unmodified:
    - GPIO_IOToggle and GPIO_EXTI;
    - UART_Printf, and UART with DMA;
    - TIM_TimeBase and TIM_PWMOutput;
    - ADC_RegularConversion_DMA;
    - SPI and I2C against catalog devices;
    - IWDG_Reset.
11. **EmbLinkRTOS on the board.** Its `nucleo_f446re` board and the
    conformance suite in its CI on EmbSIM.

Before step 10, **the core's speed.** The interpreter runs about 3.6
million instructions a second, a fiftieth of the part at 180 MHz. The
devices' clocks become lazy on Cortex-M as on the AVR (SysTick and the
timers told their cycles when their next event comes), and decoded
instructions are kept, so that an RTOS at full speed is usable on the
bench.

## 2. ESP32-C3-DevKitM-1 (ESP32-C3, RISC-V)

Started only once the NUCLEO-F446RE is done.

1. **The core.**
   - RV32IMC, the existing RV32IMAC core with the A extension off as the part
     has it.
   - Espressif's CPU interrupt controller (31 interrupts, priorities,
     threshold).
   - The interrupt matrix routing peripheral sources to CPU interrupts.
   - The debug trigger and PMP regions the part has.
2. **Boot and memory.**
   - The boot ROM's behaviour as observed from outside: strapping pins, the
     reset reasons, loading the ESP image's segments.
   - The ROM functions ESP-IDF calls, written from Espressif's ROM linker
     scripts.
   - The external SPI flash behind the cache and MMU (execute in place).
   - The eFuses (MAC, chip revision).
3. **Clocks and timers.**
   - The SYSTEM and RTC_CNTL clocks: PLL at 160 MHz, XTAL 40 MHz, RTC slow
     clock.
   - SYSTIMER, the RTOS tick.
   - Timer groups 0/1 with their watchdogs.
   - The RTC watchdog and super watchdog.
4. **GPIO and serial.**
   - GPIO with the GPIO matrix and IO MUX on the pins.
   - UART0/1.
   - USB Serial/JTAG as a host serial port.
5. **The rest.**
   - LEDC (PWM), RMT (the board's WS2812 on GPIO8), I2C, GP-SPI2, the SAR ADC
     and the temperature sensor.
   - TWAI is declared.
   - WiFi and Bluetooth answer as stubs, a scripted access point (Q11).
6. **The board.**
   - The WS2812 RGB LED on GPIO8 and the BOOT button on GPIO9.
   - The CP2102 bridge on UART0, as a host serial port with
     DTR/RTS auto-reset into download mode.
   - The 4 MB flash.
   - The power path and the headers.
7. **Vendor examples.** ESP-IDF's examples, unmodified: hello_world, blink
   with led_strip on RMT, gptimer, uart_echo, i2c, ledc, and the FreeRTOS
   it ships. Then Arduino-ESP32's basics.
8. **EmbLinkRTOS on the board.** Its RISC-V port moved from QEMU `virt`
   (CLINT and PLIC) to the ESP32-C3 (SYSTIMER and the interrupt matrix),
   with the conformance suite in its CI.
