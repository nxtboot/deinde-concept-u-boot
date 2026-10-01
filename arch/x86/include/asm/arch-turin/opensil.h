/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Glue between U-Boot and AMD's openSIL library
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __ASM_ARCH_OPENSIL_H
#define __ASM_ARCH_OPENSIL_H

/**
 * turin_opensil_init() - Start using openSIL
 *
 * This sends openSIL's messages to U-Boot's log and asks it how much memory
 * it needs, which it reports
 *
 * Return: 0 if OK, -EINVAL if openSIL refuses the debug set-up
 */
int turin_opensil_init(void);

#endif
