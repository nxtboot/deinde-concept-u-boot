// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Loading of the next phase (SPL) from the memory-mapped SPI flash
 */

#define LOG_CATEGORY	LOGC_BOOT

#include <log.h>
#include <spl.h>

/*
 * Read SPL from the mapped SPI flash into Cache-as-RAM. The generic x86
 * TPL code cannot do this, since it does not know where the next phase is;
 * binman provides that in the image_pos and size symbols.
 */
static int rom_load_image(struct spl_image_info *spl_image,
			  struct spl_boot_device *bootdev)
{
	ulong spl_pos = spl_get_image_pos();
	ulong spl_size = spl_get_image_size();

	spl_image->size = CONFIG_SYS_MONITOR_LEN;  /* We don't know SPL size */
	spl_image->entry_point = spl_get_image_text_base();
	spl_image->load_addr = spl_image->entry_point;
	spl_image->os = IH_OS_U_BOOT;
	spl_image->name = "U-Boot";

	log_debug("Copying SPL from mapped SPI %lx size %lx to %lx\n", spl_pos,
		  spl_size, spl_image->load_addr);
	memcpy((void *)spl_image->load_addr, (void *)spl_pos, spl_size);
	return 0;
}
SPL_LOAD_IMAGE_METHOD("Mapped SPI", 2, BOOT_DEVICE_SPI_MMAP, rom_load_image);
