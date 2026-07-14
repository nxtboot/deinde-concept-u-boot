// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Silicon init (FSP-S) support
 */

#include <binman.h>
#include <init.h>
#include <log.h>
#include <asm/global_data.h>
#include <asm/pci.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/arch/fsp/fsp_s_upd.h>

/* The system agent's fixed BARs, which PCI enumeration can disturb */
#define SA_DEV_ROOT		PCI_BDF(0, 0, 0)
#define SA_EPBAR		0x40
#define SA_MCHBAR		0x48
#define SA_DMIBAR		0x68
#define SA_BAR_ENABLE		BIT(0)

#define MCH_BASE		0xfedc0000
#define DMI_BASE		0xfeda0000
#define EP_BASE			0xfeda1000

/* PchSerialIoSkipInit: leave the console UART alone */
#define SERIAL_IO_UART_SKIP_INIT	4

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

	/*
	 * TODO(sjg@chromium.org): Silicon init hangs while setting up the
	 * system agent (postcode 0xa61) or the graphics power management
	 * (0xa63), depending on the settings used. U-Boot reaches a prompt
	 * without it, so leave it out until it works
	 */
	if (0) {
		/* This must be called before any devices are probed */
		ret = fsp_silicon_init(s3wake, false);
		if (ret)
			return log_msg_ret("fss", ret);
	}

	return 0;
}
