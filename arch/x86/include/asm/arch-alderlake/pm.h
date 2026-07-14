/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Power-management definitions, from coreboot's
 * soc/intel/alderlake/include/soc/pm.h and pmc.h. The PM1 and GPE0
 * offsets within the ACPI I/O space come from the generic headers,
 * except that this SoC's GPE0 registers are at 0x60 (see acpi.c)
 */

#ifndef _ASM_ARCH_PM_H
#define _ASM_ARCH_PM_H

/*
 * The SCI IRQ-select field in the PMC's ACTL register, as returned by
 * arch_read_sci_irq_select()
 */
#define SCI_IRQ_SHIFT		0
#define SCI_IRQ_MASK		(7 << SCI_IRQ_SHIFT)
#define SCIS_IRQ9		0
#define SCIS_IRQ10		1
#define SCIS_IRQ11		2
#define SCIS_IRQ20		4
#define SCIS_IRQ21		5
#define SCIS_IRQ22		6
#define SCIS_IRQ23		7

/* P-state tables */
#define PSS_RATIO_STEP		2
#define PSS_MAX_ENTRIES		8
#define PSS_LATENCY_TRANSITION	10
#define PSS_LATENCY_BUSMASTER	10

#endif /* _ASM_ARCH_PM_H */
