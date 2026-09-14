// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2019 Rockchip Electronics Co., Ltd
 */

#include <bootm.h>
#include <bootstage.h>
#include <cpu_func.h>
#include <debug_uart.h>
#include <dm.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <ram.h>
#include <spl.h>
#include <version.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/arch-rockchip/bootrom.h>
#include <asm/arch-rockchip/timer.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

#if CONFIG_IS_ENABLED(BANNER_PRINT)
#include <timestamp.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

__weak void tpl_board_init(void)
{
}

/**
 * tpl_enable_caches() - Turn on the caches now that DRAM is up
 *
 * TPL runs from SRAM, so the page tables go there too, just after the image,
 * where nothing which writes to DRAM can touch them. They must fit below the
 * early-malloc region, which sits under the stack.
 */
static void tpl_enable_caches(void)
{
	ulong addr, size;

	size = PGTABLE_SIZE;
	addr = ALIGN((ulong)__bss_end, SZ_4K);
	if (addr + size > gd->malloc_base) {
		printf("No room for page tables: %lx bytes at %lx\n", size, addr);
		return;
	}
	gd->arch.tlb_addr = addr;
	gd->arch.tlb_size = size;
	enable_caches();
}

void board_init_f(ulong dummy)
{
	struct udevice *dev;
	int ret;

#if defined(CONFIG_DEBUG_UART) && defined(CONFIG_TPL_SERIAL)
	/*
	 * Debug UART can be used from here if required:
	 *
	 * debug_uart_init();
	 * printch('a');
	 * printhex8(0x1234);
	 * printascii("string");
	 */
	debug_uart_init();
#ifdef CONFIG_TPL_BANNER_PRINT
	printascii("\nU-Boot TPL " PLAIN_VERSION " (" U_BOOT_DATE " - " \
				U_BOOT_TIME ")\n");
#endif
#endif
	/* Init secure timer */
	rockchip_stimer_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
		hang();
	}

	/* Init ARM arch timer */
	if (IS_ENABLED(CONFIG_SYS_ARCH_TIMER))
		timer_init();

	tpl_board_init();

	if (CONFIG_IS_ENABLED(RAM)) {
		ret = uclass_get_device(UCLASS_RAM, 0, &dev);
		if (ret) {
			printf("DRAM init failed: %d\n", ret);
			return;
		}

		/*
		 * Only now is it safe to map DRAM as normal memory, and there
		 * is no point in the caches if TPL does not set up DRAM (as
		 * with VBE, where VPL does)
		 */
		if (IS_ENABLED(CONFIG_ARM64) &&
		    !CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
			tpl_enable_caches();
	}
}

void spl_board_prepare_for_boot(void)
{
	/* The next phase expects the caches off, as they were on entry */
	if (IS_ENABLED(CONFIG_ARM64) && !CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
		cleanup_before_linux();
}

int board_return_to_bootrom(struct spl_image_info *spl_image,
			    struct spl_boot_device *bootdev)
{
	int ret;

	bootstage_mark_name(BOOTSTAGE_ID_END_TPL, "end tpl");
	ret = bootstage_stash_default();
	if (ret)
		debug("Failed to stash bootstage: err=%d\n", ret);

	spl_board_prepare_for_boot();
	back_to_bootrom(BROM_BOOT_NEXTSTAGE);

	return 0;
}

u32 spl_boot_device(void)
{
	if (IS_ENABLED(CONFIG_VPL))
		return BOOT_DEVICE_VBE;

	return BOOT_DEVICE_BOOTROM;
}
