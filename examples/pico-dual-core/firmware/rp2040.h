/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* rp2040.h -- the RP2040 registers the example uses, by address (the
 * RP2040 datasheet's chapter 2 and 4). Each peripheral's registers are
 * also at +0x1000 (XOR), +0x2000 (set) and +0x3000 (clear). */
#ifndef RP2040_H
#define RP2040_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define XOR(a) REG((a) + 0x1000u)
#define SET(a) REG((a) + 0x2000u)
#define CLR(a) REG((a) + 0x3000u)

#define SYSINFO_CHIP_ID 0x40000000u

#define CLOCKS 0x40008000u
#define CLK_REF_CTRL (CLOCKS + 0x30u)
#define CLK_REF_SELECTED (CLOCKS + 0x38u)
#define CLK_SYS_CTRL (CLOCKS + 0x3cu)
#define CLK_SYS_SELECTED (CLOCKS + 0x44u)
#define CLK_PERI_CTRL (CLOCKS + 0x48u)
#define FC0_REF_KHZ (CLOCKS + 0x80u)
#define FC0_MIN_KHZ (CLOCKS + 0x84u)
#define FC0_MAX_KHZ (CLOCKS + 0x88u)
#define FC0_INTERVAL (CLOCKS + 0x90u)
#define FC0_SRC (CLOCKS + 0x94u)
#define FC0_STATUS (CLOCKS + 0x98u)
#define FC0_RESULT (CLOCKS + 0x9cu)

#define RESETS_RESET 0x4000c000u
#define RESETS_DONE 0x4000c008u
#define RESET_IO_BANK0 (1u << 5)
#define RESET_PADS_BANK0 (1u << 8)
#define RESET_PLL_SYS (1u << 12)
#define RESET_TIMER (1u << 21)
#define RESET_UART0 (1u << 22)
#define RESET_SYSINFO (1u << 19)

#define IO_BANK0 0x40014000u
#define GPIO_CTRL(n) (IO_BANK0 + 8u * (n) + 4u)
#define IO_INTR(n) (IO_BANK0 + 0xf0u + 4u * (n))
#define IO_PROC0_INTE(n) (IO_BANK0 + 0x100u + 4u * (n))
#define FUNC_UART 2u
#define FUNC_SIO 5u

#define PADS_BANK0 0x4001c000u
#define PAD(n) (PADS_BANK0 + 4u + 4u * (n))
#define PAD_IE (1u << 6)
#define PAD_PUE (1u << 3)
#define PAD_PDE (1u << 2)

#define XOSC_CTRL 0x40024000u
#define XOSC_STATUS 0x40024004u
#define XOSC_STARTUP 0x4002400cu

#define PLL_SYS_CS 0x40028000u
#define PLL_SYS_PWR 0x40028004u
#define PLL_SYS_FBDIV 0x40028008u
#define PLL_SYS_PRIM 0x4002800cu

#define UART0 0x40034000u
#define UART_DR (UART0 + 0x00u)
#define UART_FR (UART0 + 0x18u)
#define UART_IBRD (UART0 + 0x24u)
#define UART_FBRD (UART0 + 0x28u)
#define UART_LCR_H (UART0 + 0x2cu)
#define UART_CR (UART0 + 0x30u)
#define UART_IMSC (UART0 + 0x38u)
#define FR_RXFE (1u << 4)
#define FR_TXFF (1u << 5)

#define TIMER 0x40054000u
#define TIMER_ALARM0 (TIMER + 0x10u)
#define TIMER_RAWL (TIMER + 0x28u)
#define TIMER_INTR (TIMER + 0x34u)
#define TIMER_INTE (TIMER + 0x38u)

#define WATCHDOG_TICK 0x4005802cu

#define SIO 0xd0000000u
#define SIO_GPIO_OUT_CLR (SIO + 0x18u)
#define SIO_GPIO_OUT_XOR (SIO + 0x1cu)
#define SIO_GPIO_OE_SET (SIO + 0x24u)
#define SIO_DIV_UDIVIDEND (SIO + 0x60u)
#define SIO_DIV_UDIVISOR (SIO + 0x64u)
#define SIO_DIV_QUOTIENT (SIO + 0x70u)
#define SIO_DIV_REMAINDER (SIO + 0x74u)
#define SIO_DIV_CSR (SIO + 0x78u)

#define SIO_CPUID (SIO + 0x00u)
#define SIO_FIFO_ST (SIO + 0x50u)
#define SIO_FIFO_WR (SIO + 0x54u)
#define SIO_FIFO_RD (SIO + 0x58u)
#define FIFO_VLD (1u << 0)
#define FIFO_RDY (1u << 1)
#define SIO_SPINLOCK(n) (SIO + 0x100u + 4u * (n))

#define TIMER_ALARM1 (TIMER + 0x14u)
#define IO_PROC1_INTE(n) (IO_BANK0 + 0x130u + 4u * (n))

#define PSM_FRCE_OFF 0x40010004u
#define PSM_PROC1 (1u << 16)

#define NVIC_ISER 0xe000e100u

/* the interrupts the example takes */
#define IRQ_TIMER0 0
#define IRQ_TIMER1 1
#define IRQ_IO_BANK0 13
#define IRQ_SIO_PROC0 15
#define IRQ_SIO_PROC1 16
#define IRQ_UART0 20

#endif
