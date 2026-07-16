// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Silicon init (FSP-S) support
 */

#include <binman.h>
#include <binman_sym.h>
#include <dm.h>
#include <efi.h>
#include <init.h>
#include <log.h>
#include <spi_flash.h>
#include <linux/linkage.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/mrccache.h>
#include <asm/pci.h>
#include <asm/arch/gpio.h>
#include <asm/arch/iomap.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/arch/fsp/fsp_s_upd.h>

/* The microcode collection, which the FSP loads on the APs */
binman_sym_declare(ulong, microcode, image_pos);
binman_sym_declare(ulong, microcode, size);

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
 * GPE routing: which GPIO group feeds each of the three GPE0 DWORDs. The PMC's
 * GPE_CFG register (in PWRM) holds the PMC code for each group and every GPIO
 * community's MISCCFG register (in its sideband space) holds the matching group
 * number; for the three groups used here the two encodings happen to have the
 * same values. The routing matches coreboot's brya devicetree: GPP_A -> DW0,
 * GPP_E -> DW1, GPP_F -> DW2, so GPP_A13 (the GSC interrupt) latches in
 * GPE0_STS DW0 bit 13
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
 * Route the GPIO groups to the GPE0 DWORDs. The FSP leaves this at reset
 * defaults, so do it here, after silicon init, as coreboot does in its ramstage
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
 * A do-nothing MP-services PPI for the FSP. With cpu_mp_ppi left at zero
 * the FSP assumes ownership of the APs even when skip_mp_init is set
 * (coreboot carries a FIXME about this), waking them with its own
 * INIT-SIPI sequence during silicon init and leaving them in an unknown
 * state. Handing it this stub instead keeps it off them entirely;
 * U-Boot's mp_init brings them up later. The layout follows the EDK2
 * EDKII_PEI_MP_SERVICES2_PPI and the FSP is built with the EDK2 IA32
 * ABI, hence asmlinkage. The stubs take no parameters, which is safe
 * since the caller pops the arguments in this ABI
 */
struct mp_services2_ppi {
	efi_status_t (asmlinkage *get_number_of_processors)(void *this,
			efi_uintn_t *nump, efi_uintn_t *num_enabledp);
	efi_status_t (asmlinkage *get_processor_info)(void);
	efi_status_t (asmlinkage *startup_all_aps)(void);
	efi_status_t (asmlinkage *startup_this_ap)(void);
	efi_status_t (asmlinkage *switch_bsp)(void);
	efi_status_t (asmlinkage *enable_disable_ap)(void);
	efi_status_t (asmlinkage *who_am_i)(void);
	efi_status_t (asmlinkage *startup_all_cpus)(void);
};

static asmlinkage efi_status_t mps_get_number_of_processors(void *this,
		efi_uintn_t *nump, efi_uintn_t *num_enabledp)
{
	/* The BSP alone; the FSP must leave the APs to U-Boot */
	*nump = 1;
	*num_enabledp = 1;

	return EFI_SUCCESS;
}

static asmlinkage efi_status_t mps_unsupported(void)
{
	return EFI_UNSUPPORTED;
}

static struct mp_services2_ppi mp_services_noop = {
	.get_number_of_processors	= mps_get_number_of_processors,
	.get_processor_info		= mps_unsupported,
	.startup_all_aps		= mps_unsupported,
	.startup_this_ap		= mps_unsupported,
	.switch_bsp			= mps_unsupported,
	.enable_disable_ap		= mps_unsupported,
	.who_am_i			= mps_unsupported,
	.startup_all_cpus		= mps_unsupported,
};

/* BIOS_RESET_CPL lives in the MCHBAR MMIO space */
#define BIOS_RESET_CPL		0x5da8

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
	 * Skip the FSP's multi-processor init: with it enabled (and the
	 * microcode region provided) silicon init hangs. U-Boot's mp_init
	 * brings up the APs and loads their microcode instead. The stub
	 * MP-services PPI stops the FSP from touching the APs itself
	 */
	cfg->skip_mp_init = 1;
	cfg->cpu_mp_ppi = (ulong)&mp_services_noop;
	cfg->microcode_region_base = binman_sym(ulong, microcode, image_pos);
	cfg->microcode_region_size = binman_sym(ulong, microcode, size);

	/*
	 * Keep the PCH ACPI PM timer running: left at zero, this makes the
	 * FSP disable the timer and enable the microcode's PM-timer
	 * emulation instead, but only on the boot processor, so the
	 * kernel's acpi_pm clocksource reads garbage on the other CPUs.
	 * coreboot also sets this, doing its own timer management
	 */
	cfg->enable_tco_timer = 1;

	/*
	 * CPU power-management and voltage-regulator policy for brya, so
	 * that the FSP's CPU feature pass (when enabled) sees the right
	 * configuration. Without the feature pass the C-state machinery is
	 * left unconfigured and any core entering C6 (which power-gates the
	 * core, relying on the PUNIT and voltage-regulator management set
	 * up here) hangs the SoC, so the kernel is limited to C1E on its
	 * command line.
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
	 * instead of the root port). coreboot disables VMD on this board
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
		return log_msg_ret("tre", ret);

	/* This must be called before any devices are probed */
	ret = fsp_silicon_init(s3wake, false);
	if (ret)
		return log_msg_ret("fss", ret);

	setup_gpe_routing();

	return 0;
}

int arch_misc_init(void)
{
	if (!ll_boot_init())
		return 0;

	/*
	 * Save the memory-training data now that the boot is well past the
	 * CSE's own flash accesses. A failure only costs a retrain on the
	 * next boot, so it does not stop the boot
	 */
	if (IS_ENABLED(CONFIG_MRC_CACHE_SAVE)) {
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
