/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_UART_H
#define ESK_ESP32_UART_H
#include "embsim/types.h"
/* Opt-in UART0 normal asynchronous profile, fixed 128-byte FIFOs, empty on
 * explicit host attach. External inputs RXD/CTS/DSR are required, not pin defaults.
 * TX uses externally advanced selected source-clock ticks, with exact rational
 * divider accounting (1/32 source-cycle units). No CPU-cycle/wall-time inference.
 * Framing/divider configuration is immutable during a TX frame; unsupported changes
 * fail unchanged. 5..8 data bits, parity, 1/1.5 stop supported. STOP=2 workaround,
 * IRDA, RS485, autobaud, DMA, loopback, inversion, RX flow/software flow, break,
 * idle insertion, power/gating/reset domains, UART1/2 and physical pin binding
 * are absent. RX supplies completed decoded frames/errors, not sampled edges;
 * RX timeout is an explicit external qualified event, not an invented duration.
 * RX FIFO reset is deliberately unsupported: SDK documents a silicon issue and
 * drains through APB instead. TX FIFO reset only while serializer idle.
 * FIFO aliases APB/AHB 32-bit; empty read/full write fail unchanged rather than
 * invent undefined bytes/drops. Unknown MMIO/WO reads fail. IRQ source 34.
 * Serialized calls only; callbacks must not re-enter. NULL emit explicitly
 * discards completed frames. Hold/pad voltage and USB bridge are not modeled. */
typedef struct esk_esp32_uart_frame {
    uint32_t data,bits,parity_enabled,parity,stop_half_bits,divider16,apb;
} esk_esp32_uart_frame;
/* Completion offset within the current advance call, in 1/32 source-cycle units. */
typedef void (*esk_esp32_uart_emit)(void *ctx,const esk_esp32_uart_frame *frame,uint64_t offset32);
typedef struct esk_esp32_uart {
    uint32_t enabled,conf0,conf1,divider,ena,raw,rxd,cts,dsr;
    uint8_t tx[128],rx[128];unsigned tx_read,tx_write,tx_count,rx_read,rx_write,rx_count;
    unsigned active,phase,half_bits,advancing;uint64_t remaining;
    esk_esp32_uart_frame frame;
} esk_esp32_uart;
void esk_esp32_uart_init(esk_esp32_uart *u);
/* Initialize the standalone object before its first configuration.
 * on=0 detach; on=1 attach/reset profile with explicit binary input levels. */
es_status esk_esp32_uart_configure(esk_esp32_uart *u,int on,int rxd,int cts,int dsr);
es_status esk_esp32_uart_read(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_uart_write(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t value);
es_status esk_esp32_uart_inputs(esk_esp32_uart *u,int rxd,int cts,int dsr);
/* flags: bit0 parity error, bit1 frame error. Data is decoded before UART masking.
 * Overflow latches RXFIFO_OVF and discards incoming data as specified by hardware. */
es_status esk_esp32_uart_receive(esk_esp32_uart *u,uint32_t data,unsigned flags);
es_status esk_esp32_uart_timeout(esk_esp32_uart *u);
es_status esk_esp32_uart_advance(esk_esp32_uart *u,uint64_t ticks,esk_esp32_uart_emit emit,void *ctx);
es_status esk_esp32_uart_irq(const esk_esp32_uart *u,int *asserted);
#endif
