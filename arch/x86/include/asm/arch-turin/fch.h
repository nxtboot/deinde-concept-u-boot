/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD Turin FCH (Fusion Controller Hub) fixed ACPI hardware
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_TURIN_FCH_H
#define _ASM_ARCH_TURIN_FCH_H

/* I/O ports of the FCH's ACPI blocks, as U-Boot programs them (PMx60-68) */
#define ACPI_PM1_EVT		0x400
#define ACPI_PM1_CNT		0x404
#define  PM1_CNT_SCI_EN		BIT(0)
#define ACPI_PM_TMR		0x408
#define ACPI_CPU_CNT		0x410
#define ACPI_GPE0		0x420

/* The SCI is level-triggered, active low, on ISA IRQ 9 */
#define ACPI_SCI_IRQ		9

#endif
