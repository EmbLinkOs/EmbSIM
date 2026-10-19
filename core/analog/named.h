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
/* a PTC fuse's look at its temperatures (its component's wake) */
void esk_ptc_on_time(esk_adev *d, es_time_t now);
#endif
