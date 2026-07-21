// SPDX-License-Identifier: Intel
/*
 * Copyright (C) 2015-2016 Intel Corp.
 * (Written by Andrey Petrov <andrey.petrov@intel.com> for Intel Corp.)
 * (Written by Alexandru Gagniuc <alexandrux.gagniuc@intel.com> for Intel Corp.)
 * Mostly taken from coreboot fsp2_0/memory_init.c
 */

#include <binman.h>
#include <bootstage.h>
#include <dm.h>
#include <log.h>
#include <asm/cpu_common.h>
#include <asm/global_data.h>
#include <asm/mrccache.h>
#include <asm/processor.h>
#include <asm/fsp/fsp_infoheader.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/arch/fsp/fsp_configs.h>
#include <asm/arch/fsp/fsp_m_upd.h>

/*
 * FSP status codes requesting a platform reset. FSP-M returns one of these
 * (rather than an error) when it has trained memory but needs the platform
 * to be reset before the new settings take effect
 */
#define FSP_STATUS_RESET_REQUIRED_COLD	0x40000001
#define FSP_STATUS_RESET_REQUIRED_WARM	0x40000002
#define FSP_STATUS_RESET_REQUIRED_8	0x40000008

/**
 * fsp_handle_reset() - Perform a reset requested by FSP
 *
 * If @status is one of the FSP reset-required codes, reset the platform as
 * requested; this does not return. Otherwise do nothing.
 *
 * The requested reset type is honoured, as coreboot does: COLD and WARM are
 * plain host resets which leave the CSE running; only the higher codes get a
 * global reset. The WARM request comes from the CSE's response to the
 * DRAM-init-done message, asking for a host reset; a global reset would
 * disturb the CSE, so the next boot's FSP-M hangs waiting for it.
 *
 * The PMC's PWRM MMIO base comes from FSP2_PWRM_BASE, since it differs
 * between SoCs; when it is not provided, the reset is downgraded to a
 * plain host reset, since the ETR register cannot be reached to set up a
 * global one.
 *
 * @status: Status code returned by an FSP entry point
 */
static void fsp_handle_reset(int status)
{
	ulong pwrmbase = CONFIG_FSP2_PWRM_BASE;

	if (status < FSP_STATUS_RESET_REQUIRED_COLD ||
	    status > FSP_STATUS_RESET_REQUIRED_8)
		return;

	if (status == FSP_STATUS_RESET_REQUIRED_COLD) {
		log_info("FSP: cold reset (status %x)\n", status);
	} else if (status == FSP_STATUS_RESET_REQUIRED_WARM) {
		log_info("FSP: warm reset (status %x)\n", status);
	} else {
		log_info("FSP: global reset (status %x)\n", status);
		if (pwrmbase)
			intel_global_reset(pwrmbase);
	}
	if (pwrmbase)
		intel_host_reset(pwrmbase,
				 status != FSP_STATUS_RESET_REQUIRED_WARM);

	/* Without the PWRM base, fall back to a plain cold reset */
	x86_cf9_reset(FULL_RST | SYS_RST | RST_CPU);
	for (;;)
		cpu_hlt();
}

#ifdef CONFIG_ENABLE_MRC_CACHE
static int prepare_mrc_cache_type(enum mrc_type_t type,
				  struct mrc_data_container **cachep)
{
	struct mrc_data_container *cache;
	struct mrc_region entry;
	int ret;

	ret = mrccache_get_region(type, NULL, &entry);
	if (ret)
		return ret;
	cache = mrccache_find_current(&entry);
	if (!cache)
		return -ENOENT;

	log_debug("MRC at %x, size %x\n", (uint)cache->data, cache->data_size);
	*cachep = cache;

	return 0;
}

int prepare_mrc_cache(struct fspm_upd *upd)
{
	struct mrc_data_container *cache;
	int ret;

	ret = prepare_mrc_cache_type(MRC_TYPE_NORMAL, &cache);
	if (ret)
		return log_msg_ret("Cannot get normal cache", ret);
	upd->arch.nvs_buffer_ptr = cache->data;

	ret = prepare_mrc_cache_type(MRC_TYPE_VAR, &cache);
	if (ret)
		return log_msg_ret("Cannot get var cache", ret);
	upd->config.variable_nvs_buffer_ptr = cache->data;

	return 0;
}
#endif /* ENABLE_MRC_CACHE */

int fsp_memory_init(bool s3wake, bool use_spi_flash)
{
	struct fspm_upd upd, *fsp_upd;
	fsp_memory_init_func func;
	struct binman_entry entry;
	struct fsp_header *hdr;
	struct hob_header *hob;
	struct udevice *dev;
	int delay;
	int ret;

	log_debug("Locating FSP\n");
	ret = fsp_locate_fsp(FSP_M, &entry, use_spi_flash, &dev, &hdr, NULL);
	if (ret)
		return log_msg_ret("locate FSP", ret);
	debug("Found FSP_M at %x, size %x\n", hdr->img_base, hdr->img_size);

	/* Copy over the default config */
	fsp_upd = (struct fspm_upd *)(hdr->img_base + hdr->cfg_region_off);
	if (fsp_upd->header.signature != FSPM_UPD_SIGNATURE)
		return log_msg_ret("Bad UPD signature", -EPERM);
	memcpy(&upd, fsp_upd, sizeof(upd));

	delay = dev_read_u32_default(dev, "fspm,training-delay", 0);
	ret = fspm_update_config(dev, &upd);
	if (ret) {
		if (ret != -ENOENT)
			return log_msg_ret("Could not setup config", ret);
	} else {
		delay = 0;
	}

	if (delay)
		printf("SDRAM training (%d seconds)...", delay);
	else
		log_debug("SDRAM init...");
	bootstage_start(BOOTSTAGE_ID_ACCUM_FSP_M, "fsp-m");
	func = (fsp_memory_init_func)(hdr->img_base + hdr->fsp_mem_init);
	ret = func(&upd, &hob);
	bootstage_accum(BOOTSTAGE_ID_ACCUM_FSP_M);
	cpu_reinit_fpu();
	if (delay)
		printf("done\n");
	else
		log_debug("done\n");

	/* FSP may ask for a reset to apply the memory training; honour it */
	fsp_handle_reset(ret);
	if (ret)
		return log_msg_ret("SDRAM init fail\n", ret);

	gd->arch.hob_list = hob;

	ret = fspm_done(dev);
	if (ret)
		return log_msg_ret("fsm_done\n", ret);

	return 0;
}
