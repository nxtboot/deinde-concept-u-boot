// SPDX-License-Identifier: GPL-2.0+
/*
 * Setting up the application processors (APs) of AMD Turin
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * U-Boot runs only on the boot processor, but the OS expects every AP to have
 * the same microcode patch, memory map (TOM, TOM2 and SYSCFG) and MTRRs as
 * the boot processor, none of which the ABL sets up: an AP with the PSP's
 * patch corrupts memory much as the boot processor did before its update.
 *
 * The APs are held until the SMU releases each thread, which then starts in
 * real mode at the reset vector, as the boot processor did; until then they
 * ignore INIT and SIPI. So, as openSIL does, put a small routine at the top of
 * the reset segment,
 * release each thread in turn and wait for it to set up its MSRs, record its
 * APIC ID for the MADT and halt. The OS starts the APs again with INIT and
 * SIPI, which leaves the MSRs and the patch alone
 */

#define LOG_CATEGORY LOGC_ARCH

#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <pci.h>
#include <sort.h>
#include <string.h>
#include <time.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/lapic.h>
#include <asm/msr.h>
#include <asm/msr-index.h>
#include <asm/mtrr.h>
#include <asm/pci.h>
#include <asm/arch/trace.h>
#include <asm/arch/ap.h>
#include <asm/arch/cpu.h>
#include <linux/errno.h>

#define SYSCFG_MTRR_FIX_DRAM_MOD_EN	BIT(19)

/*
 * A released thread starts at the reset vector of the BIOS image, which the
 * PSP copied to CONFIG_TEXT_BASE: that is U-Boot's own reset code, so the
 * routine must be in place first
 */
#define TURIN_AP_RESET_VECTOR	(CONFIG_TEXT_BASE + CONFIG_ROM_SIZE - 0x10)
#define AP_TIMEOUT_MS		100	/* a launch takes about 20ms */
#define CPUID_ADDR_SIZE		0x80000008
#define CPUID_NC_MASK		0xff	/* number of threads, minus one */
#define CPUID_EXT_APIC		0x8000001e
#define CPUID_THREADS_SHIFT	8	/* in EBX: threads per core, minus one */
#define CPUID_THREADS_MASK	0xff

/*
 * The SMU's thread-enable register of each CCD: setting a bit releases a
 * thread (openSIL SmuLaunchThreadBrh)
 */
#define SMN_INDEX		0xb8
#define SMN_DATA		0xbc
#define SMU_THREAD_EN(ccd)	(0x203c0020 | (ccd) << 23)
#define MAX_CCDS		16
#define MAX_CORES_PER_CCD	16

/**
 * struct turin_ap_params - Parameters for the AP routine, in ap_start.S
 *
 * @lock: Serialises the APs
 * @ucode_lo: Address of the microcode patch (low word), or 0 for none
 * @ucode_hi: Address of the microcode patch (high word)
 * @msr_count: Number of entries in @msrs
 * @count: Number of APs which have finished
 * @ids: APIC ID of each AP which has finished
 * @msrs: MSRs to write, in order
 */
struct turin_ap_params {
	u32 lock;
	u32 ucode_lo;
	u32 ucode_hi;
	u32 apic_lo;
	u32 apic_hi;
	u32 msr_count;
	u32 count;
	u8 ids[TURIN_AP_MAX_IDS];
	struct {
		u32 index;
		u32 lo;
		u32 hi;
	} msrs[TURIN_AP_MAX_MSRS];
} __packed;

static u8 apic_ids[TURIN_AP_MAX_IDS];
static int num_cpus;

static u32 smn_read(u32 reg)
{
	ulong val;

	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_read_config(PCI_BDF(0, 0, 0), SMN_DATA, &val, PCI_SIZE_32);

	return val;
}

static void smn_write(u32 reg, u32 val)
{
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_DATA, val, PCI_SIZE_32);
	turin_trace_smn(reg, val);
}

static void add_msr(struct turin_ap_params *params, uint index, u64 val)
{
	int i = params->msr_count++;

	params->msrs[i].index = index;
	params->msrs[i].lo = val;
	params->msrs[i].hi = val >> 32;
}

/* Copy the boot processor's memory map and MTRRs, and enable the APIC */
static int setup_msrs(struct turin_ap_params *params)
{
	static const uint fixed[NUM_FIXED_MTRRS] = {
		MTRR_FIX_64K_00000_MSR, MTRR_FIX_16K_80000_MSR,
		MTRR_FIX_16K_A0000_MSR, MTRR_FIX_4K_C0000_MSR,
		MTRR_FIX_4K_C8000_MSR, MTRR_FIX_4K_D0000_MSR,
		MTRR_FIX_4K_D8000_MSR, MTRR_FIX_4K_E0000_MSR,
		MTRR_FIX_4K_E8000_MSR, MTRR_FIX_4K_F0000_MSR,
		MTRR_FIX_4K_F8000_MSR,
	};
	int num_var, i;
	u64 syscfg;

	num_var = native_read_msr(MTRR_CAP_MSR) & MTRR_CAP_VCNT_MASK;
	if (5 + CPUID_NAME_STRING_MSRS + NUM_FIXED_MTRRS + 2 * num_var >
	    TURIN_AP_MAX_MSRS)
		return -E2BIG;
	syscfg = native_read_msr(MSR_K8_SYSCFG);
	add_msr(params, MSR_K8_TOP_MEM1, native_read_msr(MSR_K8_TOP_MEM1));
	add_msr(params, MSR_K8_TOP_MEM2, native_read_msr(MSR_K8_TOP_MEM2));

	/* the fixed MTRRs' RdMem and WrMem bits need this to be writable */
	add_msr(params, MSR_K8_SYSCFG, syscfg | SYSCFG_MTRR_FIX_DRAM_MOD_EN);
	for (i = 0; i < NUM_FIXED_MTRRS; i++)
		add_msr(params, fixed[i], native_read_msr(fixed[i]));
	for (i = 0; i < num_var; i++) {
		add_msr(params, MTRR_PHYS_BASE_MSR(i),
			native_read_msr(MTRR_PHYS_BASE_MSR(i)));
		add_msr(params, MTRR_PHYS_MASK_MSR(i),
			native_read_msr(MTRR_PHYS_MASK_MSR(i)));
	}
	add_msr(params, MTRR_DEF_TYPE_MSR, native_read_msr(MTRR_DEF_TYPE_MSR));
	add_msr(params, MSR_K8_SYSCFG, syscfg);

	/* the processor name string, which the boot CPU got from the SMU */
	for (i = 0; i < CPUID_NAME_STRING_MSRS; i++)
		add_msr(params, MSR_CPUID_NAME_STRING0 + i,
			native_read_msr(MSR_CPUID_NAME_STRING0 + i));

	return 0;
}

/**
 * launch_thread() - Release a thread and wait for it to check in
 *
 * @params: Parameters of the AP routine
 * @ccd: CCD number
 * @bit: Thread's bit in the CCD's thread-enable register
 * Return: 0 if OK, -EALREADY if already released, -ETIMEDOUT if it did not
 * check in
 */
static int launch_thread(struct turin_ap_params *params, uint ccd, uint bit)
{
	u32 reg = SMU_THREAD_EN(ccd);
	u32 val = smn_read(reg);
	u32 count = readl(&params->count);
	ulong start;

	if (val & BIT(bit))
		return -EALREADY;

	/* the AP starts with its caches off, so write back what it needs */
	asm volatile("wbinvd" ::: "memory");
	smn_write(reg, val | BIT(bit));
	start = get_timer(0);
	while (readl(&params->count) == count) {
		if (get_timer(start) > AP_TIMEOUT_MS)
			return -ETIMEDOUT;
	}

	return 0;
}

static int compare_ids(const void *a, const void *b)
{
	return *(const u8 *)a - *(const u8 *)b;
}

int turin_start_aps(const void *ucode)
{
	ulong seg = TURIN_AP_RESET_VECTOR - TURIN_AP_VECTOR_OFFSET;
	int num_aps = cpuid_ecx(CPUID_ADDR_SIZE) & CPUID_NC_MASK;
	int threads = ((cpuid_ebx(CPUID_EXT_APIC) >> CPUID_THREADS_SHIFT) &
		       CPUID_THREADS_MASK) + 1;
	uint size = turin_ap_end - turin_ap_start;
	struct turin_ap_params *params;
	void *code, *vector, *save;
	uint ccd, core, thread;
	int ret;

	apic_ids[0] = lapicid();
	num_cpus = 1;
	if (!num_aps)
		return 0;
	if (TURIN_AP_CODE_OFFSET + size > TURIN_AP_VECTOR_OFFSET)
		return log_msg_ret("siz", -E2BIG);

	/* the reset segment holds U-Boot's reset code, so put it back after */
	code = map_sysmem(seg + TURIN_AP_CODE_OFFSET,
			  TURIN_AP_SEG_SIZE - TURIN_AP_CODE_OFFSET);
	save = malloc(TURIN_AP_SEG_SIZE - TURIN_AP_CODE_OFFSET);
	if (!save)
		return log_msg_ret("sav", -ENOMEM);
	memcpy(save, code, TURIN_AP_SEG_SIZE - TURIN_AP_CODE_OFFSET);

	memcpy(code, turin_ap_start, size);
	params = code + (turin_ap_params - turin_ap_start);
	params->ucode_lo = (ulong)ucode;
	params->ucode_hi = (u64)(ulong)ucode >> 32;
	/*
	 * a released thread's local APIC is disabled, so it would not answer
	 * the OS's INIT and SIPI
	 */
	params->apic_lo = LAPIC_DEFAULT_BASE | MSR_IA32_APICBASE_ENABLE;
	params->apic_hi = 0;
	ret = setup_msrs(params);
	if (ret)
		goto out;

	/* a near jump from the reset vector to the routine */
	vector = code + TURIN_AP_VECTOR_OFFSET - TURIN_AP_CODE_OFFSET;
	writeb(0xe9, vector);
	writew(TURIN_AP_CODE_OFFSET - (TURIN_AP_VECTOR_OFFSET + 3),
	       vector + 1);

	/*
	 * Release the threads in turn. The boot processor is thread 0 of
	 * core 0 of CCD 0; a core which does not start is taken to be the last
	 * one on its CCD
	 */
	for (ccd = 0; ccd < MAX_CCDS; ccd++) {
		for (core = 0; core < MAX_CORES_PER_CCD; core++) {
			for (thread = 0; thread < threads; thread++) {
				if (!ccd && !core && !thread)
					continue;
				ret = launch_thread(params, ccd,
						    core * threads + thread);
				log_debug("CCD %d core %d thread %d: %d (%d up)\n",
					  ccd, core, thread, ret,
					  params->count);
				if (ret == -EALREADY)
					log_warning("CCD %d core %d thread %d already running\n",
						    ccd, core, thread);
				else if (ret)
					break;
			}
			if (ret == -ETIMEDOUT && !thread)
				break;
		}
	}
	ret = 0;

	/* the boot processor's core has all its threads now too */
	msr_setbits_64(MSR_AMD64_TW_CFG, BIT_ULL(TW_CFG_COMBINE_CR0_CD_BIT));
	turin_trace_msr(MSR_AMD64_TW_CFG, native_read_msr(MSR_AMD64_TW_CFG));

	memcpy(apic_ids + 1, params->ids, params->count);
	num_cpus += params->count;
	qsort(apic_ids + 1, params->count, 1, compare_ids);
	log_debug("%d of %d APs started\n", params->count, num_aps);
	if (params->count != num_aps)
		ret = log_msg_ret("aps", -EIO);
out:
	memcpy(code, save, TURIN_AP_SEG_SIZE - TURIN_AP_CODE_OFFSET);
	free(save);

	return ret;
}

int turin_get_cpus(const u8 **idsp)
{
	*idsp = apic_ids;

	return num_cpus;
}
