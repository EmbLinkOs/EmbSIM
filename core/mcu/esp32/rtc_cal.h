/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ESP32_RTC_CAL_H
#define ESK_ESP32_RTC_CAL_H
#include "embsim/types.h"
/* TIMG0 RTC-calibration counter profile, not the timer/watchdog/clock tree.
 * Explicit initial control/value; no oscillator frequency or past measurement
 * is inferred. Each host period supplies the XTAL ticks in one complete period
 * of source 0=SDK RTC slow mux, 1=RC_FAST/256, 2=XTAL32K. Caller supplies mux,
 * gating, aligned full periods and subperiod phase. No CPU-time conversion.
 * One-shot START rising edge clears RDY and accumulates MAX periods, then sets
 * RDY; cycling (MAX=1 only) updates VALUE per period without one-shot RDY. VALUE retains
 * the last completed count. Same-control writes preserve progress. Reconfiguring
 * cycling restarts at the next full supplied period; physical phase/synchronizer
 * latency is unqualified. START clears only by software. Stop preserves VALUE/RDY.
 * Concurrent modes, multi-period cycling, active one-shot retarget/resize,
 * active zero MAX, source 3,
 * zero-tick intervals/25-bit overflow, TIMG1, oscillator startup/drift/electrical behavior and other
 * timer/RTC/power/reset domains remain unsupported. Missed clock input stalls.
 * Calls serialized between machine turns; failed operations preserve state. */
typedef struct esk_esp32_rtc_cal {
    uint32_t enabled,control,value,ready,active,periods,ticks;
} esk_esp32_rtc_cal;
void esk_esp32_rtc_cal_init(esk_esp32_rtc_cal *c);
/* on=1: control contains RW fields only, value is an explicit 25-bit snapshot.
 * on=0 detaches. Snapshot creation is a host fixture, not silicon reset. */
es_status esk_esp32_rtc_cal_configure(esk_esp32_rtc_cal *c,int on,uint32_t control,uint32_t value);
es_status esk_esp32_rtc_cal_read(const esk_esp32_rtc_cal *c,uint32_t address,unsigned bytes,uint32_t *value);
es_status esk_esp32_rtc_cal_write(esk_esp32_rtc_cal *c,uint32_t address,unsigned bytes,uint32_t value);
es_status esk_esp32_rtc_cal_period(esk_esp32_rtc_cal *c,unsigned source,uint32_t xtal_ticks);
#endif
