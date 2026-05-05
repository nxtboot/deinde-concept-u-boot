// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <cpu_func.h>
#include <hang.h>
#include <spl.h>
#include <asm/spl.h>

void board_init_f(ulong dummy)
{
	if (CONFIG_IS_ENABLED(OF_CONTROL)) {
		int ret;

		ret = spl_early_init();
		if (ret) {
			debug("spl_early_init() failed: %d\n", ret);
			hang();
		}
	}

	preloader_console_init();
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_BOARD;
}

static int spl_qemu_load_image(struct spl_image_info *spl_image,
			       struct spl_boot_device *bootdev)
{
	spl_image->name = "U-Boot";
	spl_image->load_addr = spl_get_image_pos();
	spl_image->entry_point = spl_get_image_pos();
	flush_cache(spl_image->load_addr, spl_get_image_size());

	return 0;
}
SPL_LOAD_IMAGE_METHOD("QEMU", 0, BOOT_DEVICE_BOARD, spl_qemu_load_image);
