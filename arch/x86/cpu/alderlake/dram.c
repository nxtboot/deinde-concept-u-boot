// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <init.h>
#include <asm/global_data.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

int dram_init(void)
{
	/*
	 * TODO(sjg@chromium.org): Read the usable memory from the FSP-M HOBs
	 * once SPL runs memory init. For now use the low-memory size that the
	 * brya FSP typically leaves available below the top of low DRAM.
	 */
	gd->ram_size = SZ_2G;

	return 0;
}
