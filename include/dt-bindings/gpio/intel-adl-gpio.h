/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Alder Lake GPIO constants for use in the devicetree. The pad numbers
 * must match asm/arch-alderlake/gpio.h; only the pads which the
 * devicetree needs are provided here, since this file must build on all
 * x86 boards (every x86 devicetree is compiled for every board)
 */

#ifndef _DT_BINDINGS_INTEL_ADL_GPIO_H
#define _DT_BINDINGS_INTEL_ADL_GPIO_H

/* Sideband port ID of GPIO community 0 (GPP_B, T, A) */
#define PID_GPIOCOM0	0x6e

/* Sideband port ID of GPIO community 4 (GPP_C, F, E) */
#define PID_GPIOCOM4	0x6a

/* Pads in community 4 used for the DRAM-ID straps */
#define GPP_E1		351
#define GPP_E2		352
#define GPP_E11		361
#define GPP_E12		362

#endif
