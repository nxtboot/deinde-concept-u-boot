/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Memory-mapped and I/O addresses, following coreboot's
 * soc/intel/alderlake/include/soc/iomap.h
 */

#ifndef _ASM_ARCH_IOMAP_H
#define _ASM_ARCH_IOMAP_H

/* The PMC's ACPI I/O ports */
#define IOMAP_ACPI_BASE		0x1800
#define IOMAP_ACPI_SIZE		0x100
#define ACPI_BASE_ADDRESS	IOMAP_ACPI_BASE

/* The PMC's power-management MMIO registers (PWRM) */
#define PCH_PWRM_BASE_ADDRESS	0xfe000000

#define IOMAP_SPI_BASE		0xfe010000

#define MCH_BASE_ADDRESS	0xfedc0000
#define MCH_BASE_SIZE		0x20000

#ifdef __ACPI__
#define HPET_BASE_ADDRESS	0xfed00000
#endif

#endif /* _ASM_ARCH_IOMAP_H */
