// SPDX-License-Identifier: GPL-2.0+
/*
 * CPU driver for AMD EPYC Turin, which describes the CPUs to ACPI
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_CPU

#include <cpu.h>
#include <dm.h>
#include <log.h>
#include <acpi/acpigen.h>
#include <acpi/acpi_table.h>
#include <asm/cpu_x86.h>
#include <asm/arch/cpu.h>
#include <dm/acpi.h>

/* Collaborative Processor Performance Control (CPPC) MSRs */
#define MSR_CPPC_CAPABILITY_1	0xc00102b0
#define MSR_CPPC_ENABLE		0xc00102b1
#define MSR_CPPC_REQUEST	0xc00102b3
#define MSR_CPPC_STATUS		0xc00102b4
#define MSR_MPERF		0xe7
#define MSR_APERF		0xe8

/* SMU requests for the frequencies matching the CPPC performance values */
#define SMU_MSG_GET_CPPC_NOM_FREQ	0x3a
#define SMU_MSG_GET_CPPC_MIN_FREQ	0x40

#define CPPC_REVISION		3
#define CPPC_NUM_ENTRIES	23

/* Name of the processor device for a CPU, and the shared CPPC package */
#define CPU_NAME		"\\_SB.C%03X"
#define CPPC_PACKAGE		"GCPC"

/* Write a register in an MSR, or an unsupported register if @msr is 0 */
static void cppc_write_reg(struct acpi_ctx *ctx, u32 msr, uint width,
			   uint offset)
{
	struct acpi_gen_regaddr reg = {
		.space_id = ACPI_ADDRESS_SPACE_MEMORY,
	};

	if (msr) {
		reg.space_id = ACPI_ADDRESS_SPACE_FIXED;
		reg.bit_width = width;
		reg.bit_offset = offset;
		reg.access_size = ACPI_ACCESS_SIZE_QWORD_ACCESS;
		reg.addrl = msr;
	}
	acpigen_write_register_resource(ctx, &reg);
}

/* Write a frequency from the SMU in MHz, or an unsupported register */
static void cppc_write_freq(struct acpi_ctx *ctx, u32 msg)
{
	u32 args[SMU_NUM_ARGS] = {};

	if (turin_smu_request(msg, args) == SMU_RESULT_OK && args[0])
		acpigen_write_dword(ctx, args[0]);
	else
		cppc_write_reg(ctx, 0, 0, 0);
}

/*
 * Write the CPPC package shared by all CPUs, as openSIL and coreboot do for
 * AMD processors: the performance levels are fields in the CPPC MSRs, which
 * Linux's amd-pstate driver reads and writes
 */
static void turin_write_cppc(struct acpi_ctx *ctx)
{
	acpigen_write_name(ctx, CPPC_PACKAGE);
	acpigen_write_package(ctx, CPPC_NUM_ENTRIES);
	acpigen_write_dword(ctx, CPPC_NUM_ENTRIES);
	acpigen_write_byte(ctx, CPPC_REVISION);
	cppc_write_reg(ctx, MSR_CPPC_CAPABILITY_1, 8, 24); /* highest perf */
	cppc_write_reg(ctx, MSR_CPPC_CAPABILITY_1, 8, 16); /* nominal perf */
	cppc_write_reg(ctx, MSR_CPPC_CAPABILITY_1, 8, 8); /* lowest non-lin */
	cppc_write_reg(ctx, MSR_CPPC_CAPABILITY_1, 8, 0); /* lowest perf */
	cppc_write_reg(ctx, 0, 0, 0);			/* guaranteed perf */
	cppc_write_reg(ctx, MSR_CPPC_REQUEST, 8, 16);	/* desired perf */
	cppc_write_reg(ctx, MSR_CPPC_REQUEST, 8, 8);	/* minimum perf */
	cppc_write_reg(ctx, MSR_CPPC_REQUEST, 8, 0);	/* maximum perf */
	cppc_write_reg(ctx, 0, 0, 0);		/* perf reduction tolerance */
	cppc_write_reg(ctx, 0, 0, 0);		/* time window */
	cppc_write_reg(ctx, 0, 0, 0);		/* counter wraparound time */
	cppc_write_reg(ctx, MSR_MPERF, 64, 0);	/* reference perf counter */
	cppc_write_reg(ctx, MSR_APERF, 64, 0);	/* delivered perf counter */
	cppc_write_reg(ctx, MSR_CPPC_STATUS, 2, 0);	/* performance limited */
	cppc_write_reg(ctx, MSR_CPPC_ENABLE, 1, 0);	/* CPPC enable */
	acpigen_write_dword(ctx, 1);		/* autonomous selection */
	cppc_write_reg(ctx, 0, 0, 0);		/* autonomous activity window */
	cppc_write_reg(ctx, MSR_CPPC_REQUEST, 8, 24); /* energy perf pref */
	cppc_write_reg(ctx, 0, 0, 0);		/* reference performance */
	cppc_write_freq(ctx, SMU_MSG_GET_CPPC_MIN_FREQ);	/* lowest freq */
	cppc_write_freq(ctx, SMU_MSG_GET_CPPC_NOM_FREQ);	/* nominal freq */
	acpigen_pop_len(ctx);
}

/*
 * The CPUs are started by turin_start_aps() rather than being devices, so
 * this device writes a processor device for each of them, with a _UID
 * matching its MADT entry and a _CPC returning the first CPU's CPPC package
 */
static int turin_cpu_fill_ssdt(const struct udevice *dev,
			       struct acpi_ctx *ctx)
{
	char name[16], cppc[24];
	const u8 *ids;
	int num_cpus, i;

	num_cpus = turin_get_cpus(&ids);
	snprintf(cppc, sizeof(cppc), CPU_NAME "." CPPC_PACKAGE, 0);
	for (i = 0; i < num_cpus; i++) {
		snprintf(name, sizeof(name), CPU_NAME, i);
		acpigen_write_device(ctx, name);
		acpigen_write_name_string(ctx, "_HID", "ACPI0007");
		acpigen_write_name_integer(ctx, "_UID", i);
		if (!i)
			turin_write_cppc(ctx);
		acpigen_write_method(ctx, "_CPC", 0);
		acpigen_emit_byte(ctx, RETURN_OP);
		acpigen_emit_namestring(ctx, cppc);
		acpigen_pop_len(ctx);	/* Method */
		acpigen_pop_len(ctx);	/* Device */
	}
	log_debug("%d processor devices\n", num_cpus);

	return 0;
}

static const struct acpi_ops turin_cpu_acpi_ops = {
	.fill_ssdt	= turin_cpu_fill_ssdt,
};

static const struct cpu_ops turin_cpu_ops = {
	.get_desc	= cpu_x86_get_desc,
	.get_count	= cpu_x86_get_count,
	.get_vendor	= cpu_x86_get_vendor,
};

static const struct udevice_id turin_cpu_ids[] = {
	{ .compatible = "amd,turin-cpu" },
	{ }
};

U_BOOT_DRIVER(turin_cpu) = {
	.name		= "turin_cpu",
	.id		= UCLASS_CPU,
	.of_match	= turin_cpu_ids,
	.bind		= cpu_x86_bind,
	.ops		= &turin_cpu_ops,
	ACPI_OPS_PTR(&turin_cpu_acpi_ops)
	.flags		= DM_FLAG_PRE_RELOC,
};
