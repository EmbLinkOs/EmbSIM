/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/esp32/uart.h"
#include <string.h>
#define BASE UINT32_C(0x3ff40000)
#define CONF0_MASK UINT32_C(0x0e0480ff)
#define CONF1_MASK UINT32_C(0xff007f7f)
#define IRQ_MASK UINT32_C(0x7ffff)
static int binary(int n) { return n==0 || n==1; }
static uint32_t divider16(const esk_esp32_uart *u)
{ return ((u->divider&0xfffffu)<<4)|((u->divider>>20)&15u); }
static void update(esk_esp32_uart *u)
{
    /* Threshold conditions reassert after clearing while still true. Other
     * raw bits are events retained until W1C, independent of enable. */
    if(u->rx_count>(u->conf1&127u)) u->raw|=1u;
    if(u->tx_count<((u->conf1>>8)&127u)) u->raw|=2u;
}
static void start(esk_esp32_uart *u)
{
    unsigned n,ones=0;uint32_t data;
    if(u->active || !u->tx_count || (u->conf0&(1u<<18)) || ((u->conf0&(1u<<15)) && u->cts)) return;
    data=u->tx[u->tx_read];u->tx_read=(u->tx_read+1u)%128u;u->tx_count--;
    u->frame.bits=5u+((u->conf0>>2)&3u);data&=(1u<<u->frame.bits)-1u;
    for(n=0;n<u->frame.bits;n++) ones+=(data>>n)&1u;
    u->frame.data=data;u->frame.parity_enabled=(u->conf0>>1)&1u;
    u->frame.parity=(ones&1u)^(u->conf0&1u);u->frame.stop_half_bits=((u->conf0>>4)&3u)==1u ? 2u : 3u;
    u->frame.divider16=divider16(u);u->frame.apb=(u->conf0>>27)&1u;
    u->half_bits=2u+2u*(u->frame.bits+u->frame.parity_enabled)+u->frame.stop_half_bits;
    u->phase=0;u->active=1;u->remaining=u->frame.divider16;update(u);
}
static unsigned tx_state(const esk_esp32_uart *u)
{
    unsigned bit=u->phase/2u;
    if(!u->active) return 0;
    if(!bit) return 1;
    if(bit<=u->frame.bits) return bit+1u;
    if(u->frame.parity_enabled && bit==u->frame.bits+1u) return 10;
    return u->phase>=4u+2u*(u->frame.bits+u->frame.parity_enabled) ? 12u : 11u;
}
static unsigned tx_level(const esk_esp32_uart *u)
{
    unsigned bit=u->phase/2u;
    if(!u->active) return 1;
    if(!bit) return 0;
    if(bit<=u->frame.bits) return (u->frame.data>>(bit-1u))&1u;
    if(u->frame.parity_enabled && bit==u->frame.bits+1u) return u->frame.parity;
    return 1;
}
void esk_esp32_uart_init(esk_esp32_uart *u) { if(u) memset(u,0,sizeof *u); }
es_status esk_esp32_uart_configure(esk_esp32_uart *u,int on,int rxd,int cts,int dsr)
{
    if(!u || u->advancing || !binary(on) || !binary(rxd) || !binary(cts) || !binary(dsr)) return ES_EINVAL;
    esk_esp32_uart_init(u);if(!on) return ES_OK;
    u->enabled=1;u->conf0=0x0800001c;u->conf1=0x6060;u->divider=0x2b6;
    u->rxd=(uint32_t)rxd;u->cts=(uint32_t)cts;u->dsr=(uint32_t)dsr;update(u);return ES_OK;
}
static int reg_offset(uint32_t address)
{
    if(address>=BASE && address<BASE+0x80u) return (int)(address-BASE);
    if(address==UINT32_C(0x60000000)) return 0;
    return -1;
}
es_status esk_esp32_uart_read(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t *value)
{
    uint32_t result;int off=reg_offset(address);
    if(!u || !value || u->advancing) return ES_EINVAL;
    if(!u->enabled || off<0) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    switch(off) {
    case 0:
        if(!u->rx_count) return ES_EUNSUPPORTED;
        result=u->rx[u->rx_read];u->rx_read=(u->rx_read+1u)%128u;u->rx_count--;update(u);break;
    case 4: result=u->raw;break;
    case 8: result=u->raw&u->ena;break;
    case 12: result=u->ena;break;
    case 20: result=u->divider;break;
    case 28: result=u->rx_count|(u->tx_count<<16)|(tx_state(u)<<24)|(tx_level(u)<<31)|
        (u->rxd<<15)|(u->cts<<14)|(u->dsr<<13)|((u->conf0&0x40u)<<24)|((u->conf0&0x80u)<<22);break;
    case 32: result=u->conf0;break;
    case 36: result=u->conf1;break;
    case 96: result=(u->rx_read<<2)|(u->rx_write<<13);break;
    default: return ES_EUNSUPPORTED;
    }
    *value=result;return ES_OK;
}
es_status esk_esp32_uart_write(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t value)
{
    int off=reg_offset(address);uint32_t masked;
    if(!u || u->advancing) return ES_EINVAL;
    if(!u->enabled || off<0) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    switch(off) {
    case 0:
        if(u->tx_count==128u || (u->conf0&(1u<<18))) return ES_EUNSUPPORTED;
        u->tx[u->tx_write]=(uint8_t)(value&255u);u->tx_write=(u->tx_write+1u)%128u;u->tx_count++;start(u);break;
    case 12: u->ena=value&IRQ_MASK;break;
    case 16: u->raw&=~(value&IRQ_MASK);break;
    case 20:
        masked=value&0xffffffu;
        if(!(masked&0xfffffu) || (u->active && masked!=u->divider)) return ES_EUNSUPPORTED;
        u->divider=masked;break;
    case 32:
        masked=value&0xfffffffu;
        if((masked&~CONF0_MASK) || ((masked>>4)&3u)==0u || ((masked>>4)&3u)==3u ||
           (u->active && masked!=u->conf0)) return ES_EUNSUPPORTED;
        u->conf0=masked;
        if(masked&(1u<<18)) u->tx_read=u->tx_write=u->tx_count=0;
        start(u);break;
    case 36:
        if(value&~CONF1_MASK) return ES_EUNSUPPORTED;
        u->conf1=value;break;
    default: return ES_EUNSUPPORTED;
    }
    update(u);return ES_OK;
}
es_status esk_esp32_uart_inputs(esk_esp32_uart *u,int rxd,int cts,int dsr)
{
    if(!u || u->advancing || !binary(rxd) || !binary(cts) || !binary(dsr)) return ES_EINVAL;
    if(!u->enabled) return ES_EUNSUPPORTED;
    if(u->cts!=(uint32_t)cts) u->raw|=64u;
    if(u->dsr!=(uint32_t)dsr) u->raw|=32u;
    u->rxd=(uint32_t)rxd;u->cts=(uint32_t)cts;u->dsr=(uint32_t)dsr;start(u);update(u);return ES_OK;
}
es_status esk_esp32_uart_receive(esk_esp32_uart *u,uint32_t data,unsigned flags)
{
    if(!u || u->advancing || data>255u || flags>3u) return ES_EINVAL;
    if(!u->enabled) return ES_EUNSUPPORTED;
    if((flags&1u) && !(u->conf0&2u)) return ES_EINVAL;
    u->raw|=(flags&1u)*4u+(flags&2u)*4u;
    if(!flags || !(u->conf0&(1u<<26))) {
        if(u->rx_count==128u) u->raw|=16u;
        else {
            u->rx[u->rx_write]=(uint8_t)(data&((1u<<(5u+((u->conf0>>2)&3u)))-1u));
            u->rx_write=(u->rx_write+1u)%128u;u->rx_count++;
        }
    }
    update(u);return ES_OK;
}
es_status esk_esp32_uart_timeout(esk_esp32_uart *u)
{
    if(!u || u->advancing) return ES_EINVAL;
    if(!u->enabled || !(u->conf1&UINT32_C(0x80000000)) || !u->rx_count) return ES_EUNSUPPORTED;
    u->raw|=256u;return ES_OK;
}
es_status esk_esp32_uart_advance(esk_esp32_uart *u,uint64_t ticks,esk_esp32_uart_emit emit,void *ctx)
{
    uint64_t units;
    if(!u || u->advancing || ticks>UINT64_MAX/32u) return ES_EINVAL;
    if(!u->enabled) return ES_EUNSUPPORTED;
    units=ticks*32u;u->advancing=1;
    while(u->active && units>=u->remaining) {
        units-=u->remaining;u->phase++;
        if(u->phase==u->half_bits) {
            u->active=0;u->remaining=0;if(emit) emit(ctx,&u->frame,ticks*32u-units);
            if(!u->tx_count) u->raw|=UINT32_C(0x4000);
            start(u);
        } else u->remaining=u->frame.divider16;
    }
    if(u->active) u->remaining-=units;
    u->advancing=0;update(u);return ES_OK;
}
es_status esk_esp32_uart_irq(const esk_esp32_uart *u,int *asserted)
{
    if(!u || !asserted) return ES_EINVAL;
    if(!u->enabled) return ES_EUNSUPPORTED;
    *asserted=(u->raw&u->ena)!=0;return ES_OK;
}
