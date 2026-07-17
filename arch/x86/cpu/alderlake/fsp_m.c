// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Settings passed to FSP-M, which sets up SDRAM
 */

#define LOG_CATEGORY UCLASS_NORTHBRIDGE

#include <binman_sym.h>
#include <dm.h>
#include <log.h>
#include <asm-generic/gpio.h>
#include <linux/build_bug.h>
#include <linux/sizes.h>
#include <asm/arch/cpu.h>
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

/* The SPD data for each memory part which may be fitted */
binman_sym_declare(ulong, spd, image_pos);
binman_sym_declare(ulong, spd, size);

/* Each SPD is this large */
#define SPD_LEN		0x200

/*
 * The DQ and DQS mapping between the CPU and the DRAM, and the Rcomp
 * settings, all of which depend on the board's memory routing. These come
 * from the coreboot brya support
 */
static const u8 dq_map[8][16] = {
	{  9, 11,  8, 10, 12, 14, 13, 15,  4,  7,  6,  5,  2,  3,  0,  1 },
	{ 15, 12, 14, 13,  9, 10, 11,  8,  0,  1,  3,  2,  7,  5,  4,  6 },
	{  2,  3,  1,  0,  6,  7,  5,  4, 15,  9, 14,  8, 11, 10, 13, 12 },
	{  3,  1,  2,  0,  4,  6,  7,  5, 13, 15, 14, 12, 11,  9,  8, 10 },
	{ 13, 12, 14, 15,  9,  8, 10, 11,  4,  7,  5,  6,  1,  2,  0,  3 },
	{  5,  0,  6,  4,  3,  1,  7,  2, 11,  9, 10,  8, 15, 12, 14, 13 },
	{ 15, 12, 14, 13, 10,  9, 11,  8,  0,  1,  2,  3,  5,  6,  4,  7 },
	{  0,  3,  1,  2,  4,  5,  6,  7, 11,  8, 13, 14,  9, 12, 15, 10 },
};

static const u8 dqs_map[8][2] = {
	{ 1, 0 }, { 1, 0 }, { 0, 1 }, { 0, 1 },
	{ 1, 0 }, { 0, 1 }, { 1, 0 }, { 0, 1 },
};

/* The board uses only 100ohm Rcomp resistors, with these targets */
#define RCOMP_RESISTOR		100
static const u16 rcomp_targets[5] = { 40, 30, 30, 30, 30 };

/**
 * setup_memory() - Provide the board's memory configuration to the FSP
 *
 * @cfg: FSP-M configuration to update
 * @mem_id: DRAM ID read from the board's straps, selecting the SPD to use
 * Return: 0 if OK, -ve on error
 */
static int setup_memory(struct fsp_m_config *cfg, int mem_id)
{
	ulong spd_base, spd_size, spd;
	u32 *spd_ptr;
	int i;

	/*
	 * Memory cannot train without the SPD, so the symbol must resolve
	 * in this phase; catch a build that disables binman symbols for it
	 * (see binman_sym_assert()) rather than failing silently at run
	 * time
	 */
	binman_sym_assert(spd);
	spd_base = binman_sym(ulong, spd, image_pos);
	spd_size = binman_sym(ulong, spd, size);
	if (spd_base == BINMAN_SYM_MISSING)
		return log_msg_ret("spd", -ENOENT);
	if ((mem_id + 1) * SPD_LEN > spd_size)
		return log_msg_ret("mem_id", -ERANGE);
	spd = spd_base + mem_id * SPD_LEN;
	log_debug("SPD at %lx\n", spd);

	/*
	 * The memory is soldered down, so every channel uses the same SPD.
	 * There are two controllers with four channels each, one DIMM per
	 * channel
	 */
	/*
	 * The loop and memcpy() calls below rely on the SPD pointers and the
	 * per-channel map fields being adjacent, so pin that at build time
	 */
	static_assert(offsetof(struct fsp_m_config, memory_spd_ptr131) ==
		      offsetof(struct fsp_m_config, memory_spd_ptr000) +
		      15 * sizeof(u32), "SPD pointers must be contiguous");
	static_assert(offsetof(struct fsp_m_config, dq_map_cpu2_dram_mc1_ch3)
		      == offsetof(struct fsp_m_config,
				  dq_map_cpu2_dram_mc0_ch0) + 7 * 16,
		      "DQ maps must be contiguous");
	static_assert(offsetof(struct fsp_m_config, dqs_map_cpu2_dram_mc1_ch3)
		      == offsetof(struct fsp_m_config,
				  dqs_map_cpu2_dram_mc0_ch0) + 7 * 2,
		      "DQS maps must be contiguous");

	cfg->memory_spd_data_len = SPD_LEN;
	spd_ptr = &cfg->memory_spd_ptr000;
	for (i = 0; i < 8; i++) {
		spd_ptr[i * 2] = spd;		/* DIMM 0 */
		spd_ptr[i * 2 + 1] = 0;		/* DIMM 1: not fitted */
	}

	memcpy(cfg->dq_map_cpu2_dram_mc0_ch0, dq_map, sizeof(dq_map));
	memcpy(cfg->dqs_map_cpu2_dram_mc0_ch0, dqs_map, sizeof(dqs_map));
	cfg->dq_pins_interleaved = 0;		/* not used for LPDDR4x */

	cfg->rcomp_resistor = RCOMP_RESISTOR;
	memcpy(cfg->rcomp_target, rcomp_targets, sizeof(rcomp_targets));

	cfg->lp_ddr_dq_dqs_re_training = 1;
	cfg->ect = 1;				/* early command training */

	/* Felwinter is a ULT mobile board */
	cfg->user_bd = USER_BD_ULT_MOBILE;

	/*
	 * Use a fixed memory frequency rather than SA-GV (dynamic frequency
	 * switching), which is the FSP default. SA-GV needs a reset during
	 * the first training to apply its frequency points, and relies on
	 * the training data being saved so that the reset is not needed
	 * again; there is no such storage yet, so it would reset on every
	 * boot. Point 1 is the frequency coreboot's fixed setting uses on
	 * this platform
	 */
	cfg->sa_gv = SA_GV_FIXED_POINT1;

	/*
	 * Two more FSP-M settings for brya, both left at the FSP defaults
	 * here: disable C6DRAM (the C-state DRAM save machinery) and let
	 * pcode choose the boot ratio rather than pinning it to 28
	 */
	cfg->enable_c6_dram = 0;
	cfg->cpu_ratio = 0;

	return 0;
}

/**
 * setup_platform() - Apply coreboot's non-memory FSP-M settings
 *
 * These are the settings which coreboot uses on brya but which differ from
 * the FSP's own defaults (checked against FspmUpd.dsc in the FSP source).
 * The memory training works without them, but they keep the FSP away from
 * devices it should not touch and skip ME interactions U-Boot does not
 * need.
 *
 * @cfg: FSP-M configuration to update
 */
static void setup_platform(struct fsp_m_config *cfg)
{
	/*
	 * Do not poll the CSE for CPU-replacement status: with the CSE not
	 * fully operational the check times out and forces a full retrain
	 * on every boot
	 */
	cfg->skip_cpu_replacement_check = 1;

	/* BIOS Guard defaults to on; coreboot always disables it */
	cfg->bios_guard = 0;

	/* Leave the PCU thermal-management registers unlocked */
	cfg->lock_pt_mregs = 0;

	/* The GPIO pads are configured by U-Boot, not the FSP */
	cfg->gpio_override = 1;

	/* coreboot uses an 8MB TSEG; the FSP default is 16MB */
	cfg->tseg_size = SZ_8M;

	/*
	 * The debug UART is already set up (mode 4 is 'skip init'); the
	 * default of 2 has the FSP reinitialise and hide it
	 */
	cfg->serial_io_uart_debug_mode = 4;
	cfg->serial_io_uart_debug_controller_number = 0;
	/* the FSP writes its own debug output through this base */
	cfg->serial_io_uart_debug_mmio_base = CONFIG_DEBUG_UART_BASE;

	/* Devices not fitted / not enabled on brya */
	cfg->sa_ipu_enable = 0;
	cfg->pch_ish_enable = 0;

	/*
	 * Disable the Thunderbolt root ports and DMA engines: nothing sets
	 * up the type-C subsystem (no IOM firmware interaction), so the
	 * kernel's thunderbolt driver otherwise spends minutes timing out
	 * against dead controllers
	 */
	cfg->tcss_itbt_pcie0_en = 0;
	cfg->tcss_itbt_pcie1_en = 0;
	cfg->tcss_itbt_pcie2_en = 0;
	cfg->tcss_itbt_pcie3_en = 0;
	cfg->tcss_dma0_en = 0;
	cfg->tcss_dma1_en = 0;

	/* coreboot does not use C6 DRAM */
	cfg->enable_c6_dram = 0;

	/*
	 * The FSP default leaves VT-d enabled but with all the VT-d base
	 * addresses zero; its VtdInit() then hangs (postcode 0xa51).
	 * coreboot either fills in the base addresses or disables VT-d;
	 * U-Boot has no use for VT-d, so disable it
	 */
	cfg->vtd_disable = 1;

	/*
	 * coreboot sets the CPU ratio from the flex-ratio MSR, which reads
	 * as zero on these parts; the FSP default is 0x1c
	 */
	cfg->cpu_ratio = 0;

	/*
	 * Feed clock source 1 to PCH root port 9 (the NVMe SSD, at PCI
	 * 00:1d.0), with CLKREQ 1, as coreboot's brya devicetree does. The
	 * FSP default leaves every clock source unused, so the SSD's link
	 * never trains without this. The value is the 0-based root-port
	 * number
	 */
	cfg->pcie_clk_src_usage[1] = 8;
	cfg->pcie_clk_src_clk_req[1] = 1;
}

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
	int cache_ret;
	int mem_id;

	/*
	 * Look for saved memory-training data. With it, the FSP skips the
	 * lengthy training and does not ask for a reset afterwards
	 */
	arch->nvs_buffer_ptr = NULL;
	cache_ret = prepare_mrc_cache(upd);
	if (cache_ret && cache_ret != -ENOENT)
		return log_msg_ret("mrc", cache_ret);

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
	arch->boot_mode = cache_ret ? FSP_BOOT_WITH_FULL_CONFIGURATION :
		FSP_BOOT_ASSUMING_NO_CONFIGURATION_CHANGES;

	mem_id = read_memory_id();
	if (mem_id < 0)
		return log_msg_ret("memory ID", mem_id);
	log_debug("Memory ID %d\n", mem_id);

	setup_platform(&upd->config);

	return setup_memory(&upd->config, mem_id);
}

int fspm_done(struct udevice *dev)
{
	adl_log_pm_state("post-fspm");

	/* The SSD's power has been up for the whole of memory init */
	adl_release_ssd_reset();

	return 0;
}
