// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Settings passed to the FSP-T TempRamInit call which sets up
 * Cache-as-RAM. This lives in the ROM and is read by car.S, which runs
 * before there is a stack, so it must be const and fully built at compile
 * time.
 */

#include <event.h>
#include <linux/build_bug.h>
#include <asm/arch/fsp/fsp_t_upd.h>

/*
 * The FSP's own copy of these settings has a zero code region, which makes
 * TempRamInit fail, so provide a filled-in copy. The microcode region is
 * left empty since the FIT causes the microcode to be loaded before the
 * CPU executes the reset vector.
 */
const struct fspt_upd temp_ram_init_params = {
	.header = {
		.signature = FSPT_UPD_SIGNATURE,
		.revision = 2,
	},
	.arch = {
		.revision = 1,
		.length = sizeof(struct fspt_arch_upd),
	},
	.core = {
		.code_region_base = CONFIG_FSP_CODE_REGION_BASE,
		.code_region_size = CONFIG_FSP_CODE_REGION_SIZE,
	},
	.config = {
		/*
		 * FSP-T programs the host bridge's ECAM window from these,
		 * and the FSP uses ECAM for all its PCI config access
		 */
		.pci_express_base = CONFIG_MMCONF_BASE_ADDRESS,
		.pci_express_region_length = CONFIG_SA_PCIEX_LENGTH,
	},
	.terminator = FSPT_UPD_TERMINATOR,
};

static_assert(sizeof(struct fspt_upd) == 0xe0,
	      "FSPT_UPD must match the FSP's layout");
