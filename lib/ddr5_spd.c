// SPDX-License-Identifier: GPL-2.0+
/*
 * Decoding of DDR5 Serial Presence Detect (SPD) data, per JEDEC JESD400-5
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <ddr5_spd.h>
#include <errno.h>
#include <string.h>
#include <vsprintf.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/sizes.h>

/* Byte offsets in the SPD */
enum {
	SPD_KEY_BYTE = 2,	/* DRAM type: 0x12 is DDR5 */
	SPD_MODULE_TYPE = 3,
	SPD_FIRST_DENSITY = 4,	/* bits 4:0 density, bits 7:5 dies per package */
	SPD_FIRST_IO_WIDTH = 6,	/* bits 7:5 */
	SPD_SECOND_DENSITY = 8,
	SPD_SECOND_IO_WIDTH = 10,
	SPD_TCK_MIN = 20,	/* two bytes, in picoseconds */
	SPD_MODULE_ORG = 234,	/* bits 5:3 ranks - 1, bit 6 asymmetric ranks */
	SPD_BUS_WIDTH = 235,	/* bits 2:0 width, 4:3 extension, 6:5 channels */
	SPD_MODULE_MFG = 512,	/* two bytes */
	SPD_SERIAL = 517,	/* four bytes */
	SPD_PART_NUMBER = 521,	/* 30 bytes, space-padded */
	SPD_REVISION = 551,
	SPD_DRAM_MFG = 552,	/* two bytes */
};

#define SPD_DDR5		0x12
#define SPD_PART_NUMBER_LEN	30

/* SDRAM density per die in Gb, indexed by bits 4:0 of the density byte */
static const u8 density_gb[] = { 0, 4, 8, 12, 16, 24, 32, 48, 64 };

/* Dies per package, indexed by bits 7:5 of the density byte */
static const u8 dies_per_pkg[] = { 1, 0, 2, 4, 8, 16 };

static const struct {
	u16 id;
	const char *name;
} mfg_names[] = {
	{ 0x2c80, "Micron" },
	{ 0xad80, "SK Hynix" },
	{ 0xce80, "Samsung" },
	{ 0x0b02, "Nanya" },
	{ 0x9801, "Kingston" },
};

/* Bytes of one rank's SDRAMs, from a density byte and an I/O-width byte */
static u64 rank_size(const u8 *spd, uint density_off, uint width_off,
		     uint channels, uint bus_width)
{
	uint density = spd[density_off] & 0x1f;
	uint dies = spd[density_off] >> 5;
	uint io_width = 4 << (spd[width_off] >> 5);

	if (density >= ARRAY_SIZE(density_gb) || dies >= ARRAY_SIZE(dies_per_pkg))
		return 0;
	if (!density_gb[density] || !dies_per_pkg[dies])
		return 0;

	/* 1Gb is 128MB; each channel has bus_width / io_width SDRAMs */
	return (u64)channels * bus_width / io_width * dies_per_pkg[dies] *
		density_gb[density] * SZ_128M;
}

int ddr5_spd_decode(const u8 *spd, struct ddr5_spd_info *info)
{
	uint tck, mts, rank, i;
	bool asym;

	memset(info, '\0', sizeof(*info));
	if (spd[SPD_KEY_BYTE] != SPD_DDR5)
		return -EINVAL;

	info->module_type = spd[SPD_MODULE_TYPE] & 0xf;
	info->ranks = ((spd[SPD_MODULE_ORG] >> 3) & 7) + 1;
	asym = spd[SPD_MODULE_ORG] & BIT(6);
	info->bus_width = 8 << (spd[SPD_BUS_WIDTH] & 7);
	info->bus_width_ext = 4 * ((spd[SPD_BUS_WIDTH] >> 3) & 3);
	info->channels = ((spd[SPD_BUS_WIDTH] >> 5) & 3) + 1;
	info->io_width = 4 << (spd[SPD_FIRST_IO_WIDTH] >> 5);

	/* an asymmetric module alternates two kinds of rank */
	for (rank = 0; rank < info->ranks; rank++) {
		bool second = asym && (rank & 1);
		u64 size;

		size = rank_size(spd, second ? SPD_SECOND_DENSITY :
				 SPD_FIRST_DENSITY,
				 second ? SPD_SECOND_IO_WIDTH :
				 SPD_FIRST_IO_WIDTH, info->channels,
				 info->bus_width);
		if (!size)
			return -ENODATA;
		info->size += size;
	}

	/* the clock period gives the data rate, which is twice the clock */
	tck = spd[SPD_TCK_MIN] | spd[SPD_TCK_MIN + 1] << 8;
	if (tck) {
		mts = (2000000 + tck / 2) / tck;
		info->speed = (mts + 50) / 100 * 100;
	}

	info->mfg_id = spd[SPD_MODULE_MFG] | spd[SPD_MODULE_MFG + 1] << 8;
	info->dram_mfg_id = spd[SPD_DRAM_MFG] | spd[SPD_DRAM_MFG + 1] << 8;
	info->revision = spd[SPD_REVISION];
	memcpy(info->part_number, &spd[SPD_PART_NUMBER], SPD_PART_NUMBER_LEN);
	for (i = SPD_PART_NUMBER_LEN; i > 0 && info->part_number[i - 1] == ' ';
	     i--)
		info->part_number[i - 1] = '\0';
	snprintf(info->serial, sizeof(info->serial), "%02X%02X%02X%02X",
		 spd[SPD_SERIAL], spd[SPD_SERIAL + 1], spd[SPD_SERIAL + 2],
		 spd[SPD_SERIAL + 3]);

	return 0;
}

const char *ddr5_spd_mfg_name(u16 mfg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mfg_names); i++) {
		if (mfg_names[i].id == mfg_id)
			return mfg_names[i].name;
	}

	return NULL;
}
