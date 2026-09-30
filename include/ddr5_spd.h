/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Decoding of DDR5 Serial Presence Detect (SPD) data
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __DDR5_SPD_H
#define __DDR5_SPD_H

#include <linux/types.h>

/* Size of the SPD EEPROM on a DDR5 module */
#define DDR5_SPD_SIZE		1024

/* Byte 3, bits 3:0: base module type */
enum ddr5_spd_module_type {
	DDR5_SPD_RDIMM = 1,
	DDR5_SPD_UDIMM = 2,
	DDR5_SPD_SODIMM = 3,
	DDR5_SPD_LRDIMM = 4,
	DDR5_SPD_CUDIMM = 5,
	DDR5_SPD_CSODIMM = 6,
	DDR5_SPD_MRDIMM = 7,
	DDR5_SPD_CAMM2 = 8,
	DDR5_SPD_DDIMM = 10,
	DDR5_SPD_SOLDERED = 11,
};

/**
 * struct ddr5_spd_info - What a module's SPD says about it
 *
 * @size: Module capacity in bytes
 * @speed: Maximum data rate in MT/s, rounded to the nearest 100
 * @mfg_id: Module manufacturer's JEDEC JEP106 id, bank byte in the low 8 bits
 *	and the code in the high 8 bits, as SMBIOS wants it
 * @dram_mfg_id: DRAM manufacturer's id, in the same form
 * @module_type: Base module type (enum ddr5_spd_module_type)
 * @ranks: Package ranks per channel
 * @channels: Sub-channels on the module (1 or 2)
 * @io_width: SDRAM I/O width in bits (4, 8, 16 or 32)
 * @bus_width: Data bus width per channel in bits (8 to 64)
 * @bus_width_ext: Extra (ECC) bits per channel (0, 4 or 8)
 * @revision: Module revision code
 * @part_number: Manufacturer's part number, without trailing spaces
 * @serial: Serial number as eight upper-case hex digits
 */
struct ddr5_spd_info {
	u64 size;
	u32 speed;
	u16 mfg_id;
	u16 dram_mfg_id;
	u8 module_type;
	u8 ranks;
	u8 channels;
	u8 io_width;
	u8 bus_width;
	u8 bus_width_ext;
	u8 revision;
	char part_number[31];
	char serial[9];
};

/**
 * ddr5_spd_decode() - Decode the SPD data of a DDR5 module
 *
 * @spd: SPD data, at least DDR5_SPD_SIZE bytes
 * @info: Returns what was found
 * Return: 0 if OK, -EINVAL if the data is not a DDR5 SPD, -ENODATA if the
 *	density or width fields are not ones this can decode
 */
int ddr5_spd_decode(const u8 *spd, struct ddr5_spd_info *info);

/**
 * ddr5_spd_mfg_name() - Look up the name of a common memory manufacturer
 *
 * @mfg_id: JEDEC id in the form used by struct ddr5_spd_info
 * Return: the name, or NULL if it is not one of the few known here
 */
const char *ddr5_spd_mfg_name(u16 mfg_id);

#endif
