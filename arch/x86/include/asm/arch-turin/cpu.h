/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_TURIN_CPU_H
#define _ASM_ARCH_TURIN_CPU_H

/**
 * turin_ecam_init() - Move the PCIe ECAM window to where U-Boot can reach it
 *
 * Does nothing if it has already been moved
 */
void turin_ecam_init(void);

#endif
