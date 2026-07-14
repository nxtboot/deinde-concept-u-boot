// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Alder Lake CPU driver, providing the CPU information used by the ACPI
 * tables (the MADT local-APIC entries and the processor count). The
 * SoC-specific core setup which coreboot does can be added here as the
 * port progresses
 */

#include <cpu.h>
#include <dm.h>
#include <asm/cpu_x86.h>

static const struct cpu_ops cpu_x86_adl_ops = {
	.get_desc	= cpu_x86_get_desc,
	.get_count	= cpu_x86_get_count,
	.get_vendor	= cpu_x86_get_vendor,
};

static const struct udevice_id cpu_x86_adl_ids[] = {
	{ .compatible = "intel,alderlake-cpu" },
	{ }
};

U_BOOT_DRIVER(intel_adl_cpu) = {
	.name		= "intel_adl_cpu",
	.id		= UCLASS_CPU,
	.of_match	= cpu_x86_adl_ids,
	.bind		= cpu_x86_bind,
	.ops		= &cpu_x86_adl_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
