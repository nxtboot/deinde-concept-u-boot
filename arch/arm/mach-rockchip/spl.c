// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2019 Rockchip Electronics Co., Ltd
 */

#include <binman_sym.h>
#include <bloblist.h>
#include <bootm.h>
#include <cpu_func.h>
#include <debug_uart.h>
#include <dm.h>
#include <hang.h>
#include <image.h>
#include <init.h>
#include <log.h>
#include <mapmem.h>
#include <ram.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/arch-rockchip/bootrom.h>
#include <asm/arch-rockchip/timer.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/bitops.h>

DECLARE_GLOBAL_DATA_PTR;

int board_return_to_bootrom(struct spl_image_info *spl_image,
			    struct spl_boot_device *bootdev)
{
	back_to_bootrom(BROM_BOOT_NEXTSTAGE);

	return 0;
}

__weak const char * const boot_devices[BROM_LAST_BOOTSOURCE + 1] = {
};

__weak u32 read_brom_bootsource_id(void)
{
	u32 bootsource_id = readl(BROM_BOOTSOURCE_ID_ADDR);

	/* Re-map the raw value read from reg to an existing BROM_BOOTSOURCE
	 * enum value to avoid having to create a larger boot_devices table.
	 */
	if (bootsource_id == 0x81)
		return BROM_BOOTSOURCE_USB;
	else if (bootsource_id > BROM_LAST_BOOTSOURCE)
		log_debug("Unknown bootsource %x\n", bootsource_id);

	return bootsource_id;
}

const char *board_spl_was_booted_from(void)
{
	static u32 brom_bootsource_id_cache = BROM_BOOTSOURCE_UNKNOWN;
	u32 bootdevice_brom_id;
	const char *bootdevice_ofpath = NULL;

	if (brom_bootsource_id_cache != BROM_BOOTSOURCE_UNKNOWN)
		bootdevice_brom_id = brom_bootsource_id_cache;
	else
		bootdevice_brom_id = read_brom_bootsource_id();

	if (bootdevice_brom_id < ARRAY_SIZE(boot_devices))
		bootdevice_ofpath = boot_devices[bootdevice_brom_id];

	if (bootdevice_ofpath) {
		brom_bootsource_id_cache = bootdevice_brom_id;
		debug("%s: brom_bootdevice_id %x maps to '%s'\n",
		      __func__, bootdevice_brom_id, bootdevice_ofpath);
	} else {
		debug("%s: failed to resolve brom_bootdevice_id %x\n",
		      __func__, bootdevice_brom_id);
	}

	return bootdevice_ofpath;
}

u32 spl_boot_device(void)
{
	u32 boot_device = BOOT_DEVICE_MMC1;

	if (IS_ENABLED(CONFIG_VPL))
		return BOOT_DEVICE_VBE;

#if defined(CONFIG_TARGET_CHROMEBOOK_JERRY) || \
		defined(CONFIG_TARGET_CHROMEBIT_MICKEY) || \
		defined(CONFIG_TARGET_CHROMEBOOK_MINNIE) || \
		defined(CONFIG_TARGET_CHROMEBOOK_SPEEDY) || \
		defined(CONFIG_TARGET_CHROMEBOOK_BOB) || \
		defined(CONFIG_TARGET_CHROMEBOOK_KEVIN)
	return BOOT_DEVICE_SPI;
#endif
	if (CONFIG_IS_ENABLED(ROCKCHIP_BACK_TO_BROM))
		return BOOT_DEVICE_BOOTROM;

	return boot_device;
}

u32 spl_mmc_boot_mode(struct mmc *mmc, const u32 boot_device)
{
	return MMCSD_MODE_RAW;
}

__weak int board_early_init_f(void)
{
	return 0;
}

__weak int arch_cpu_init(void)
{
	return 0;
}

void board_init_f(ulong dummy)
{
	int ret;

	board_early_init_f();

	ret = spl_early_init();
	if (ret) {
		printf("spl_early_init() failed: %d\n", ret);
		hang();
	}
	arch_cpu_init();

	rockchip_stimer_init();

#ifdef CONFIG_SYS_ARCH_TIMER
	/* Init ARM arch timer in arch/arm/cpu/armv7/arch_timer.c */
	timer_init();
#endif
#if !defined(CONFIG_TPL) || defined(CONFIG_SPL_RAM)
	debug("\nspl:init dram\n");
	ret = dram_init();
	if (ret) {
		printf("DRAM init failed: %d\n", ret);
		return;
	}
	gd->ram_top = gd->ram_base + get_effective_memsize();
	gd->ram_top = board_get_usable_ram_top(gd->ram_size);

	if (IS_ENABLED(CONFIG_ARM64) && !CONFIG_IS_ENABLED(SYS_DCACHE_OFF)) {
		gd->relocaddr = gd->ram_top;
		arch_reserve_mmu();
		enable_caches();
	}
#endif
	preloader_console_init();
}

void spl_board_prepare_for_boot(void)
{
	/*
	 * TF-A is executed after SPL and before U-Boot. It removes our access
	 * to the SRAM. So move the bloblist to RAM.
	 */
	if (xpl_phase() == PHASE_SPL && CONFIG_IS_ENABLED(BLOBLIST_RELOC)) {
		ulong addr = CONFIG_IF_ENABLED_INT(BLOBLIST_RELOC,
						   BLOBLIST_RELOC_ADDR);

		log_debug("Relocating bloblist %p to %lx\n", gd_bloblist(),
			  addr);
		bloblist_reloc(map_sysmem(addr, 0), bloblist_get_total_size());
	}

	if (!IS_ENABLED(CONFIG_ARM64) || CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
		return;

	cleanup_before_linux();
}

#if CONFIG_IS_ENABLED(RAM_DEVICE)
binman_sym_declare_optional(ulong, payload, image_pos);
binman_sym_declare_optional(ulong, payload, size);

static ulong ramboot_load_read(struct spl_load_info *load, ulong sector,
			       ulong count, void *buf)
{
	ulong addr = (ulong)load->priv;

	memcpy(buf, map_sysmem(addr + sector, 0), count);
	return count;
}

static int ramboot_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	ulong image_pos = binman_sym(ulong, payload, image_pos);
	ulong size = binman_sym(ulong, payload, size);
	struct legacy_img_hdr *header;
	struct spl_load_info load;

	if (IS_ENABLED(CONFIG_SPL_LOAD_FIT)) {
		ulong addr = IF_ENABLED_INT(CONFIG_SPL_LOAD_FIT,
					    CONFIG_SPL_LOAD_FIT_ADDRESS);

		if (addr == CFG_SYS_SDRAM_BASE || addr == CONFIG_SPL_TEXT_BASE)
			return -ENODEV;

		if (image_pos != BINMAN_SYM_MISSING &&
		    size != BINMAN_SYM_MISSING) {
			header = map_sysmem(image_pos, 0);
			if (image_get_magic(header) == FDT_MAGIC) {
				memmove(map_sysmem(addr, 0), header, size);
				memset(header, 0, sizeof(*header));
			}
		}

		header = map_sysmem(addr, 0);
		if (image_get_magic(header) == FDT_MAGIC) {
			spl_load_init(&load, ramboot_load_read,
				      map_sysmem(addr, 0), 1);
			return spl_load_simple_fit(spl_image, &load, 0,
						   header);
		}
	}

	/* Fall back to a legacy image placed directly after SPL */
	if (image_pos != BINMAN_SYM_MISSING && size != BINMAN_SYM_MISSING) {
		header = map_sysmem(image_pos, 0);
		if (image_get_magic(header) == IH_MAGIC) {
			spl_load_init(&load, ramboot_load_read,
				      map_sysmem(image_pos, 0), 1);
			return spl_load(spl_image, bootdev, &load, size, 0);
		}
	}

	return -ENODEV;
}

/* Use priority and method name that sort before default spl_ram_load_image */
SPL_LOAD_IMAGE_METHOD("RAM", 0, BOOT_DEVICE_RAM, ramboot_load_image);
#endif
