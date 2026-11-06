/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#include "mcu/esp32/uart.h"
#include <string.h>
static uint32_t apb_base(unsigned port)
{ return port==0 ? UINT32_C(0x3ff40000) : port==1 ? UINT32_C(0x3ff50000) : UINT32_C(0x3ff6e000); }
static uint32_t ahb_fifo(unsigned port)
{ return port==0 ? UINT32_C(0x60000000) : port==1 ? UINT32_C(0x60010000) : UINT32_C(0x6002e000); }
static unsigned capacity(const esk_esp32_uart *u,int rx)
{ return 128u*((u->memconf>>(rx ? 3u : 7u))&15u); }
static uint8_t *storage(esk_esp32_uart *u,int rx)
{ return u->shared ? &u->shared[128u*(u->port+(rx ? 3u : 0u))] : rx ? u->rx : u->tx; }
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
    if(u->rx_count>((u->conf1&127u)|(((u->memconf>>25)&7u)<<7))) u->raw|=1u;
    if(u->tx_count<(((u->conf1>>8)&127u)|(((u->memconf>>28)&7u)<<7))) u->raw|=2u;
}
static void start(esk_esp32_uart *u)
{
    unsigned n,ones=0;uint32_t data;
    if(u->active || !u->tx_count || u->external_tx_reset || (u->conf0&(1u<<18)) || ((u->conf0&(1u<<15)) && u->cts)) return;
    data=storage(u,0)[u->tx_read];u->tx_read=(u->tx_read+1u)%capacity(u,0);u->tx_count--;
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
static es_status uart_configure(esk_esp32_uart *u,int on,int rxd,int cts,int dsr,int coordinated)
{
    if(!u || u->advancing || !binary(on) || !binary(rxd) || !binary(cts) || !binary(dsr)) return ES_EINVAL;
    if(u->shared && !coordinated) return ES_EUNSUPPORTED;
    esk_esp32_uart_init(u);if(!on) return ES_OK;
    u->enabled=1;u->memconf=0x88;u->conf0=0x0800001c;u->conf1=0x6060;u->divider=0x2b6;
    u->rxd=(uint32_t)rxd;u->cts=(uint32_t)cts;u->dsr=(uint32_t)dsr;update(u);return ES_OK;
}
es_status esk_esp32_uart_configure(esk_esp32_uart *u,int on,int rxd,int cts,int dsr)
{ return uart_configure(u,on,rxd,cts,dsr,0); }
static int reg_offset(const esk_esp32_uart *u,uint32_t address)
{
    uint32_t base=apb_base(u->port);
    if(address>=base && address<base+0x80u) return (int)(address-base);
    if(address==ahb_fifo(u->port)) return 0;
    return -1;
}
es_status esk_esp32_uart_read(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t *value)
{
    uint32_t result;int off;
    if(!u || !value || u->advancing) return ES_EINVAL;
    off=reg_offset(u,address);
    if(!u->enabled || off<0) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    switch(off) {
    case 0:
        if(!u->rx_count) return ES_EUNSUPPORTED;
        result=storage(u,1)[u->rx_read];u->rx_read=(u->rx_read+1u)%capacity(u,1);u->rx_count--;update(u);break;
    case 4: result=u->raw;break;
    case 8: result=u->raw&u->ena;break;
    case 12: result=u->ena;break;
    case 20: result=u->divider;break;
    case 28: result=(u->rx_count&255u)|((u->tx_count&255u)<<16)|(tx_state(u)<<24)|(tx_level(u)<<31)|
        (u->rxd<<15)|(u->cts<<14)|(u->dsr<<13)|((u->conf0&0x40u)<<24)|((u->conf0&0x80u)<<22);break;
    case 32: result=u->conf0;break;
    case 36: result=u->conf1;break;
    case 88: result=u->memconf;break;
    case 100: result=(u->rx_count>>8)|((u->tx_count>>8)<<3);break;
    case 96: result=(u->rx_read<<2)|(u->rx_write<<13);break;
    default: return ES_EUNSUPPORTED;
    }
    *value=result;return ES_OK;
}
static es_status uart_write(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t value,int coordinated)
{
    int off;uint32_t masked;
    if(!u || u->advancing) return ES_EINVAL;
    off=reg_offset(u,address);
    if(!u->enabled || off<0) return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    switch(off) {
    case 0:
        if(u->tx_count==capacity(u,0) || u->external_tx_reset || (u->conf0&(1u<<18))) return ES_EUNSUPPORTED;
        storage(u,0)[u->tx_write]=(uint8_t)(value&255u);u->tx_write=(u->tx_write+1u)%capacity(u,0);u->tx_count++;start(u);break;
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
        if(u->port==2 && (masked&(1u<<18))) return ES_EUNSUPPORTED;
        if(u->port==1 && ((masked|u->conf0)&(1u<<18)) && !coordinated) return ES_EUNSUPPORTED;
        u->conf0=masked;
        if(masked&(1u<<18)) u->tx_read=u->tx_write=u->tx_count=0;
        start(u);break;
    case 36:
        if(value&~CONF1_MASK) return ES_EUNSUPPORTED;
        u->conf1=value;break;
    case 88: {
        unsigned tx_size,rx_size;
        masked=value&UINT32_C(0x7fff87f9);
        if(masked&~UINT32_C(0x7e1c07f8)) return ES_EUNSUPPORTED;
        tx_size=(masked>>7)&15u;rx_size=(masked>>3)&15u;
        if(!tx_size || !rx_size || tx_size>8u-u->port || rx_size>5u-u->port ||
           (!u->shared && (tx_size!=1 || rx_size!=1))) return ES_EUNSUPPORTED;
        if(((masked^u->memconf)&0x780u) && (u->active || u->tx_count)) return ES_EUNSUPPORTED;
        if(((masked^u->memconf)&0x78u) && u->rx_count) return ES_EUNSUPPORTED;
        if((masked^u->memconf)&0x780u) u->tx_read=u->tx_write=0;
        if((masked^u->memconf)&0x78u) u->rx_read=u->rx_write=0;
        u->memconf=masked;break;
    }
    default: return ES_EUNSUPPORTED;
    }
    update(u);return ES_OK;
}
es_status esk_esp32_uart_write(esk_esp32_uart *u,uint32_t address,unsigned bytes,uint32_t value)
{ return uart_write(u,address,bytes,value,0); }
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
        if(u->rx_count==capacity(u,1)) u->raw|=16u;
        else {
            storage(u,1)[u->rx_write]=(uint8_t)(data&((1u<<(5u+((u->conf0>>2)&3u)))-1u));
            u->rx_write=(u->rx_write+1u)%capacity(u,1);u->rx_count++;
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

static int bank_busy(const esk_esp32_uart_bank *b)
{ return b->port[0].advancing || b->port[1].advancing || b->port[2].advancing; }
void esk_esp32_uart_bank_init(esk_esp32_uart_bank *b)
{
    unsigned i;if(!b) return;
    memset(b,0,sizeof *b);
    for(i=0;i<3;i++) { b->port[i].port=i;b->port[i].shared=b->ram; }
}
es_status esk_esp32_uart_bank_configure(esk_esp32_uart_bank *b,unsigned port,int on,int rxd,int cts,int dsr)
{
    es_status st;if(!b || port>2 || bank_busy(b)) return ES_EINVAL;
    st=uart_configure(&b->port[port],on,rxd,cts,dsr,1);
    if(st!=ES_OK) return st;
    b->port[port].port=port;b->port[port].shared=b->ram;
    b->port[2].external_tx_reset=(b->port[1].conf0>>18)&1u;
    start(&b->port[2]);return ES_OK;
}
static int bank_port(uint32_t address)
{
    unsigned i;for(i=0;i<3;i++) if((address>=apb_base(i) && address<apb_base(i)+0x80u) || address==ahb_fifo(i)) return (int)i;
    return -1;
}
es_status esk_esp32_uart_bank_read(esk_esp32_uart_bank *b,uint32_t address,unsigned bytes,uint32_t *value)
{
    int port;if(!b || !value || bank_busy(b)) return ES_EINVAL;
    port=bank_port(address);if(port<0) return ES_EUNSUPPORTED;
    return esk_esp32_uart_read(&b->port[port],address,bytes,value);
}
es_status esk_esp32_uart_bank_write(esk_esp32_uart_bank *b,uint32_t address,unsigned bytes,uint32_t value)
{
    int port;es_status st;unsigned reset;
    if(!b || bank_busy(b)) return ES_EINVAL;
    port=bank_port(address);if(port<0) return ES_EUNSUPPORTED;
    if(b->port[(unsigned)port].enabled && (bytes!=4 || (address&3u))) return ES_EINVAL;
    if(port==1 && address==apb_base(1)+32u) {
        reset=(value>>18)&1u;
        if(reset && b->port[2].active) return ES_EUNSUPPORTED;
        st=uart_write(&b->port[1],address,bytes,value,1);if(st!=ES_OK) return st;
        b->port[2].external_tx_reset=reset;
        if(reset) b->port[2].tx_read=b->port[2].tx_write=b->port[2].tx_count=0;
        else start(&b->port[2]);
        update(&b->port[2]);return ES_OK;
    }
    return uart_write(&b->port[(unsigned)port],address,bytes,value,0);
}
