/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_UART_H
#define ESK_ESP32_UART_H
#include "embsim/types.h"
/* Opt-in normal asynchronous UART profile, 128-byte FIFOs by default, empty on
 * explicit host attach. External inputs RXD/CTS/DSR are required, not pin defaults.
 * TX uses externally advanced selected source-clock ticks, with exact rational
 * divider accounting (1/32 source-cycle units). No CPU-cycle/wall-time inference.
 * Framing/divider configuration is immutable during a TX frame; unsupported changes
 * fail unchanged. 5..8 data bits, parity, 1/1.5 stop supported. STOP=2 workaround,
 * IRDA, RS485, autobaud, DMA, loopback, inversion, RX flow/software flow, break,
 * idle insertion, power/gating/reset domains and physical pin binding
 * are absent. RX supplies completed decoded frames/errors, not sampled edges;
 * RX timeout is an explicit external qualified event, not an invented duration.
 * RX FIFO reset is deliberately unsupported: SDK documents a silicon issue and
 * drains through APB instead. TX FIFO reset only while serializer idle.
 * FIFO aliases APB/AHB 32-bit; empty read/full write fail unchanged rather than
 * invent undefined bytes/drops. Unknown MMIO/WO reads fail. Bank IRQ sources 34/35/36.
 * Serialized calls only; callbacks must not re-enter. NULL emit explicitly
 * discards completed frames. Hold/pad voltage and USB bridge are not modeled. */
typedef struct esk_esp32_uart_frame {
    uint32_t data,bits,parity_enabled,parity,stop_half_bits,divider16,apb;
} esk_esp32_uart_frame;
/* Completion offset within the current advance call, in 1/32 source-cycle units. */
typedef void (*esk_esp32_uart_emit)(void *ctx,const esk_esp32_uart_frame *frame,uint64_t offset32);
typedef struct esk_esp32_uart {
    uint32_t enabled,conf0,conf1,divider,ena,raw,rxd,cts,dsr,memconf;
    unsigned port,external_tx_reset;uint8_t *shared;
    uint8_t tx[128],rx[128];unsigned tx_read,tx_write,tx_count,rx_read,rx_write,rx_count;
    unsigned active,phase,half_bits,advancing;uint64_t remaining;
    esk_esp32_uart_frame frame;
} esk_esp32_uart;
/* UART0/1/2 coordinator: default TX bases 0/128/256, RX bases 384/512/640.
 * A shared byte array preserves overlap writes. Only nonwrapping allocations
 * within 1024 bytes are qualified; zero sizes, live resize, RAM power-down and
 * shared-RAM arbitration/latency remain unsupported. UART1 TX reset affects
 * UART2 only with both serializers idle; active-reset cases fail unchanged.
 * UART2 has no FIFO reset; RX reset remains unsupported on every port.
 * Public per-port write cannot bypass coordinated UART1 reset handling. */
typedef struct esk_esp32_uart_bank { esk_esp32_uart port[3];uint8_t ram[1024]; } esk_esp32_uart_bank;
void esk_esp32_uart_bank_init(esk_esp32_uart_bank *b);
es_status esk_esp32_uart_bank_configure(esk_esp32_uart_bank *b,unsigned port,int on,int rxd,int cts,int dsr);
es_status esk_esp32_uart_bank_read(esk_esp32_uart_bank *b,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_uart_bank_write(esk_esp32_uart_bank *b,uint32_t address,unsigned bytes,uint32_t value);
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
