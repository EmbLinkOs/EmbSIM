/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ESP32 TRM v5.9 GPIO hold / RTC DIG_ISO; pinned ESP-IDF v4.4.7 definitions. */
#include "mcu/esp32/gpio.h"
#include <string.h>
static uint32_t valid(unsigned bank) { return bank ? 255u : UINT32_C(0x0effffff); }
static uint32_t outputs(unsigned bank) { return bank ? 3u : valid(0); }
/* SDK GPIO_HOLD_MASK inverse; bit 13 is package-specific GPIO20, unqualified.
 * These are pad-control bits, not GPIO-number shifts. */
static const unsigned digital_hold_pin[17]={3,1,6,7,8,9,10,11,5,16,17,18,19,40,21,22,23};
static uint32_t merge(uint32_t old,uint32_t live,uint32_t mask)
{ return (old&~mask)|(live&mask); }
static uint32_t projected(const esk_esp32_gpio *g,unsigned bank,const uint32_t live[2],const uint32_t held[2])
{ return merge(live[bank],held[bank],g->held[bank]); }
static void settle_hold(esk_esp32_gpio *g)
{
    unsigned n,bank;uint32_t local[2]={0,0};
    int unhold=(g->iso&0x4000u)!=0,global=(g->iso&0x8000u)!=0 || g->auto_hold;
    for(n=0;n<17;n++) if(digital_hold_pin[n]<40 && (g->dig_hold&(UINT32_C(1)<<n))) {
        unsigned pin=digital_hold_pin[n];local[pin/32]|=UINT32_C(1)<<(pin%32);
    }
    for(bank=0;bank<2;bank++) {
        uint32_t next=unhold ? 0u : (global ? g->route[bank] : local[bank]&g->route[bank]);
        uint32_t rising=next&~g->held[bank];
        g->hold_out[bank]=merge(g->hold_out[bank],g->out[bank],rising);
        g->hold_oe[bank]=merge(g->hold_oe[bank],g->oe[bank],rising);
        g->hold_od[bank]=merge(g->hold_od[bank],g->od[bank],rising);
        g->hold_ie[bank]=merge(g->hold_ie[bank],g->ie[bank],rising);
        g->hold_pu[bank]=merge(g->hold_pu[bank],g->pu[bank],rising);
        g->hold_pd[bank]=merge(g->hold_pd[bank],g->pd[bank],rising);
        g->held[bank]=next;
    }
}
void esk_esp32_gpio_init(esk_esp32_gpio *g)
{
    if(!g) return;
    memset(g,0,sizeof *g);g->iso=UINT32_C(0xaaaa5000);
}
es_status esk_esp32_gpio_configure(esk_esp32_gpio *g,const uint32_t *config,size_t count)
{
    esk_esp32_gpio next;unsigned bank;
    if(!g || (!config && count)) return ES_EINVAL;
    if(!config && !count) { esk_esp32_gpio_init(g);return ES_OK; }
    if(count!=14) return ES_EINVAL;
    esk_esp32_gpio_init(&next);
    for(bank=0;bank<2;bank++) {
        uint32_t route=config[bank];
        if(route&~valid(bank)) return ES_EINVAL;
        if((config[2+bank]|config[4+bank]|config[6+bank]|config[8+bank])&~route) return ES_EINVAL;
        if((config[4+bank]|config[6+bank])&~outputs(bank)) return ES_EUNSUPPORTED;
        next.route[bank]=route;next.ie[bank]=config[2+bank];next.pu[bank]=config[4+bank];next.pd[bank]=config[6+bank];next.od[bank]=config[8+bank];
        if(bank && ((config[10+bank]|config[12+bank])&~UINT32_C(255))) return ES_EINVAL;
        next.out[bank]=config[10+bank];next.oe[bank]=config[12+bank];
    }
    next.enabled=1;*g=next;return ES_OK;
}
static int selected(const esk_esp32_gpio *g,unsigned pin)
{
    return pin<40 && (g->route[pin/32]&(UINT32_C(1)<<(pin%32)));
}
es_status esk_esp32_gpio_input(esk_esp32_gpio *g,unsigned pin,int known,int high)
{
    uint32_t bit;unsigned bank;
    if(!g || pin>=40 || (known!=0 && known!=1) || (high!=0 && high!=1)) return ES_EINVAL;
    if(!g->enabled || !selected(g,pin)) return ES_EUNSUPPORTED;
    bank=pin/32;bit=UINT32_C(1)<<(pin%32);
    if(known) g->known[bank]|=bit;else g->known[bank]&=~bit;
    if(high) g->level[bank]|=bit;else g->level[bank]&=~bit;
    return ES_OK;
}
es_status esk_esp32_gpio_output(const esk_esp32_gpio *g,unsigned pin,esk_esp32_gpio_pad *pad)
{
    unsigned bank;uint32_t bit,out,oe,od;esk_esp32_gpio_pad result;
    if(!g || !pad || pin>=40) return ES_EINVAL;
    if(!g->enabled || !selected(g,pin)) return ES_EUNSUPPORTED;
    bank=pin/32;bit=UINT32_C(1)<<(pin%32);
    out=projected(g,bank,g->out,g->hold_out)&bit;
    oe=projected(g,bank,g->oe,g->hold_oe)&outputs(bank)&bit;
    od=projected(g,bank,g->od,g->hold_od)&bit;
    result.drive=oe && !(od && out) ? (out ? 1 : 0) : -1;
    result.input_enabled=(projected(g,bank,g->ie,g->hold_ie)&bit) ? 1u : 0u;
    result.pull_up=(projected(g,bank,g->pu,g->hold_pu)&bit) ? 1u : 0u;result.pull_down=(projected(g,bank,g->pd,g->hold_pd)&bit) ? 1u : 0u;
    *pad=result;return ES_OK;
}
es_status esk_esp32_gpio_auto_hold_event(esk_esp32_gpio *g)
{
    if(!g) return ES_EINVAL;
    if(!g->enabled) return ES_EUNSUPPORTED;
    if(g->iso&0x800u) g->auto_hold=1;
    settle_hold(g);return ES_OK;
}
static es_status decode(const esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t *offset)
{
    if(!g->enabled) return ES_EUNSUPPORTED;
    if(address>=0x3ff44000 && address<0x3ff45000) *offset=address-0x3ff44000;
    else if(address>=0x3ff48474 && address<0x3ff48478) *offset=0x1004;
    else if(address>=0x3ff48088 && address<0x3ff4808c) *offset=0x1000;
    else return ES_EUNSUPPORTED;
    if(bytes!=4 || (address&3u)) return ES_EINVAL;
    return ES_OK;
}
es_status esk_esp32_gpio_read(const esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t *value)
{
    uint32_t off;es_status st;unsigned pin,bank;
    if(!g || !value) return ES_EINVAL;
    st=decode(g,address,bytes,&off);if(st!=ES_OK) return st;
    if(off==0x1004) { *value=g->dig_hold;return ES_OK; }
    if(off==0x1000) { *value=g->iso|(g->auto_hold<<9);return ES_OK; }
    if(off>=0x530 && off<=0x5cc) {
        pin=(off-0x530)/4;if(!selected(g,pin)) return ES_EUNSUPPORTED;
        *value=256;return ES_OK;
    }
    if(off>=0x88 && off<=0x124) {
        pin=(off-0x88)/4;if(!selected(g,pin)) return ES_EUNSUPPORTED;
        *value=((g->od[pin/32]>>(pin%32))&1u)<<2;return ES_OK;
    }
    switch(off) {
    case 4:*value=g->out[0];break;case 0x10:*value=g->out[1];break;
    case 0x20:*value=g->oe[0];break;case 0x2c:*value=g->oe[1];break;
    case 0x3c:case 0x40:
        bank=off==0x40 ? 1u : 0u;
        if(projected(g,bank,g->ie,g->hold_ie)&~g->known[bank]) return ES_EUNSUPPORTED;
        *value=g->level[bank]&projected(g,bank,g->ie,g->hold_ie);break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
es_status esk_esp32_gpio_write(esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t value)
{
    uint32_t off,*reg,mask;unsigned bank,operation,pin;es_status st;
    if(!g) return ES_EINVAL;
    st=decode(g,address,bytes,&off);if(st!=ES_OK) return st;
    if(off==0x1004) {
        if(value&~UINT32_C(0x1dfff)) return ES_EUNSUPPORTED;
        g->dig_hold=value;settle_hold(g);return ES_OK;
    }
    if(off==0x1000) {
        /* Do not accept memory/Wi-Fi/power/pad-isolation changes without their
         * domain model. Reserved writes/RO status have no effect. */
        if((value^g->iso)&UINT32_C(0xffff3180)) return ES_EUNSUPPORTED;
        if((value&0xc000u)==0xc000u) return ES_EUNSUPPORTED;
        g->iso=(g->iso&UINT32_C(0xffff3180))|(value&0xc800u);
        if(value&0x400u) g->auto_hold=0;
        settle_hold(g);return ES_OK;
    }
    if(off>=0x530 && off<=0x5cc) {
        pin=(off-0x530)/4;if(!selected(g,pin) || value!=256) return ES_EUNSUPPORTED;
        return ES_OK; /* already selected noninverted software-GPIO fixture */
    }
    if(off>=0x88 && off<=0x124) {
        pin=(off-0x88)/4;if(!selected(g,pin)) return ES_EUNSUPPORTED;
        if(value&~UINT32_C(4)) return ES_EUNSUPPORTED;
        mask=UINT32_C(1)<<(pin%32);bank=pin/32;
        if(value&4u) g->od[bank]|=mask;else g->od[bank]&=~mask;
        return ES_OK;
    }
    if(off>=4 && off<=0x18) { bank=off>=0x10 ? 1u : 0u;operation=(off-(bank ? 0x10u : 4u))/4;reg=&g->out[bank]; }
    else if(off>=0x20 && off<=0x34) { bank=off>=0x2c ? 1u : 0u;operation=(off-(bank ? 0x2cu : 0x20u))/4;reg=&g->oe[bank]; }
    else if(off==0x3c || off==0x40) return ES_OK; /* input registers are RO */
    else return ES_EUNSUPPORTED;
    value&=bank ? 255u : UINT32_MAX;
    if(operation==0) *reg=value;else if(operation==1) *reg|=value;else *reg&=~value;
    return ES_OK;
}
