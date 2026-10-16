/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* nrfx_config.h -- the drivers nrfx_tour uses, as an nrfx integration's
 * configuration enables them; the rest are the SoC template's defaults. */
#ifndef NRFX_CONFIG_H__
#define NRFX_CONFIG_H__

#define NRFX_CLOCK_ENABLED 1
#define NRFX_CLOCK_CONFIG_LF_SRC 1 /* the 32.768 kHz crystal */
#define NRFX_GPIOTE_ENABLED 1
#define NRFX_GPIOTE0_ENABLED 1
#define NRFX_TIMER_ENABLED 1
#define NRFX_TIMER1_ENABLED 1
#define NRFX_RTC_ENABLED 1
#define NRFX_RTC1_ENABLED 1
#define NRFX_UARTE_ENABLED 1
#define NRFX_UARTE0_ENABLED 1

#include "nrfx_config_common.h"
#include "nrfx_templates_config.h"

#endif
