// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Silicon init (FSP-S) support
 */

#include <binman.h>
#include <cpu.h>
#include <dm.h>
#include <efi.h>
#include <init.h>
#include <log.h>
#include <malloc.h>
#include <spi_flash.h>
#include <dm/uclass-internal.h>
#include <linux/linkage.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/lapic.h>
#include <asm/mp.h>
#include <asm/msr.h>
#include <asm/microcode.h>
#include <asm/mrccache.h>
#include <asm/pci.h>
#include <asm/arch/cpu.h>
#include <asm/arch/gpio.h>
#include <asm/arch/iomap.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/arch/fsp/fsp_s_upd.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;


/* The system agent's fixed BARs, which PCI enumeration can disturb */
#define SA_DEV_ROOT		PCI_BDF(0, 0, 0)
#define SA_EPBAR		0x40
#define SA_MCHBAR		0x48
#define SA_DMIBAR		0x68
#define SA_BAR_ENABLE		BIT(0)

#define MCH_BASE		0xfedc0000
#define DMI_BASE		0xfeda0000
#define EP_BASE			0xfeda1000

#define PCH_DEV_FAST_SPI	PCI_BDF(0, 0x1f, 5)
#define FAST_SPI_BASE		0xfe010000

/* PchSerialIoSkipInit: leave the console UART alone */
#define SERIAL_IO_UART_SKIP_INIT	4

/* PchSerialIoPci: the controller enumerates as a PCI device */
#define SERIAL_IO_I2C_PCI		1

/*
 * GPE routing: which GPIO group feeds each of the three GPE0 DWORDs.
 * The PMC's GPE_CFG register (in PWRM) holds the PMC code for each
 * group and every GPIO community's MISCCFG register (in its sideband
 * space) holds the matching group number; for the three groups used
 * here the two encodings happen to have the same values. The routing
 * matches coreboot's brya devicetree: GPP_A -> DW0, GPP_E -> DW1,
 * GPP_F -> DW2, so GPP_A13 (the GSC interrupt) latches in GPE0_STS
 * DW0 bit 13
 */
#define GPE_CFG			0x1920
#define  GPE_DW_MASK		0xfff
#define  GPE_DW0_GPP_A		0x2
#define  GPE_DW1_GPP_E		(0xc << 4)
#define  GPE_DW2_GPP_F		(0xa << 8)
#define  GPE_ROUTE		(GPE_DW0_GPP_A | GPE_DW1_GPP_E | \
				 GPE_DW2_GPP_F)

#define MISCCFG			0x10
#define  MISCCFG_GPE_MASK	(0xfff << 8)
#define  MISCCFG_ROUTE		(GPE_ROUTE << 8)

/* The GPIO communities on Alder Lake-P */
static const u8 gpio_pids[] = {
	PID_GPIOCOM0, PID_GPIOCOM1, PID_GPIOCOM2, PID_GPIOCOM3,
	PID_GPIOCOM4, PID_GPIOCOM5,
};

/*
 * Route the GPIO groups to the GPE0 DWORDs. The FSP leaves this at
 * reset defaults, so do it here, after silicon init
 */
static void setup_gpe_routing(void)
{
	int i;

	clrsetbits_le32((void *)(PCH_PWRM_BASE_ADDRESS + GPE_CFG),
			GPE_DW_MASK, GPE_ROUTE);
	for (i = 0; i < ARRAY_SIZE(gpio_pids); i++) {
		void *ptr = (void *)(CONFIG_PCR_BASE_ADDRESS +
				     ((ulong)gpio_pids[i] << 16) + MISCCFG);

		clrsetbits_le32(ptr, MISCCFG_GPE_MASK, MISCCFG_ROUTE);
	}
}

/*
 * The MP-services PPI, which lets the FSP run its per-CPU feature
 * programming (C-state machinery, GT pcode init and the rest) on the
 * application processors, which U-Boot's mp_init() has already started
 * by the time silicon init runs. The layout and behaviour follow the
 * EDK2 EDKII_PEI_MP_SERVICES2_PPI; the FSP is built with the EDK2 IA32
 * ABI, hence asmlinkage on everything it calls, including the
 * procedures it passes in. An all-APs dispatch runs the procedure on
 * every AP concurrently unless single_thread asks otherwise, matching
 * the EDK2 semantics
 */
struct efi_cpu_location {
	u32 package;
	u32 core;
	u32 thread;
};

struct efi_cpu_location2 {
	u32 package;
	u32 module;
	u32 tile;
	u32 die;
	u32 core;
	u32 thread;
};

struct efi_processor_info {
	u64 processor_id;
	u32 status_flag;
	struct efi_cpu_location location;
	struct efi_cpu_location2 location2;
};

/* Flag in the processor number requesting location2 in the info */
#define CPU_V2_EXTENDED_TOPOLOGY	BIT(24)

/* status_flag bits */
#define PROCESSOR_AS_BSP_BIT		BIT(0)
#define PROCESSOR_ENABLED_BIT		BIT(1)
#define PROCESSOR_HEALTH_STATUS_BIT	BIT(2)

typedef asmlinkage void (*efi_ap_procedure)(void *arg);

struct mp_services2_ppi {
	efi_status_t (asmlinkage *get_number_of_processors)(void *this,
			efi_uintn_t *nump, efi_uintn_t *num_enabledp);
	efi_status_t (asmlinkage *get_processor_info)(void *this,
			efi_uintn_t num, struct efi_processor_info *info);
	efi_status_t (asmlinkage *startup_all_aps)(void *this,
			efi_ap_procedure proc, bool single_thread,
			efi_uintn_t timeout_us, void *arg);
	efi_status_t (asmlinkage *startup_this_ap)(void *this,
			efi_ap_procedure proc, efi_uintn_t num,
			efi_uintn_t timeout_us, void *arg);
	efi_status_t (asmlinkage *switch_bsp)(void);
	efi_status_t (asmlinkage *enable_disable_ap)(void);
	efi_status_t (asmlinkage *who_am_i)(void *this, efi_uintn_t *nump);
	efi_status_t (asmlinkage *startup_all_cpus)(void *this,
			efi_ap_procedure proc, efi_uintn_t timeout_us,
			void *arg);
};

struct mps_call {
	efi_ap_procedure proc;
	void *arg;
};

/*
 * The APIC ID of each CPU, indexed by its logical number. The FSP's
 * dispatched procedures call the PPI from the APs (at least who_am_i),
 * where driver-model iteration and console logging are not safe, so
 * the PPI functions work only on this table, prepared on the BSP
 * before silicon init
 */
static u32 mps_apic_ids[CONFIG_MAX_CPUS];
static int mps_num_cpus;

static void mps_prepare(void)
{
	struct udevice *dev;

	mps_num_cpus = 0;
	for (uclass_find_first_device(UCLASS_CPU, &dev); dev;
	     uclass_find_next_device(&dev)) {
		struct cpu_plat *plat = dev_get_parent_plat(dev);
		int seq = dev_seq(dev);

		if (seq >= 0 && seq < CONFIG_MAX_CPUS) {
			mps_apic_ids[seq] = plat->cpu_id;
			if (seq >= mps_num_cpus)
				mps_num_cpus = seq + 1;
		}
	}
}

/* Count of completed procedure calls, incremented on the APs */
static volatile u32 mps_done_count;

/*
 * Feature-pass wedge diagnostic, run on the last AP after the FSP's
 * final dispatch (see arch_fsp_init_r() for the story it serves):
 * dump the GT and pcode state to port 80 just before the pcode crash,
 * then again from inside it. Codes:
 *   e600 - diagnostic running (its absence = dispatch problem)
 *   e7xx - GTTMMADR bits 31:24 (00 = BAR never programmed)
 *   e8xx - GT 0xd34 bits 7:0 (bit 0 set = the FSP's CpuDeadLoop cond)
 *   e9xx - GT 0xd38 bits 31:24 (bit 31 clear = ditto)
 *   eaxx - GT 0x130044 bits 7:0 (force-wake ack)
 *   ebxx - MCHBAR 0x5da8 bits 7:0 (BIOS_RESET_CPL readback)
 *   ecxx - MCHBAR 0x5da4 bits 31:24 (bit 31 = pcode mailbox RUN_BUSY)
 *   e500 - the second, in-wedge pass is starting
 * A code that never appears means the access before it hung the AP
 * (e500 alone missing = the whole fabric is dead by then)
 */
/* Call an EFI-ABI procedure from U-Boot's MP-callback context */
static void mps_call_proc(void *arg)
{
	struct mps_call *call = arg;

	call->proc(call->arg);
	mps_done_count++;
}

/* Run the procedure on the given CPUs, concurrently or one at a time */
static efi_status_t mps_run(efi_ap_procedure proc, void *arg, bool this_cpu,
			    bool serial, int cpu_select)
{
	struct mps_call call = { .proc = proc, .arg = arg };
	int seq;

	log_debug("run %p select %x this %d serial %d\n", proc, cpu_select,
		  this_cpu, serial);
	if (!proc)
		return EFI_INVALID_PARAMETER;
	if (this_cpu)
		proc(arg);
	if (cpu_select == MP_SELECT_APS && !serial) {
		int ret;

		ret = mp_run_on_cpus(MP_SELECT_APS, mps_call_proc, &call);
		log_debug("all-APs ret %d done %d\n", ret, mps_done_count);
		if (ret)
			return EFI_NOT_STARTED;

		return EFI_SUCCESS;
	}
	for (seq = 1; seq < mps_num_cpus; seq++) {
		int ret;

		if (cpu_select != MP_SELECT_APS && cpu_select != seq)
			continue;
		ret = mp_run_on_cpus(seq, mps_call_proc, &call);
		log_debug("cpu %d ret %d done %d\n", seq, ret,
			  mps_done_count);
		if (ret)
			return EFI_NOT_STARTED;
	}

	return EFI_SUCCESS;
}

static asmlinkage efi_status_t mps_get_number_of_processors(void *this,
		efi_uintn_t *nump, efi_uintn_t *num_enabledp)
{
	if (!nump || !num_enabledp)
		return EFI_INVALID_PARAMETER;
	/* Before mps_prepare() runs, report the BSP alone */
	*nump = mps_num_cpus ?: 1;
	*num_enabledp = *nump;

	return EFI_SUCCESS;
}

static asmlinkage efi_status_t mps_get_processor_info(void *this,
		efi_uintn_t num, struct efi_processor_info *info)
{
	bool want_loc2;
	uint apic;

	if (!info)
		return EFI_INVALID_PARAMETER;
	want_loc2 = num & CPU_V2_EXTENDED_TOPOLOGY;
	num &= ~CPU_V2_EXTENDED_TOPOLOGY;
	if (num >= mps_num_cpus)
		return EFI_NOT_FOUND;
	apic = mps_apic_ids[num];

	memset(info, '\0', sizeof(*info));
	info->processor_id = apic;
	info->status_flag = PROCESSOR_ENABLED_BIT |
		PROCESSOR_HEALTH_STATUS_BIT;
	if (!num)
		info->status_flag |= PROCESSOR_AS_BSP_BIT;

	/* Threads use the low APIC-ID bit on this SoC */
	info->location.package = 0;
	info->location.core = apic >> 1;
	info->location.thread = apic & 1;
	if (want_loc2) {
		info->location2.core = apic >> 1;
		info->location2.thread = apic & 1;
	}

	return EFI_SUCCESS;
}

static asmlinkage efi_status_t mps_startup_all_aps(void *this,
		efi_ap_procedure proc, bool single_thread,
		efi_uintn_t timeout_us, void *arg)
{
	return mps_run(proc, arg, false, single_thread, MP_SELECT_APS);
}

static asmlinkage efi_status_t mps_startup_all_cpus(void *this,
		efi_ap_procedure proc, efi_uintn_t timeout_us, void *arg)
{
	return mps_run(proc, arg, true, false, MP_SELECT_APS);
}

static asmlinkage efi_status_t mps_startup_this_ap(void *this,
		efi_ap_procedure proc, efi_uintn_t num,
		efi_uintn_t timeout_us, void *arg)
{
	if (!num || num >= mps_num_cpus)
		return EFI_INVALID_PARAMETER;

	return mps_run(proc, arg, false, true, num);
}

static asmlinkage efi_status_t mps_who_am_i(void *this, efi_uintn_t *nump)
{
	uint apic = lapicid();
	int seq;

	if (!nump)
		return EFI_INVALID_PARAMETER;
	for (seq = 0; seq < mps_num_cpus; seq++) {
		if (mps_apic_ids[seq] == apic) {
			*nump = seq;
			return EFI_SUCCESS;
		}
	}

	return EFI_DEVICE_ERROR;
}

static asmlinkage efi_status_t mps_switch_bsp(void)
{
	log_debug("switch_bsp\n");

	return EFI_UNSUPPORTED;
}

static asmlinkage efi_status_t mps_enable_disable_ap(void)
{
	log_debug("enable_disable_ap\n");

	return EFI_UNSUPPORTED;
}

/* Status-code type class reported to fsps_event_handler() */
#define STATUS_CODE_TYPE_MASK	0xff
#define STATUS_PROGRESS_CODE	1
#define STATUS_ERROR_CODE	2

/* Statically-sized header preceding any event data */
struct status_code_data {
	u16 header_size;
	u16 size;
	efi_guid_t type;
};

/* String event payload, following the header */
struct status_code_string {
	u32 string_type;	/* 0 is ASCII */
	const char *str;
};

/**
 * fsps_event_handler() - Receive status codes from the FSP
 *
 * The FSP reports progress and error status codes through this
 * callback, with a description string attached to the major
 * milestones, even in a release build. Log them, so that a hang
 * inside FspSiliconInit() shows how far it got. Unlike FSP-M, whose
 * handler lives in the revision-gated architectural header and so
 * never fires, FSP-S takes this through an ordinary config UPD
 */
static asmlinkage u32 fsps_event_handler(u32 type, u32 value, u32 instance,
					 efi_guid_t *caller_id,
					 struct status_code_data *data)
{
	uint code_type = type & STATUS_CODE_TYPE_MASK;
	const char *str = NULL;

	if (data && data->size >= sizeof(struct status_code_string)) {
		const struct status_code_string *pl =
			(void *)((u8 *)data + data->header_size);

		if (!pl->string_type && pl->str)
			str = pl->str;
	}
	if (code_type == STATUS_ERROR_CODE)
		log_warning("FSP: error %x %x %s\n", type, value,
			    str ? str : "");
	else if (str)
		log_debug("FSP: %x %x %s\n", type, value, str);

	return 0;
}

static struct mp_services2_ppi mp_services = {
	.get_number_of_processors	= mps_get_number_of_processors,
	.get_processor_info		= mps_get_processor_info,
	.startup_all_aps		= mps_startup_all_aps,
	.startup_this_ap		= mps_startup_this_ap,
	.switch_bsp			= mps_switch_bsp,
	.enable_disable_ap		= mps_enable_disable_ap,
	.who_am_i			= mps_who_am_i,
	.startup_all_cpus		= mps_startup_all_cpus,
};

/* BIOS_RESET_CPL lives in the MCHBAR MMIO space */
#define BIOS_RESET_CPL		0x5da8

/* IA32_MISC_ENABLE bit 38 disengages turbo when set */
#define MSR_MISC_ENABLE		0x1a0
#define  TURBO_DISENGAGE_HI	BIT(38 - 32)

/* P-state request; bits 15:8 take the desired ratio */
#define MSR_PERF_CTL		0x199

/* Bits 7:0 hold the 1-core maximum (turbo) ratio */

/* Runs on every CPU: allow turbo (clear the disengage bit) */
static void enable_turbo_cb(void *arg)
{
	msr_t msr;

	msr = msr_read(MSR_MISC_ENABLE);
	msr.hi &= ~TURBO_DISENGAGE_HI;
	msr_write(MSR_MISC_ENABLE, msr);
}

/*
 * Enable turbo on every CPU and request the maximum (1-core turbo)
 * ratio on the boot processor.
 * Without this the cores stay at their boot ratio until the kernel's
 * cpufreq management takes over
 */
static void set_max_ratio(void)
{
	msr_t msr, perf;
	int ret;

	ret = mp_run_on_cpus(MP_SELECT_ALL, enable_turbo_cb, NULL);
	if (ret)
		log_warning("turbo enable failed (err=%d)\n", ret);
	msr = msr_read(MSR_TURBO_RATIO_LIMIT);
	perf.lo = (msr.lo & 0xff) << 8;
	perf.hi = 0;
	msr_write(MSR_PERF_CTL, perf);
	log_debug("PERF_CTL set to %x\n", perf.lo);
}

/*
 * Called before each FspMultiPhaseSiInit() phase. coreboot sets
 * BIOS_RESET_CPL itself before phase 2 (its phase-1 callback does TCSS
 * setup, not needed here with Thunderbolt disabled)
 */
void fsp_multi_phase_si_init_cb(int phase)
{
	if (phase == 2) {
		log_debug("Setting BIOS_RESET_CPL\n");
		setbits_8(MCH_BASE + BIOS_RESET_CPL, 3);
		log_debug("done (now %x)\n", readb(MCH_BASE + BIOS_RESET_CPL));
	}
}

int fsps_update_config(struct udevice *dev, ulong rom_offset,
		       struct fsps_upd *upd)
{
	struct fsp_s_config *cfg = &upd->config;
	void *vbt_buf;
	int ret;

	/*
	 * Run silicon init in phases. This FSP reports a
	 * single phase (its phase count is only non-zero with the TCSS
	 * xHCI enabled), so the phase-2 callback below is not reached, but
	 * the flow then matches coreboot's exactly
	 */
	upd->arch.enable_multi_phase_silicon_init = 1;

	/*
	 * The end-of-post message goes to the CSE, which cannot be relied
	 * on here; U-Boot does not need it sent
	 */
	cfg->end_of_post_message = 0;

	/* The console UART is already set up */
	cfg->serial_io_uart_mode[0] = SERIAL_IO_UART_SKIP_INIT;

	/* The GSC (Cr50 TPM) is on I2C1; expose it as a PCI device */
	cfg->serial_io_i2c_mode[1] = SERIAL_IO_I2C_PCI;

	/*
	 * Provide the Video BIOS Table, found through the image's fdtmap.
	 * Without it the FSP's graphics power-management init hangs
	 * (postcode 0xa63)
	 */
	ret = binman_entry_map(ofnode_null(), "vbt", &vbt_buf, NULL);
	if (!ret && *(u32 *)vbt_buf == VBT_SIGNATURE)
		cfg->graphics_config_ptr = (ulong)vbt_buf;
	else
		log_warning("VBT not found; graphics init may hang\n");

	/*
	 * Run the FSP's graphics init (the GOP): as well as providing a
	 * display, it performs the display-engine init (CDCLK and friends)
	 * without which the kernel's i915 driver panics in
	 * intel_gt_init_clock_frequency()
	 */
	cfg->pei_graphics_peim_init = 1;

	/*
	 * Tell the GOP the lid is open, so it powers the panel on and
	 * enables the backlight. Left at 0 the FSP treats the lid as
	 * closed and leaves the panel dark
	 */
	cfg->lid_status = 1;

	/*
	 * U-Boot's mp_init brings up the APs and loads their microcode
	 * before silicon init runs; the MP-services PPI lets the FSP run
	 * its per-CPU feature programming on them (which would set up the
	 * missing C-state and GT-pcode machinery). With skip_mp_init
	 * cleared the FSP does use the PPI and every dispatch completes,
	 * but pcode then crashes during its power-management bring-up and
	 * stalls the whole fabric, so the feature pass stays disabled -
	 * see the full account in arch_fsp_init_r(). The FSP's own MP
	 * init (a null PPI) hangs even earlier
	 */
	/*
	 * Have the FSP report its progress, for debugging hangs. Note the
	 * release FSP compiles the status strings out, so this is silent
	 * unless a debug FSP is used; the postcodes on the EC console are
	 * the usable trace
	 */
	cfg->fsp_event_handler = (ulong)fsps_event_handler;

	/*
	 * Run the FSP's CPU feature pass: the FSP does the per-CPU
	 * programming itself on the APs (which arch_fsp_init_r() starts)
	 * through the MP-services PPI. This sets up the C-state machinery
	 * and the GT pcode, without which the kernel is limited to C1E
	 * idle and the GPU hangs on forcewake
	 */
	cfg->skip_mp_init = 0;
	cfg->cpu_mp_ppi = (ulong)&mp_services;

	/*
	 * The FSP's feature pass reloads the microcode from this region on
	 * every core (ReloadMicrocodePatch), and its MCHECK - which pcode's
	 * power-management bring-up then depends on - runs against it. With
	 * the region left unset, pcode wedges at postcode 0xa61.
	 *
	 * adl_find_microcode() locates this CPU's matching update and
	 * leaves its flash address in ucode_base. It must be called here,
	 * since the CPU driver probes after silicon init and so ucode_base
	 * is still zero at this point.
	 *
	 * That address is in the cached execute-in-place region, so the FSP
	 * reads it without the uncached-SPI crawl a lower address would
	 * cause, and it is the single matching update rather than the whole
	 * collection, whose other updates the FSP's matcher might pick
	 * instead
	 */
	adl_find_microcode();
	if (ucode_base && ucode_size) {
		cfg->microcode_region_base = ucode_base;
		cfg->microcode_region_size = ucode_size;
	} else {
		log_warning("No microcode located; feature pass may wedge\n");
	}

	/*
	 * Keep the PCH ACPI PM timer running: left at zero, this makes the
	 * FSP disable the timer and enable the microcode's PM-timer
	 * emulation instead, but only on the boot processor, so the
	 * kernel's acpi_pm clocksource reads garbage on the other CPUs.
	 * The timer is managed here instead
	 */
	cfg->enable_tco_timer = 1;

	/*
	 * CPU power-management and voltage-regulator policy for brya, so
	 * that the FSP's CPU feature pass sees the right configuration.
	 * This is what sets up the C-state machinery and the PUNIT and
	 * voltage-regulator management a core needs to enter C6 (which
	 * power-gates it) without hanging the SoC.
	 * VccInAuxImonIccImax is 32A for a 15W Alder Lake-P, in 1/4A units
	 */
	cfg->pm_support = 1;
	cfg->hwp = 1;
	cfg->cx = 1;
	cfg->ps_on_enable = 1;
	cfg->pkg_c_state_limit = 255;
	cfg->vcc_in_aux_imon_icc_imax = 128;
	/* Disable the C-state demotions on brya */
	cfg->pkg_c_state_demotion = 0;
	cfg->c1_state_auto_demotion = 0;
	cfg->energy_efficient_turbo = 0;
	/*
	 * The remaining PM configuration for brya: C1E enable, a 10C
	 * thermal-control offset, the PMC's S0ix substate mask (S0i2.0 |
	 * S0i3.0 for Alder Lake-P), the system agent's thermal device and
	 * 1.5% FIVR spread-spectrum
	 */
	cfg->c1e = 1;
	cfg->tcc_activation_offset = 10;
	cfg->pmc_lpm_s0ix_sub_state_enable_mask = 0x09;
	cfg->device4_enable = 1;
	cfg->fivr_spread_spectrum = 8;

	/*
	 * Felwinter has no external V1p05/Vnn bypass rails, but the FSP
	 * default (0x1) enables their use in the S0i1/S0i2 states, so the
	 * PMC would drive voltage regulators that do not exist on this
	 * board. Zero the whole external-FIVR configuration
	 */
	cfg->pch_fivr_ext_v1p05_rail_enabled_states = 0;
	cfg->pch_fivr_ext_v1p05_rail_supported_voltage_states = 0;
	cfg->pch_fivr_ext_v1p05_rail_voltage = 0;
	cfg->pch_fivr_ext_v1p05_rail_icc_max = 0;
	cfg->pch_fivr_ext_vnn_rail_enabled_states = 0;
	cfg->pch_fivr_ext_vnn_rail_supported_voltage_states = 0;
	cfg->pch_fivr_ext_vnn_rail_voltage = 0;
	cfg->pch_fivr_ext_vnn_rail_icc_max = 0;
	cfg->pch_fivr_ext_vnn_rail_sx_enabled_states = 0;
	cfg->pch_fivr_ext_vnn_rail_sx_voltage = 0;
	cfg->pch_fivr_ext_vnn_rail_sx_icc_max = 0;
	/*
	 * The VR settings for the 15W ADL-P SKU, from coreboot's
	 * vr_config.c tables: IA (domain 0) and GT (domain 1) loadlines
	 * in 1/100 mohm, Icc max in 1/4 A, TDC limit in 1/8 A
	 */
	cfg->ac_loadline[0] = 280;
	cfg->dc_loadline[0] = 280;
	cfg->ac_loadline[1] = 320;
	cfg->dc_loadline[1] = 320;
	cfg->icc_max[0] = 320;
	cfg->icc_max[1] = 160;
	cfg->tdc_enable[0] = 1;
	cfg->tdc_enable[1] = 1;
	cfg->tdc_current_limit[0] = 160;
	cfg->tdc_current_limit[1] = 160;
	cfg->tdc_time_window[0] = 28000;
	cfg->tdc_time_window[1] = 28000;
	cfg->irms[0] = 1;
	cfg->irms[1] = 1;

	/*
	 * The FSP default enables VMD, which remaps the storage root ports
	 * into the VMD controller's own domain, hiding the NVMe SSD from
	 * normal PCI enumeration (00:1d.0 then shows the VMD dummy device
	 * instead of the root port)
	 */
	cfg->vmd_enable = 0;

	return 0;
}

int arch_fsps_preinit(void)
{
	ulong mchbar;

	/*
	 * FSP-S accesses the system agent through MCHBAR (for example its
	 * BIOS_RESET_CPL write, postcode 0xa61) and hangs if it does not
	 * decode. SPL set these BARs up for FSP-M, but U-Boot's PCI
	 * enumeration reassigns the host bridge's BARs on the way here, so
	 * put them back
	 */
	pci_x86_read_config(SA_DEV_ROOT, SA_MCHBAR, &mchbar, PCI_SIZE_32);
	log_debug("SA BARs: mchbar %lx\n", mchbar);
	pci_x86_write_config(SA_DEV_ROOT, SA_MCHBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_MCHBAR, MCH_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_DMIBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_DMIBAR, DMI_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_EPBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_EPBAR, EP_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);

	return 0;
}

int arch_fsp_init_r(void)
{
	bool s3wake = false;
	int ret;

	if (!ll_boot_init())
		return 0;

	/*
	 * Now that U-Boot is running from DRAM, have the FSP move its own
	 * state out of Cache-as-RAM, so that silicon init can find it
	 */
	ret = fsp_temp_ram_exit();
	if (ret)
		return log_msg_ret("temp_ram_exit", ret);

	/*
	 * Start the APs so the FSP can run its per-CPU feature programming
	 * (the C-state machinery and GT pcode init) on them through the
	 * MP-services PPI during silicon init, since skip_mp_init is
	 * cleared. This must come after the TempRamExit call - starting
	 * the APs before it hangs it.
	 *
	 * Getting this to work took a long hunt: with the feature pass
	 * enabled the FSP's post-configuration pcode handshake wedged at
	 * postcode 0xa61 (SetBiosResetCpl). The cause was a missing
	 * microcode region (see the fsps_update_config() note): the FSP's
	 * MCHECK, which pcode depends on, never ran, so pcode crashed
	 * during power-management bring-up. Running this same FSP-S on top
	 */
	ret = x86_mp_init();
	if (ret)
		return log_msg_ret("mp", ret);
	mps_prepare();

	/* This must be called before any devices are probed */
	ret = fsp_silicon_init(s3wake, false);
	if (ret)
		return log_msg_ret("fsp_s", ret);

	setup_gpe_routing();

	return 0;
}

int arch_misc_init(void)
{
	if (!ll_boot_init())
		return 0;

	/* The CPUs are up (cpu_init_r has run), so turbo can be enabled */
	set_max_ratio();

	/*
	 * Save the memory-training data now that the boot is well past the
	 * CSE's own flash accesses. A failure only costs a retrain on the
	 * next boot, so it does not stop the boot
	 */
	if (IS_ENABLED(CONFIG_MRC_CACHE_SAVE_PROPER)) {
		struct udevice *dev;
		ulong bar, cmd;
		int ret;

		/*
		 * TempRamExit can leave the fast-SPI controller's temporary
		 * BAR disabled; put it back so that the save can talk to
		 * the flash
		 */
		pci_x86_read_config(PCH_DEV_FAST_SPI, PCI_BASE_ADDRESS_0,
				    &bar, PCI_SIZE_32);
		pci_x86_read_config(PCH_DEV_FAST_SPI, PCI_COMMAND, &cmd,
				    PCI_SIZE_16);
		log_debug("fast-spi bar %lx, cmd %lx\n", bar, cmd);
		pci_x86_write_config(PCH_DEV_FAST_SPI, PCI_BASE_ADDRESS_0,
				     FAST_SPI_BASE, PCI_SIZE_32);
		pci_x86_write_config(PCH_DEV_FAST_SPI, PCI_COMMAND,
				     cmd | PCI_COMMAND_MEMORY |
				     PCI_COMMAND_MASTER, PCI_SIZE_16);

		/*
		 * Probe the PCI bus and then the flash: the SPI
		 * controller's setup reads its PCI BAR, so the bus must be
		 * enumerated first (this board does not enable PCI_INIT_R,
		 * so nothing has probed it yet)
		 */
		ret = uclass_first_device_err(UCLASS_PCI, &dev);
		if (!ret)
			ret = uclass_first_device_err(UCLASS_SPI_FLASH, &dev);
		if (ret)
			log_warning("MRC: no SPI flash (err=%d)\n", ret);
		else
			mrccache_save();
	}

	return 0;
}
