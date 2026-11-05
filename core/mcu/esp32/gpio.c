/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ESP32 TRM v5.9 GPIO hold / RTC DIG_ISO; pinned ESP-IDF v4.4.7 definitions. */
#include "mcu/esp32/gpio.h"
#include <string.h>
static uint32_t valid(unsigned bank) { return bank ? 255u : UINT32_C(0x0effffff); }
static uint32_t outputs(unsigned bank) { return bank ? 3u : valid(0); }
static void settle_hold(esk_esp32_gpio *g)
{
    uint32_t hold=(!(g->iso&0x4000u) && ((g->iso&0x8000u) || g->auto_hold)) ? 1u : 0u;
    if(hold && !g->held) {
        memcpy(g->hold_out,g->out,sizeof g->out);memcpy(g->hold_oe,g->oe,sizeof g->oe);memcpy(g->hold_od,g->od,sizeof g->od);
    }
    g->held=hold;
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
    out=(g->held ? g->hold_out[bank] : g->out[bank])&bit;
    oe=(g->held ? g->hold_oe[bank] : g->oe[bank])&outputs(bank)&bit;
    od=(g->held ? g->hold_od[bank] : g->od[bank])&bit;
    result.drive=oe && !(od && out) ? (out ? 1 : 0) : -1;
    result.input_enabled=(g->ie[bank]&bit) ? 1u : 0u;result.pull_up=(g->pu[bank]&bit) ? 1u : 0u;result.pull_down=(g->pd[bank]&bit) ? 1u : 0u;
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
        if(g->ie[bank]&~g->known[bank]) return ES_EUNSUPPORTED;
        *value=g->level[bank]&g->ie[bank];break;
    default:return ES_EUNSUPPORTED;
    }
    return ES_OK;
}
es_status esk_esp32_gpio_write(esk_esp32_gpio *g,uint32_t address,unsigned bytes,uint32_t value)
{
    uint32_t off,*reg,mask;unsigned bank,operation,pin;es_status st;
    if(!g) return ES_EINVAL;
    st=decode(g,address,bytes,&off);if(st!=ES_OK) return st;
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
