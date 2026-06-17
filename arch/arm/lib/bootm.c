// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2011
 * Corscience GmbH & Co. KG - Simon Schwarz <schwarz@corscience.de>
 *  - Added prep subcommand support
 *  - Reorganized source - modeled after powerpc version
 *
 * (C) Copyright 2002
 * Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Marius Groeger <mgroeger@sysgo.de>
 *
 * Copyright (C) 2001  Erik Mouw (J.A.K.Mouw@its.tudelft.nl)
 */

#define LOG_CATEGORY	LOGC_BOOT

#include <bootm.h>
#include <bootstage.h>
#include <command.h>
#include <cpu_func.h>
#include <dm.h>
#include <init.h>
#include <log.h>
#include <asm/global_data.h>
#include <dm/root.h>
#include <env.h>
#include <image.h>
#include <u-boot/zlib.h>
#include <asm/byteorder.h>
#include <linux/libfdt.h>
#include <mapmem.h>
#include <fdt_support.h>
#include <asm/bootm.h>
#include <asm/secure.h>
#include <linux/compiler.h>
#include <bootm.h>
#include <vxworks.h>
#include <asm/cache.h>

#ifdef CONFIG_ARMV7_NONSEC
#include <asm/armv7.h>
#endif
#include <asm/setup.h>

DECLARE_GLOBAL_DATA_PTR;

static struct tag *params;

__weak void board_quiesce_devices(void)
{
}

static void setup_start_tag (struct bd_info *bd)
{
	params = (struct tag *)bd->bi_boot_params;

	params->hdr.tag = ATAG_CORE;
	params->hdr.size = tag_size (tag_core);

	params->u.core.flags = 0;
	params->u.core.pagesize = 0;
	params->u.core.rootdev = 0;

	params = tag_next (params);
}

static void setup_memory_tags(struct bd_info *bd)
{
	int i;

	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		params->hdr.tag = ATAG_MEM;
		params->hdr.size = tag_size (tag_mem32);

		params->u.mem.start = gd->dram[i].start;
		params->u.mem.size = gd->dram[i].size;

		params = tag_next (params);
	}
}

static void setup_commandline_tag(struct bd_info *bd, char *commandline)
{
	char *p;

	if (!commandline)
		return;

	/* eat leading white space */
	for (p = commandline; *p == ' '; p++);

	/* skip non-existent command lines so the kernel will still
	 * use its default command line.
	 */
	if (*p == '\0')
		return;

	params->hdr.tag = ATAG_CMDLINE;
	params->hdr.size =
		(sizeof (struct tag_header) + strlen (p) + 1 + 4) >> 2;

	strcpy (params->u.cmdline.cmdline, p);

	params = tag_next (params);
}

static void setup_initrd_tag(struct bd_info *bd, ulong initrd_start,
			     ulong initrd_end)
{
	/* an ATAG_INITRD node tells the kernel where the compressed
	 * ramdisk can be found. ATAG_RDIMG is a better name, actually.
	 */
	params->hdr.tag = ATAG_INITRD2;
	params->hdr.size = tag_size (tag_initrd);

	params->u.initrd.start = initrd_start;
	params->u.initrd.size = initrd_end - initrd_start;

	params = tag_next (params);
}

static void setup_serial_tag(struct tag **tmp)
{
	struct tag *params = *tmp;
	struct tag_serialnr serialnr;

	get_board_serial(&serialnr);
	params->hdr.tag = ATAG_SERIAL;
	params->hdr.size = tag_size (tag_serialnr);
	params->u.serialnr.low = serialnr.low;
	params->u.serialnr.high= serialnr.high;
	params = tag_next (params);
	*tmp = params;
}

static void setup_revision_tag(struct tag **in_params)
{
	u32 rev = 0;

	rev = get_board_rev();
	params->hdr.tag = ATAG_REVISION;
	params->hdr.size = tag_size (tag_revision);
	params->u.revision.rev = rev;
	params = tag_next (params);
}

static void setup_end_tag(struct bd_info *bd)
{
	params->hdr.tag = ATAG_NONE;
	params->hdr.size = 0;
}

__weak void setup_board_tags(struct tag **in_params) {}

#ifdef CONFIG_ARM64
static void do_nonsec_virt_switch(void)
{
	if (ll_boot_init()) {
		smp_kick_all_cpus();
		dcache_disable();	/* flush cache before switching to EL2 */
	}
}
#endif

__weak void board_prep_linux(struct bootm_headers *images) { }

/* Subcommand: PREP */
static int boot_prep_linux(struct bootm_headers *images)
{
	char *commandline = env_get("bootargs");
	bool use_fdt = CONFIG_IS_ENABLED(OF_LIBFDT) && IS_ENABLED(CONFIG_LMB);

	if (use_fdt && images->ft_len) {
		debug("using: FDT\n");
		if (image_setup_linux(images)) {
			log_err("FDT creation failed!");
			return -EINVAL;
		}
	} else if (BOOTM_ENABLE_TAGS) {
		debug("using: ATAGS\n");
		setup_start_tag(gd->bd);
		if (BOOTM_ENABLE_SERIAL_TAG)
			setup_serial_tag(&params);
		if (BOOTM_ENABLE_CMDLINE_TAG)
			setup_commandline_tag(gd->bd, commandline);
		if (BOOTM_ENABLE_REVISION_TAG)
			setup_revision_tag(&params);
		if (BOOTM_ENABLE_MEMORY_TAGS)
			setup_memory_tags(gd->bd);
		if (BOOTM_ENABLE_INITRD_TAG) {
			/*
			 * In boot_ramdisk_high(), it may relocate ramdisk to
			 * a specified location. And set images->initrd_start &
			 * images->initrd_end to relocated ramdisk's start/end
			 * addresses. So use them instead of images->rd_start &
			 * images->rd_end when possible.
			 */
			if (images->initrd_start && images->initrd_end) {
				setup_initrd_tag(gd->bd, images->initrd_start,
						 images->initrd_end);
			} else if (images->rd_start && images->rd_end) {
				setup_initrd_tag(gd->bd, images->rd_start,
						 images->rd_end);
			}
		}
		setup_board_tags(&params);
		setup_end_tag(gd->bd);
	} else if (use_fdt && !images->ft_len) {
		log_err("No FDT provided\n");
		return -ENOENT;
	} else {
		panic("FDT and ATAGS support not compiled in\n");
	}

	board_prep_linux(images);

	return 0;
}

__weak bool armv7_boot_nonsec_default(void)
{
#ifdef CONFIG_ARMV7_BOOT_SEC_DEFAULT
	return false;
#else
	return true;
#endif
}

#ifdef CONFIG_ARMV7_NONSEC
bool armv7_boot_nonsec(void)
{
	char *s = env_get("bootm_boot_mode");
	bool nonsec = armv7_boot_nonsec_default();

	if (s && !strcmp(s, "sec"))
		nonsec = false;

	if (s && !strcmp(s, "nonsec"))
		nonsec = true;

	return nonsec;
}
#endif

#ifdef CONFIG_ARM64
__weak void update_os_arch_secondary_cores(uint8_t os_arch)
{
}

#ifdef CONFIG_ARMV8_SWITCH_TO_EL1
static void switch_to_el1(void)
{
	if ((IH_ARCH_DEFAULT == IH_ARCH_ARM64) &&
	    (images.os.arch == IH_ARCH_ARM))
		armv8_switch_to_el1(0, (u64)gd->bd->bi_arch_number,
				    (u64)images.ft_addr, 0,
				    (u64)images.ep,
				    ES_TO_AARCH32);
	else
		armv8_switch_to_el1((u64)images.ft_addr, 0, 0, 0,
				    images.ep,
				    ES_TO_AARCH64);
}
#endif
#endif

/* Subcommand: GO */
static void boot_jump_linux(struct bootm_headers *images, int flag)
{
#ifdef CONFIG_ARM64
	int fake = (flag & BOOTM_STATE_OS_FAKE_GO);

	debug("## Transferring control to Linux (at address %lx)...\n",
		(ulong)images->ep);
	bootstage_mark(BOOTSTAGE_ID_RUN_OS);

	bootm_final(flag);
	cleanup_before_linux();

	if (!fake) {
#ifdef CONFIG_ARMV8_PSCI
		armv8_setup_psci();
#endif
		do_nonsec_virt_switch();

		update_os_arch_secondary_cores(images->os.arch);

#ifdef CONFIG_ARMV8_SWITCH_TO_EL1
		armv8_switch_to_el2((u64)images->ft_addr, 0, 0, 0,
				    (u64)switch_to_el1, ES_TO_AARCH64);
#else
		if ((IH_ARCH_DEFAULT == IH_ARCH_ARM64) &&
		    (images->os.arch == IH_ARCH_ARM))
			armv8_switch_to_el2(0, (u64)gd->bd->bi_arch_number,
					    (u64)images->ft_addr, 0,
					    (u64)images->ep,
					    ES_TO_AARCH32);
		else
			armv8_switch_to_el2((u64)images->ft_addr, 0, 0, 0,
					    images->ep,
					    ES_TO_AARCH64);
#endif
	}
#else
	unsigned long machid = gd->bd->bi_arch_number;
	char *s;
	void (*kernel_entry)(int zero, int arch, uint params);
	unsigned long r2;
	int fake = (flag & BOOTM_STATE_OS_FAKE_GO);

	kernel_entry = (void (*)(int, int, uint))images->ep;
#ifdef CONFIG_CPU_V7M_V8M
	ulong addr = (ulong)kernel_entry | 1;
	kernel_entry = (void *)addr;
#endif
	s = env_get("machid");
	if (s) {
		if (strict_strtoul(s, 16, &machid) < 0) {
			debug("strict_strtoul failed!\n");
			return;
		}
		printf("Using machid 0x%lx from environment\n", machid);
	}

	debug("## Transferring control to Linux (at address %08lx)" \
		"...\n", (ulong) kernel_entry);
	bootstage_mark(BOOTSTAGE_ID_RUN_OS);
	bootm_final(flag);
	cleanup_before_linux();

	if (CONFIG_IS_ENABLED(OF_LIBFDT) && images->ft_len)
		r2 = (unsigned long)images->ft_addr;
	else
		r2 = gd->bd->bi_boot_params;

	if (!fake) {
#ifdef CONFIG_ARMV7_NONSEC
		if (armv7_boot_nonsec()) {
			armv7_init_nonsec();
			secure_ram_addr(_do_nonsec_entry)(kernel_entry,
							  0, machid, r2);
		} else
#endif
			kernel_entry(0, machid, r2);
	}
#endif
}

/* Main Entry point for arm bootm implementation
 *
 * Modeled after the powerpc implementation
 * DIFFERENCE: Instead of calling prep and go at the end
 * they are called if subcommand is equal 0.
 */
int do_bootm_linux(int flag, struct bootm_info *bmi)
{
	struct bootm_headers *images = bmi->images;
	int ret;

	log_debug("boot linux flag %x\n", flag);
	/* No need for those on ARM */
	if (flag & BOOTM_STATE_OS_BD_T || flag & BOOTM_STATE_OS_CMDLINE)
		return -1;

	if (flag & BOOTM_STATE_OS_PREP) {
		log_debug("Preparing to boot Linux\n");
		ret = boot_prep_linux(images);
		if (ret)
			return ret;
		return 0;
	}

	if (flag & (BOOTM_STATE_OS_GO | BOOTM_STATE_OS_FAKE_GO)) {
		log_debug("Jumping to Linux (or faking it)\n");
		boot_jump_linux(images, flag);
		return 0;
	}

	log_debug("No flags set: continuing to prepare and jump to Linux\n");
	ret = boot_prep_linux(images);
	if (ret)
		return ret;
	boot_jump_linux(images, flag);
	return 0;
}

#if defined(CONFIG_BOOTM_VXWORKS)
void boot_prep_vxworks(struct bootm_headers *images)
{
#if defined(CONFIG_OF_LIBFDT)
	int off;

	if (images->ft_addr) {
		off = fdt_path_offset(images->ft_addr, "/memory");
		if (off > 0) {
			if (arch_fixup_fdt(images->ft_addr))
				puts("## WARNING: fixup memory failed!\n");
		}
	}
#endif
	cleanup_before_linux();
}

void boot_jump_vxworks(struct bootm_headers *images)
{
#if defined(CONFIG_ARM64) && defined(CONFIG_ARMV8_PSCI)
	armv8_setup_psci();
	smp_kick_all_cpus();
#endif

	/* ARM VxWorks requires device tree physical address to be passed */
	((void (*)(void *))images->ep)(images->ft_addr);
}
#endif
