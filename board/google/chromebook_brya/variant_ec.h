/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * EC settings for brya, from coreboot's brya baseboard. The EC's SCI
 * arrives over eSPI, which has a fixed GPE number on this PCH
 */

#ifndef VARIANT_EC_H
#define VARIANT_EC_H

#include <ec_commands.h>

#define EC_SCI_GPI		110	/* GPE0_ESPI */

/* Enable EC-backed features */
#define EC_ENABLE_LID_SWITCH
#define SIO_EC_ENABLE_PS2K		/* PS/2 keyboard */

#endif
