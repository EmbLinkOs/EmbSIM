/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
#ifndef ESK_ANALOG_NAMED_H
#define ESK_ANALOG_NAMED_H
#include "analog/analog.h"
extern const esk_adev_ops esk_comparator_ops;
extern const esk_adev_ops esk_temperature_ops;
extern const esk_adev_ops esk_timer555_ops;
extern const esk_adev_ops esk_environmental_ops;
extern const esk_adev_ops esk_photodiode_ops;
extern const esk_adev_ops esk_optocoupler_ops;
extern const esk_adev_ops esk_relay_contacts_ops;
extern const esk_adev_ops esk_spdt_ops;
extern const esk_adev_ops esk_button4_ops;
extern const esk_adev_ops esk_ptc_fuse_ops;
/* an inline USB power meter (meter.c) */
extern const esk_adev_ops esk_usb_meter_ops;
/* a part's junction and package temperatures (thermal.c, ADR-034) */
typedef struct esk_thermal2 {
    double tj, ts;       /* the junction's and the solder point's (case's) temperatures, C */
    double rjs, rsa;     /* junction to solder point, solder point to room, K/W */
    double cj, cs;       /* heat capacities, J/K */
} esk_thermal2;
void esk_thermal2_init(esk_thermal2 *th, double ta, double rjs, double rja, double cj, double cs);
void esk_thermal2_advance(esk_thermal2 *th, double p, double ta, double dt);
double esk_thermal2_steady(const esk_thermal2 *th, double p, double ta);
/* seconds until the junction passes `limit` under constant p, or -1 never */
double esk_thermal2_cross(const esk_thermal2 *th, double p, double ta, double limit);
extern const esk_adev_ops esk_multimeter_ops;
void esk_multimeter_on_time(esk_adev *d, es_time_t now);
void esk_multimeter_on_input(esk_adev *d, const char *input, es_time_t now);
void esk_usb_meter_on_time(esk_adev *d, es_time_t now);
/* the PPK2's measurement core (power_profiler.c): its samples since `since`, and its sample ring freed */
extern const esk_adev_ops esk_power_profiler_ops;
es_status esk_power_profiler_read(esk_adev *d, uint64_t since, size_t max, esk_json *out);
void esk_power_profiler_free(esk_adev *d);
double esk_ppk2_quantize(double i);
/* the DS1054Z oscilloscope (oscilloscope.c): its last acquisition, its wake, its memory freed */
extern const esk_adev_ops esk_oscilloscope_ops;
es_status esk_oscilloscope_read(esk_adev *d, uint64_t since, esk_json *out);
void esk_oscilloscope_on_time(esk_adev *d, es_time_t now);
void esk_oscilloscope_on_input(esk_adev *d, es_time_t now);
void esk_power_profiler_on_input(esk_adev *d, es_time_t now);
void esk_oscilloscope_free(esk_adev *d);
/* a PTC fuse's look at its temperatures (its component's wake) */
void esk_ptc_on_time(esk_adev *d, es_time_t now);
void esk_environmental_on_time(esk_adev *d, es_time_t now);
void esk_environmental_on_input(esk_adev *d, es_time_t now);
#endif
