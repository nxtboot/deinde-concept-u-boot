/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * System-agent registers, following coreboot's
 * soc/intel/alderlake/include/soc/systemagent.h
 */

#ifndef _ASM_ARCH_SYSTEMAGENT_H
#define _ASM_ARCH_SYSTEMAGENT_H

/* Device 0:0.0 PCI configuration space */
#define MCHBAR		0x48

/* Registers within the MCHBAR MMIO space */
#define BIOS_RESET_CPL	0x5da8

#endif /* _ASM_ARCH_SYSTEMAGENT_H */
