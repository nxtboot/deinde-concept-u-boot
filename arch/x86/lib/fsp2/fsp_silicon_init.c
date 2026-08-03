// SPDX-License-Identifier: Intel
/*
 * Copyright (C) 2015-2016 Intel Corp.
 * (Written by Andrey Petrov <andrey.petrov@intel.com> for Intel Corp.)
 *
 * Mostly taken from coreboot fsp2_0/silicon_init.c
 */

#define LOG_CATEGORY UCLASS_NORTHBRIDGE

#include <binman.h>
#include <bootstage.h>
#include <dm.h>
#include <log.h>
#include <asm/arch/fsp/fsp_configs.h>
#include <asm/arch/fsp/fsp_s_upd.h>
#include <asm/fsp/fsp_infoheader.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/global_data.h>

int fsp_temp_ram_exit(void)
{
	fsp_temp_ram_exit_func func;
	struct binman_entry entry;
	struct fsp_header *hdr;
	struct udevice *dev;
	int ret;

	ret = fsp_locate_fsp(FSP_M, &entry, false, &dev, &hdr, NULL);
	if (ret)
		return log_msg_ret("flm", ret);
	if (!hdr->fsp_tempram_exit)
		return 0;

	log_debug("TempRamExit\n");
	func = (fsp_temp_ram_exit_func)(hdr->img_base + hdr->fsp_tempram_exit);
	ret = func(NULL);
	if (ret)
		return log_msg_ret("tre", ret);
	log_debug("done\n");

	return 0;
}

/* FspMultiPhaseSiInit() interface, from FSP 2.2 */
enum fsp_multi_phase_action {
	FSP_ACTION_GET_NUM_PHASES	= 0,
	FSP_ACTION_EXECUTE_PHASE	= 1,
};

struct fsp_multi_phase_params {
	u32 action;
	u32 phase_index;
	void *param_ptr;
};

struct fsp_multi_phase_num {
	u32 num_phases;
	u32 phases_executed;
};

typedef asmlinkage int (*fsp_multi_phase_func)(
		struct fsp_multi_phase_params *params);

/* The FSP-S header gained the FspMultiPhaseSiInit entry at this size */
#define FSP_HDR_LEN_MULTI_PHASE	0x4c

__weak void fsp_multi_phase_si_init_cb(int phase)
{
}

#ifdef CONFIG_INTEL_ALDERLAKE
/**
 * fsp_multi_phase_si_init() - Run the phased part of silicon init
 *
 * When enable_multi_phase_silicon_init is set, FspSiliconInit() returns
 * early and the rest of its work is split into phases, each run through
 * this entry point, with the board's fsp_multi_phase_si_init_cb() called
 * before each phase (coreboot sets BIOS_RESET_CPL itself before phase 2)
 *
 * @hdr: FSP-S header, providing the entry point
 * Return: 0 if OK, -ve on error
 */
static int fsp_multi_phase_si_init(struct fsp_header *hdr)
{
	struct fsp_multi_phase_params params;
	struct fsp_multi_phase_num num;
	fsp_multi_phase_func func;
	int ret, i;

	if (hdr->hdr_len < FSP_HDR_LEN_MULTI_PHASE)
		return log_msg_ret("hdr", -ENOSYS);
	if (!hdr->fsp_multi_phase_si_init)
		return log_msg_ret("entry", -ENOSYS);
	func = (fsp_multi_phase_func)(hdr->img_base +
				      hdr->fsp_multi_phase_si_init);

	memset(&num, '\0', sizeof(num));
	params.action = FSP_ACTION_GET_NUM_PHASES;
	params.phase_index = 0;
	params.param_ptr = &num;
	ret = func(&params);
	if (ret)
		return log_msg_ret("num", ret);
	log_debug("Multi-phase silicon init: %d phases\n", num.num_phases);

	for (i = 1; i <= num.num_phases; i++) {
		fsp_multi_phase_si_init_cb(i);
		params.action = FSP_ACTION_EXECUTE_PHASE;
		params.phase_index = i;
		params.param_ptr = NULL;
		log_debug("Phase %d...", i);
		ret = func(&params);
		log_debug("done (ret=%x)\n", ret);
		if (ret)
			return log_msg_ret("phase", ret);
	}

	return 0;
}
#endif /* CONFIG_INTEL_ALDERLAKE */

int fsp_silicon_init(bool s3wake, bool use_spi_flash)
{
	struct fsps_upd upd, *fsp_upd;
	fsp_silicon_init_func func;
	struct fsp_header *hdr;
	struct binman_entry entry;
	struct udevice *dev;
	ulong rom_offset = 0;
	u32 init_addr;
	int ret;

	log_debug("Locating FSP\n");
	ret = fsp_locate_fsp(FSP_S, &entry, use_spi_flash, &dev, &hdr,
			     &rom_offset);
	if (ret)
		return log_msg_ret("locate FSP", ret);
	binman_set_rom_offset(rom_offset);
	gd->arch.fsp_s_hdr = hdr;

	/* Copy over the default config */
	fsp_upd = (struct fsps_upd *)(hdr->img_base + hdr->cfg_region_off);
	if (fsp_upd->header.signature != FSPS_UPD_SIGNATURE)
		return log_msg_ret("Bad UPD signature", -EPERM);
	memcpy(&upd, fsp_upd, sizeof(upd));

	ret = fsps_update_config(dev, rom_offset, &upd);
	if (ret)
		return log_msg_ret("Could not setup config", ret);
	log_debug("Silicon init @ %x...", init_addr);
	bootstage_start(BOOTSTAGE_ID_ACCUM_FSP_S, "fsp-s");
	func = (fsp_silicon_init_func)(hdr->img_base + hdr->fsp_silicon_init);
	ret = func(&upd);
	bootstage_accum(BOOTSTAGE_ID_ACCUM_FSP_S);
	if (ret)
		return log_msg_ret("Silicon init fail\n", ret);
	log_debug("done\n");

#ifdef CONFIG_INTEL_ALDERLAKE
	/* Only Alder Lake's UPD layout has the arch area with this flag */
	if (upd.arch.enable_multi_phase_silicon_init) {
		ret = fsp_multi_phase_si_init(hdr);
		if (ret)
			return log_msg_ret("multi-phase", ret);
	}
#endif

	return 0;
}
