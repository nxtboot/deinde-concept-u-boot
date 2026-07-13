// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Settings passed to FSP-M, which sets up SDRAM
 */

#define LOG_CATEGORY UCLASS_NORTHBRIDGE

#include <dm.h>
#include <log.h>
#include <asm-generic/gpio.h>
#include <linux/build_bug.h>
#include <asm/fsp2/fsp_internal.h>
#include <asm/arch/fsp/fsp_m_upd.h>

/*
 * The UPD region must match the FSP binary's, or FspMemoryInit() reads the
 * wrong settings. Check the size, which catches most mistakes
 */
static_assert(sizeof(struct fsp_m_config) == 0xaf8,
	      "FSP_M_CONFIG must match the FSP's layout");
static_assert(sizeof(struct fspm_upd) == 0xb40,
	      "FSPM_UPD must match the FSP's layout");

/* The board has at most this many DRAM-ID straps */
#define MAX_MEM_ID_GPIOS	8

/**
 * read_memory_id() - Read the DRAM-ID straps
 *
 * The board fits one of several memory parts, indicated by a few strapping
 * pins. The value selects which SPD data to give to the FSP.
 *
 * Return: ID value, or -ve on error
 */
static int read_memory_id(void)
{
	struct gpio_desc gpios[MAX_MEM_ID_GPIOS];
	ofnode node;
	int count;
	int ret;

	node = ofnode_path("/board");
	if (!ofnode_valid(node))
		return log_msg_ret("board", -ENOENT);

	count = gpio_request_list_by_name_nodev(node, "memory-id-gpios", gpios,
						MAX_MEM_ID_GPIOS, GPIOD_IS_IN);
	if (count < 0)
		return log_msg_ret("gpios", count);
	/* an absent property would silently read as memory ID 0 */
	if (!count)
		return log_msg_ret("no memory-id-gpios", -ENOENT);

	ret = dm_gpio_get_values_as_int(gpios, count);
	gpio_free_list_nodev(gpios, count);
	if (ret < 0)
		return log_msg_ret("read", ret);

	log_debug("Memory ID %d (from %d straps)\n", ret, count);

	return ret;
}

int fspm_update_config(struct udevice *dev, struct fspm_upd *upd)
{
	struct fspm_arch_upd *arch = &upd->arch;
	int mem_id;

	/*
	 * Tell the FSP where it may put its stack. The memory settings
	 * themselves start from the FSP's own defaults, which the caller has
	 * copied in
	 */
	arch->nvs_buffer_ptr = NULL;

	/*
	 * The FSP needs its own stack. U-Boot's is at the top of the region
	 * it uses, so put the FSP's above that: FSP-T sets up rather more
	 * Cache-as-RAM than SYS_CAR_SIZE, and the rest is free
	 */
	arch->stack_base = (void *)(CONFIG_SYS_CAR_ADDR + CONFIG_SYS_CAR_SIZE);

	/*
	 * The FSP default is 160KB, which is too small for a debug FSP;
	 * there is CAR headroom up to 0xfeffff00
	 */
	arch->stack_size = 0x38000;
	arch->boot_loader_tolum_size = 0;
	arch->boot_mode = FSP_BOOT_WITH_FULL_CONFIGURATION;

	mem_id = read_memory_id();
	if (mem_id < 0)
		return log_msg_ret("memory ID", mem_id);
	log_info("Memory ID %d\n", mem_id);

	/*
	 * TODO(sjg@chromium.org): Use the memory ID to select the SPD data for
	 * the parts fitted, put that in the image with binman and point the
	 * FSP at it with upd->config.memory_spd_ptr*. The DQ/DQS maps and
	 * Rcomp settings are needed as well
	 */

	return 0;
}

int fspm_done(struct udevice *dev)
{
	return 0;
}
