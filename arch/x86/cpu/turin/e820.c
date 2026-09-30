// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Memory map for AMD EPYC Turin, from the ABL's APOB
 *
 * The PSP's ABL records the system memory map in the APOB it leaves in DRAM:
 * the top of memory and a list of holes, one being the MMIO hole around 4GB
 * and the rest regions it keeps for the PSP, SMU, MPIO and so on. The OS
 * must not use any of them, so they go into the e820 map as reserved, as
 * coreboot's openSIL glue does
 */

#define LOG_CATEGORY LOGC_ARCH

#include <init.h>
#include <bloblist.h>
#include <log.h>
#include <asm/e820.h>
#include <asm/global_data.h>
#include <asm/msr.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

#define APOB_BASE		0x7010000
#define APOB_SIGNATURE		0x424f5041	/* "APOB" */
#define APOB_GROUP_FABRIC	9
#define APOB_TYPE_SYS_MAP	9
#define APOB_HMAC_SIZE		32

#define MSR_TOP_MEM		0xc001001a
#define HIGH_GAP_SIZE		SZ_64K

/**
 * struct apob_header - Header at the start of the APOB
 *
 * @signature: APOB_SIGNATURE ("APOB")
 * @version: Version of the APOB's layout
 * @size: Size of the whole APOB in bytes, including this header
 * @first_entry: Offset of the first entry from the start of the APOB
 */
struct apob_header {
	u32 signature;
	u32 version;
	u32 size;
	u32 first_entry;
};

/**
 * struct apob_entry - Header of each entry in the APOB
 *
 * The entry's data follows the header. The entries are grouped by the part
 * of the ABL which writes them, such as its memory or fabric code, and each
 * group numbers its own types.
 *
 * @group: Group the entry is in
 * @type: Type of entry within the group
 * @instance: Instance of the type, for entries which have several, such as
 *	one per die
 * @size: Size of the entry in bytes, including this header
 * @hmac: HMAC of the entry, which U-Boot does not check
 */
struct apob_entry {
	u32 group;
	u32 type;
	u32 instance;
	u32 size;
	u8 hmac[APOB_HMAC_SIZE];
};

/**
 * struct apob_hole - A range of the physical address space which is not RAM
 *
 * @base: Start address
 * @size: Size in bytes
 * @type: What the range is, such as the MMIO hole or a region kept for the
 *	PSP or SMU (MEMORY_HOLE_TYPES in openSIL)
 * @unused: Padding, so that each hole is a multiple of 8 bytes
 */
struct apob_hole {
	u64 base;
	u64 size;
	u32 type;
	u32 unused;
};

/**
 * struct apob_sys_map - The system memory map, an APOB fabric entry
 *
 * @hdr: Header of the entry
 * @top_of_mem: Address of the last byte of DRAM, plus one
 * @num_holes: Number of entries in @hole
 * @pad: Padding, so that @hole is 8-byte aligned
 * @hole: Ranges which are not RAM, in ascending order of address
 */
struct apob_sys_map {
	struct apob_entry hdr;
	u64 top_of_mem;
	u32 num_holes;
	u32 pad;
	struct apob_hole hole[];
};

static const struct apob_sys_map *apob_find_sys_map(void)
{
	const struct apob_header *apob = (void *)APOB_BASE;
	ulong pos, end;

	if (apob->signature != APOB_SIGNATURE)
		return NULL;
	end = APOB_BASE + apob->size;
	for (pos = APOB_BASE + apob->first_entry; pos < end;) {
		const struct apob_entry *entry = (void *)pos;

		if (!entry->size)
			break;
		if (entry->group == APOB_GROUP_FABRIC &&
		    entry->type == APOB_TYPE_SYS_MAP)
			return (void *)entry;
		pos += entry->size;
	}

	return NULL;
}

/* Add RAM from @start to @end, less any reserved holes within it */
static void add_ram(struct e820_ctx *ctx, const struct apob_sys_map *map,
		    u64 start, u64 end)
{
	u64 pos = start;
	int i;

	/* the ABL lists the holes in ascending order */
	for (i = 0; i < map->num_holes; i++) {
		const struct apob_hole *hole = &map->hole[i];
		u64 hstart = hole->base, hend = hole->base + hole->size;

		/*
		 * The MMIO hole runs from the top of low memory to a little
		 * above 4GB, so part of it lies in the high RAM range too
		 */
		if (hend <= pos || hstart >= end)
			continue;
		if (hstart > pos)
			e820_add(ctx, E820_RAM, pos, hstart - pos);
		hend = min(hend, end);
		e820_add(ctx, E820_RESERVED, max(hstart, pos),
			 hend - max(hstart, pos));
		pos = hend;
	}
	if (pos < end)
		e820_add(ctx, E820_RAM, pos, end - pos);
}

unsigned int install_e820_map(unsigned int max_entries,
			      struct e820_entry *entries)
{
	const struct apob_sys_map *map = apob_find_sys_map();
	u64 tom = native_read_msr(MSR_TOP_MEM) & ~(SZ_8M - 1ULL);
	struct e820_ctx ctx;
	u64 tstart, tend;
	int i;

	e820_init(&ctx, entries, max_entries);
	e820_add(&ctx, E820_RAM, 0, ISA_START_ADDRESS);
	e820_add(&ctx, E820_RESERVED, ISA_START_ADDRESS,
		 ISA_END_ADDRESS - ISA_START_ADDRESS);
	if (!map) {
		log_warning("No APOB memory map; reserving nothing\n");
		e820_add(&ctx, E820_RAM, ISA_END_ADDRESS,
			 tom - ISA_END_ADDRESS);
		return e820_finish(&ctx);
	}
	for (i = 0; i < map->num_holes; i++)
		log_debug("hole %2d: %010llx size %010llx type %u\n", i,
			  map->hole[i].base, map->hole[i].size,
			  map->hole[i].type);

	/*
	 * The bloblist holds the ACPI and SMBIOS tables, in RAM. Linux maps
	 * a table which lies in RAM one page at a time and silently skips
	 * any longer than a page, so tell it the bloblist is ACPI memory,
	 * which it maps as a whole, as the QEMU map does
	 */
	tstart = ALIGN_DOWN((ulong)gd->bloblist, SZ_4K);
	tend = ALIGN((ulong)gd->bloblist + bloblist_get_total_size(), SZ_4K);
	if (IS_ENABLED(CONFIG_BLOBLIST_TABLES) && tend <= tom) {
		add_ram(&ctx, map, ISA_END_ADDRESS, tstart);
		e820_add(&ctx, E820_ACPI, tstart, tend - tstart);
		add_ram(&ctx, map, tend, tom);
	} else {
		add_ram(&ctx, map, ISA_END_ADDRESS, tom);
	}
	e820_add(&ctx, E820_RESERVED, CONFIG_PCIE_ECAM_BASE,
		 CONFIG_PCIE_ECAM_SIZE);

	/*
	 * The first 64KB above 4GB reads as all ones although the APOB lists
	 * no hole there; coreboot reserves it too
	 */
	e820_add(&ctx, E820_RESERVED, SZ_4G, HIGH_GAP_SIZE);
	if (map->top_of_mem > SZ_4G)
		add_ram(&ctx, map, SZ_4G + HIGH_GAP_SIZE, map->top_of_mem);

	return e820_finish(&ctx);
}
