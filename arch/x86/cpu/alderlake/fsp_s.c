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
#include <asm/mrccache.h>
#include <asm/pci.h>
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

int fsps_update_config(struct udevice *dev, ulong rom_offset,
		       struct fsps_upd *upd)
{
	struct fsp_s_config *cfg = &upd->config;
	void *vbt_buf;
	int ret;

	/*
	 * The end-of-post message goes to the CSE, which cannot be relied
	 * on here; U-Boot does not need it sent
	 */
	cfg->end_of_post_message = 0;

	/* The console UART is already set up */
	cfg->serial_io_uart_mode[0] = SERIAL_IO_UART_SKIP_INIT;

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

	/* U-Boot does not use the FSP's graphics output protocol */
	cfg->pei_graphics_peim_init = 0;

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
	 * CPU power management, matching coreboot. Without this the
	 * C-state machinery is left unconfigured and any core entering C6
	 * (which power-gates the core, relying on the PUNIT and
	 * voltage-regulator management set up here) hangs the SoC, so the
	 * kernel is limited to C1E on its command line
	 */
	cfg->pm_support = 1;
	cfg->hwp = 1;
	cfg->cx = 1;
	cfg->ps_on_enable = 1;
	cfg->pkg_c_state_limit = 255;	/* no limit (auto) */

	/* VccIn Aux Imon IccMax: 32A for a 15W Alder Lake-P, in 1/4A units */
	cfg->vcc_in_aux_imon_icc_imax = 128;

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
