/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* ESP32 TRM v5.9 TIMG registers and ESP-IDF v4.4.7 rtc_time.c protocol.
 * Original implementation. External complete-period inputs, no guessed clocks. */
#include "mcu/esp32/rtc_cal.h"
#include <string.h>
#define CONTROL UINT32_C(0x3ff5f068)
#define RESULT UINT32_C(0x3ff5f06c)
#define RW UINT32_C(0xffff7000)
#define START UINT32_C(0x80000000)
#define CYCLE UINT32_C(0x1000)
#define LIMIT UINT32_C(0x1ffffff)
static int valid_control(uint32_t value)
{
    uint32_t mode=value&(START|CYCLE);
    return ((value>>13)&3u)!=3u && mode!=(START|CYCLE) &&
        (!mode || ((value>>16)&0x7fffu)) && (!(value&CYCLE) || ((value>>16)&0x7fffu)==1u);
}
void esk_esp32_rtc_cal_init(esk_esp32_rtc_cal *c) { if(c) memset(c,0,sizeof *c); }
es_status esk_esp32_rtc_cal_configure(esk_esp32_rtc_cal *c,int on,uint32_t control,uint32_t value)
{
    if(!c || (on!=0 && on!=1)) return ES_EINVAL;
    if(on && ((control&~RW) || value>LIMIT || !valid_control(control))) return ES_EUNSUPPORTED;
    esk_esp32_rtc_cal_init(c);if(!on) return ES_OK;
    c->enabled=1;c->control=control;c->value=value;c->active=(control&(START|CYCLE)) ? 1u : 0u;return ES_OK;
}
static es_status access_ok(const esk_esp32_rtc_cal *c,uint32_t address,unsigned bytes)
{
    if(!c) return ES_EINVAL;
    if(!c->enabled || address<CONTROL || address>=RESULT+4u) return ES_EUNSUPPORTED;
    return bytes==4 && !(address&3u) ? ES_OK : ES_EINVAL;
}
es_status esk_esp32_rtc_cal_read(const esk_esp32_rtc_cal *c,uint32_t address,unsigned bytes,uint32_t *value)
{
    es_status st;if(!value) return ES_EINVAL;
    st=access_ok(c,address,bytes);if(st!=ES_OK) return st;
    *value=address==CONTROL ? c->control|(c->ready<<15) : c->value<<7;return ES_OK;
}
es_status esk_esp32_rtc_cal_write(esk_esp32_rtc_cal *c,uint32_t address,unsigned bytes,uint32_t value)
{
    es_status st=access_ok(c,address,bytes);uint32_t next=value&RW,previous;
    if(st!=ES_OK) return st;
    if(address==RESULT) return ES_OK; /* Known read-only result. */
    if(!valid_control(next)) return ES_EUNSUPPORTED;
    if(c->active && (c->control&START) && (next&START) && ((next^c->control)&UINT32_C(0x7fff6000))) return ES_EUNSUPPORTED;
    previous=c->control;if(previous==next) return ES_OK;
    c->control=next;
    if(!(next&(START|CYCLE))) { c->active=0;c->periods=c->ticks=0; }
    else if((next&CYCLE) || !(previous&START)) {
        c->active=1;c->ready=0;c->periods=c->ticks=0;
    }
    return ES_OK;
}
es_status esk_esp32_rtc_cal_period(esk_esp32_rtc_cal *c,unsigned source,uint32_t xtal_ticks)
{
    uint64_t sum;
    if(!c || source>2) return ES_EINVAL;
    if(!c->enabled) return ES_EUNSUPPORTED;
    if(!c->active || source!=((c->control>>13)&3u)) return ES_OK;
    if(!xtal_ticks) return ES_EUNSUPPORTED;
    sum=(uint64_t)c->ticks+xtal_ticks;if(sum>LIMIT) return ES_EUNSUPPORTED;
    c->ticks=(uint32_t)sum;c->periods++;
    if(c->periods==((c->control>>16)&0x7fffu)) {
        c->value=c->ticks;c->periods=c->ticks=0;
        if(c->control&START) { c->ready=1;c->active=0; }
    }
    return ES_OK;
}
