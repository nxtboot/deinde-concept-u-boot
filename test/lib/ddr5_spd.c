// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the DDR5 SPD decoder
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <ddr5_spd.h>
#include <string.h>
#include <test/lib.h>
#include <test/ut.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

/* The start of the SPD of a Micron MTC20F2085S1RC64BH1, a 32GB 2Rx8 RDIMM */
static const u8 micron_start[] = {
	0x30, 0x13, 0x12, 0x01, 0x04, 0x00, 0x20, 0x62,
	0x00, 0x00, 0x00, 0x00, 0xa2, 0x02, 0x1f, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x38, 0x01, 0xf2, 0x03,
};

/* Its manufacturing information, from byte 512 */
static const u8 micron_mfg[] = {
	0x80, 0x2c, 0x1a, 0x26, 0x03, 0xd3, 0x7d, 0x12, 0x38,
	'M', 'T', 'C', '2', '0', 'F', '2', '0', '8', '5', 'S', '1', 'R', 'C',
	'6', '4', 'B', 'H', '1', ' ', '3', 'S', 'F', 'F', ' ', ' ', ' ', ' ',
	' ', ' ', '1', 0x80, 0x2c,
};

static int lib_test_ddr5_spd(struct unit_test_state *uts)
{
	struct ddr5_spd_info info;
	u8 spd[DDR5_SPD_SIZE];

	memset(spd, '\0', sizeof(spd));
	memcpy(spd, micron_start, sizeof(micron_start));
	spd[234] = 0x08;	/* two ranks, symmetric */
	spd[235] = 0x32;	/* two channels of 32 bits with 8 bits of ECC */
	memcpy(&spd[512], micron_mfg, sizeof(micron_mfg));

	ut_assertok(ddr5_spd_decode(spd, &info));
	ut_asserteq_64(32ULL * SZ_1G, info.size);
	ut_asserteq(6400, info.speed);
	ut_asserteq(DDR5_SPD_RDIMM, info.module_type);
	ut_asserteq(2, info.ranks);
	ut_asserteq(2, info.channels);
	ut_asserteq(8, info.io_width);
	ut_asserteq(32, info.bus_width);
	ut_asserteq(8, info.bus_width_ext);
	ut_asserteq(0x2c80, info.mfg_id);
	ut_asserteq(0x2c80, info.dram_mfg_id);
	ut_asserteq('1', info.revision);
	ut_asserteq_str("MTC20F2085S1RC64BH1 3SFF", info.part_number);
	ut_asserteq_str("D37D1238", info.serial);
	ut_asserteq_str("Micron", ddr5_spd_mfg_name(info.mfg_id));
	ut_assertnull(ddr5_spd_mfg_name(0x1234));

	/* an asymmetric module: the second rank has 8Gb x16 SDRAMs */
	spd[234] |= BIT(6);
	spd[8] = 0x02;
	spd[10] = 0x40;
	ut_assertok(ddr5_spd_decode(spd, &info));
	ut_asserteq_64(20ULL * SZ_1G, info.size);

	/* a density the decoder does not know */
	spd[4] = 0x1f;
	ut_asserteq(-ENODATA, ddr5_spd_decode(spd, &info));

	/* not DDR5 at all */
	spd[2] = 0x0c;
	ut_asserteq(-EINVAL, ddr5_spd_decode(spd, &info));

	return 0;
}
LIB_TEST(lib_test_ddr5_spd, 0);
