# Raspberry Pi Pico: both RP2040 cores, the FIFOs, a spinlock and each core's divider (firmware)

The bundled Raspberry Pi Pico running [`firmware/main.c`](firmware/main.c)
on both of its RP2040's Cortex-M0+ cores, on the registers (no SDK). It
boots as [`pico-blink-serial`](../pico-blink-serial) does, through its own
boot2, and then:

- core 0 brings clk_sys to 125 MHz from PLL_SYS, sets UART0 up at 115200
  baud on GP0 and GP1, and launches core 1 as the pico-sdk's
  `multicore_launch_core1` does: core 1 reset through the PSM
  (`FRCE_OFF.PROC1` set, then cleared), then the words 0, 0, 1, the vector
  table, core 1's stack and its entry sent through the FIFO, each echoed by
  core 1's bootrom;
- both cores add 1000 to one counter, each addition under spinlock 31: the
  cores run interleaved, instruction by instruction, so the lock is what
  makes the total 2000;
- core 1 blinks the LED on GPIO25 at 1 Hz from the timer's alarm 1 (its own
  NVIC), takes the button on GP15 through IO_BANK0's PROC1 interrupt, and
  answers the numbers core 0 sends it through the FIFO with n * 1000 / 7
  from its own divider, while core 0 prints with its divider;
- core 0 prints what core 1 sends back (SIO_IRQ_PROC0) and echoes the
  terminal in upper case; both cores wait in WFI between interrupts.

The scenario checks the banner (`pico: two cores, clk_sys 125000 kHz, core
0 CPUID 0`), `core 1 CPUID 1, shared 2000`, core 1's three answers (`core 1:
1000 * 1000 / 7 = 142857`), the LED off at 0.4 s and on at 0.6 s with four
edges in two seconds, the button pressed at 1.2 s printed as `button 1
(core 1)`, and `two cores` typed at 1.5 s echoed as `TWO CORES`.

```sh
embsim run examples/pico-dual-core/project.json --scenario examples/pico-dual-core/scenarios/two-cores.json
```

The image is committed; to rebuild it with clang and lld (or EmbCC):

```sh
cd examples/pico-dual-core && embsim build project.json [--toolchain embcc]
```
