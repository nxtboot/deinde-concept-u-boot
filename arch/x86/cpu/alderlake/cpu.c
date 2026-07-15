// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Alder Lake CPU driver, providing the CPU information used by the ACPI
 * tables (the MADT local-APIC entries and the processor count) and the
 * microcode for the application processors. The SoC-specific core setup
 * which coreboot does can be added here as the bring-up progresses
 */

#include <binman.h>
#include <cpu.h>
#include <dm.h>
#include <log.h>
#include <asm/arch/iomap.h>
#include <asm/cpu.h>
#include <asm/cpu_common.h>
#include <asm/cpu_x86.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/microcode.h>
#include <asm/msr.h>
#include <asm/msr-index.h>
#include <asm/processor.h>

DECLARE_GLOBAL_DATA_PTR;

/* MSR_PIC_MSG_CONTROL: stop the APIC blocking TPR updates */
#define TPR_UPDATES_DISABLE	BIT(10)

/* IA32_PACKAGE_THERM_INTERRUPT: critical-temperature interrupt */
#define PACKAGE_THERM_INT_CRITICAL	BIT(4)

/* MSR_POWER_CTL bits */
#define POWER_CTL_PROCHOT_INPUT	BIT(0)
#define POWER_CTL_EPB_ENABLE	BIT(18)
#define POWER_CTL_LOCK		BIT(23)

/* IA32_ENERGY_PERFORMANCE_BIAS hint (0 performance..15 powersave) */
#define EPB_MASK		0xf
#define EPB_NORMAL		7

/* CPUID.6 ECX: energy-performance bias supported */
#define CPUID_POWER_EPB		BIT(3)

/* PMC ACPI I/O registers used for the boot-time state report */
#define PM1_TMR			0x8
#define SMI_EN			0x30

/* Deepest package C-state for MSR_PMG_CST_CONFIG_CONTROL[3:0] */
#define PKG_C_STATE_LIMIT_C10	8

/*
 * Package C-state interrupt-response-time limits, in 1024ns units,
 * from coreboot's cannonlake configure_c_states() (Alder Lake leaves
 * these to the FSP, which does not program them under U-Boot)
 */
#define C_STATE_LATENCY_CONTROL_0_LIMIT	0x4e	/* package C3 */
#define C_STATE_LATENCY_CONTROL_1_LIMIT	0x76	/* package C6/C7 short */
#define C_STATE_LATENCY_CONTROL_2_LIMIT	0x94	/* package C6/C7 long */
#define C_STATE_LATENCY_CONTROL_3_LIMIT	0xfa	/* package C8 */
#define C_STATE_LATENCY_CONTROL_4_LIMIT	0x14c	/* package C9 */
#define C_STATE_LATENCY_CONTROL_5_LIMIT	0x3f2	/* package C10 */

/* Layout of the standard Intel microcode-update header */
#define UCODE_UPDATE_REV	0x04
#define UCODE_SIG		0x0c
#define UCODE_PROC_FLAGS	0x18
#define UCODE_DATA_SIZE		0x1c
#define UCODE_TOTAL_SIZE	0x20
#define UCODE_DEFAULT_DATA	2000
#define UCODE_DEFAULT_SIZE	2048

/* The optional extended-signature table follows the update data */
#define UCODE_EXT_HDR_LEN	20
#define UCODE_EXT_SIG_LEN	12

/**
 * ucode_matches() - Check whether an update is for this CPU
 *
 * An update matches if the CPU's signature and platform-id flag appear
 * in its header or in its extended-signature table (an update built for
 * one stepping often covers others that way)
 *
 * @pos: Address of the update
 * @sig: This CPU's signature
 * @pfid: This CPU's platform-id flag
 * Return: true if this update matches the CPU
 */
static bool ucode_matches(ulong pos, ulong sig, uint pfid)
{
	ulong dsize, tsize, ext;
	uint count, i;

	if (readl(pos + UCODE_SIG) == sig &&
	    (readl(pos + UCODE_PROC_FLAGS) & pfid))
		return true;

	dsize = readl(pos + UCODE_DATA_SIZE) ?: UCODE_DEFAULT_DATA;
	tsize = readl(pos + UCODE_TOTAL_SIZE) ?: UCODE_DEFAULT_SIZE;
	if (tsize <= UCODE_HEADER_LEN + dsize)
		return false;
	ext = pos + UCODE_HEADER_LEN + dsize;
	count = readl(ext);
	for (i = 0; i < count; i++) {
		ulong entry = ext + UCODE_EXT_HDR_LEN + i * UCODE_EXT_SIG_LEN;

		if (readl(entry) == sig && (readl(entry + 4) & pfid))
			return true;
	}

	return false;
}

/**
 * find_microcode() - Point ucode_base at this CPU's microcode update
 *
 * The FIT gives the boot processor its microcode, but the application
 * processors start with none, which makes them unreliable, so mp_init
 * has each one load the update at ucode_base. Find the update matching
 * this CPU in the image's microcode collection, which may hold several
 * (e.g. for different steppings)
 */
static void find_microcode(void)
{
	struct binman_entry mcu;
	ulong base, size, pos, sig;
	uint pfid;
	int ret;

	if (ucode_base)
		return;
	ret = binman_entry_find("microcode", &mcu);
	if (ret) {
		log_warning("CPU: no microcode in image (err=%d)\n", ret);
		return;
	}
	/*
	 * Entries found via the fdtmap have image-relative positions,
	 * unlike those in the devicetree's binman node, which are absolute
	 * addresses in the memory-mapped ROM (the image ends at 4GB)
	 */
	base = mcu.image_pos;
	if (base < (u32)-CONFIG_ROM_SIZE)
		base += (u32)-CONFIG_ROM_SIZE;
	size = mcu.size;
	sig = cpuid_eax(1);
	pfid = 1 << ((native_read_msr(MSR_IA32_PLATFORM_ID) >> 50) & 7);
	for (pos = base; pos < base + size;) {
		ulong total = readl(pos + UCODE_TOTAL_SIZE) ?:
			UCODE_DEFAULT_SIZE;

		if (ucode_matches(pos, sig, pfid)) {
			ucode_base = pos;
			ucode_size = total;
			log_debug("CPU: microcode rev %x at %lx (running rev %x)\n",
				  readl(pos + UCODE_UPDATE_REV), pos,
				  microcode_read_rev());
			return;
		}
		pos += total;
	}
	log_warning("CPU: no microcode for sig %lx flag %x\n", sig, pfid);
}

/**
 * adl_core_init() - Set up the core executing this function
 *
 * The equivalent of coreboot's per-thread soc_core_init(), run on the
 * boot processor when it is probed before mp_init and on each AP when
 * the MP flight plan probes its CPU device. Only touches MSRs, since on
 * the APs this runs with no driver-model or console services
 */
/**
 * adl_cstate_init() - Set up the C-state machinery on this core
 *
 * The FSP would normally do this in its per-AP feature programming,
 * which U-Boot does not use. Allow all package C-states, lock the
 * configuration and set the interrupt-response-time limit for each
 * package C-state, following coreboot's cannonlake
 * configure_c_states(). Without the response-time limits a core
 * entering C6 hangs the SoC
 */
static void adl_cstate_init(void)
{
	msr_clrsetbits_64(MSR_PMG_CST_CONFIG_CONTROL,
			  PKG_C_STATE_LIMIT_C2_MASK | CORE_C_STATE_LIMIT_C10_MASK,
			  PKG_C_STATE_LIMIT_C10 | CST_CFG_LOCK_MASK);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_0,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_0_LIMIT);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_1,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_1_LIMIT);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_2,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_2_LIMIT);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_3,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_3_LIMIT);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_4,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_4_LIMIT);
	wrmsrl(MSR_C_STATE_LATENCY_CONTROL_5,
	       IRTL_VALID | IRTL_1024_NS | C_STATE_LATENCY_CONTROL_5_LIMIT);
}

static void adl_core_init(void)
{
	/* Clear out pending MCEs and enable the machine-check banks */
	cpu_mca_configure();

	/* Set up C-states, which the skipped FSP CPU init would do */
	adl_cstate_init();

	/* Allow the local APIC to send TPR updates */
	msr_clrsetbits_64(MSR_PIC_MSG_CONTROL, TPR_UPDATES_DISABLE, 0);

	/* Enable Fast Strings and thermal monitoring */
	msr_setbits_64(MSR_IA32_MISC_ENABLE,
		       MISC_ENABLE_FAST_STRING | MISC_ENABLE_TM1);

	/* Disable thermal interrupts, keeping the package critical one */
	wrmsrl(MSR_IA32_THERM_INTERRUPT, 0);
	wrmsrl(MSR_IA32_PACKAGE_THERM_INTERRUPT, PACKAGE_THERM_INT_CRITICAL);

	/* Enable PROCHOT input and energy/performance-bias control */
	msr_setbits_64(MSR_POWER_CTL, POWER_CTL_PROCHOT_INPUT |
		       POWER_CTL_EPB_ENABLE | POWER_CTL_LOCK);

	/* Set a neutral energy/performance bias (Alder Lake wants 7) */
	if (cpuid_ecx(6) & CPUID_POWER_EPB)
		msr_clrsetbits_64(MSR_IA32_ENERGY_PERFORMANCE_BIAS, EPB_MASK,
				  EPB_NORMAL);
}

static int cpu_x86_adl_probe(struct udevice *dev)
{
	if (gd->flags & GD_FLG_RELOC) {
		find_microcode();
		adl_core_init();

		/*
		 * On the boot processor, probed after FSP-S has run, record
		 * whether SMIs are enabled (they should not be, since no
		 * SMM handler is installed) and that the ACPI PM timer
		 * ticks (consecutive reads differing)
		 */
		if (!dev_seq(dev))
			log_debug("SMI_EN %x, PM timer %x/%x\n",
				  inl(ACPI_BASE_ADDRESS + SMI_EN),
				  inl(ACPI_BASE_ADDRESS + PM1_TMR),
				  inl(ACPI_BASE_ADDRESS + PM1_TMR));
	}

	return 0;
}

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
	.probe		= cpu_x86_adl_probe,
	.ops		= &cpu_x86_adl_ops,
	.flags		= DM_FLAG_PRE_RELOC,
};
