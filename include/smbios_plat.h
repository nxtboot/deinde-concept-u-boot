/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2024 Linaro Limited
 * Author: Raymond Mao <raymond.mao@linaro.org>
 */
#ifndef __SMBIOS_PLAT_H
#define __SMBIOS_PLAT_H

#include <smbios.h>

struct cache_info {
	union cache_config config;
	union cache_sram_type supp_sram_type;
	union cache_sram_type curr_sram_type;
	u32 line_size;
	u32 associativity;
	u32 max_size;
	u32 inst_size;
	u8 cache_type;
	u8 speed;
	u8 err_corr_type;
	char *socket_design;
};

struct processor_info {
	u32 id[2];
	u16 ext_clock;
	u16 max_speed;
	u16 curr_speed;
	u16 characteristics;
	u16 family2;
	u16 core_count2;
	u16 core_enabled2;
	u16 thread_count2;
	u16 thread_enabled;
	u8 type;
	u8 family;
	u8 voltage;
	u8 status;
	u8 upgrade;
	u8 core_count;
	u8 core_enabled;
	u8 thread_count;
	char *socket_design;
	char *manufacturer;
	char *version;
	char *sn;
	char *asset_tag;
	char *pn;
};

/* Lengths of the strings in struct memory_dev_info, including the terminator */
#define SYSINFO_MEM_LOCATOR_LEN	16
#define SYSINFO_MEM_STR_LEN	32
#define SYSINFO_MEM_SERIAL_LEN	16

/**
 * struct memory_array_info - The physical memory array, for SMBIOS type 16
 *
 * @max_capacity: Maximum memory the array can hold, in bytes
 * @num_devices: Number of memory-device slots, whether populated or not
 * @location: Where the array is (SMBIOS_MA_LOCATION_...)
 * @use: What the array is for (SMBIOS_MA_USE_...)
 * @err_corr: Error correction it supports (SMBIOS_MA_ERRCORR_...)
 */
struct memory_array_info {
	u64 max_capacity;
	u16 num_devices;
	u8 location;
	u8 use;
	u8 err_corr;
};

/**
 * struct memory_dev_info - A memory device (a DIMM slot), for SMBIOS type 17
 *
 * An empty slot has @size 0; the other fields then describe the slot, with
 * SMBIOS_MD_..._UNKNOWN where nothing is known.
 *
 * @size: Bytes installed in the slot, 0 if it is empty
 * @speed: Maximum data rate in MT/s, 0 if unknown
 * @config_speed: Data rate in use in MT/s, 0 if unknown
 * @total_width: Width including ECC bits, 0xffff if unknown
 * @data_width: Data width in bits, 0xffff if unknown
 * @min_voltage: Minimum operating voltage in mV, 0 if unknown
 * @max_voltage: Maximum operating voltage in mV, 0 if unknown
 * @config_voltage: Voltage in use in mV, 0 if unknown
 * @type_detail: Bits describing the device (SMBIOS_MD_TD_...)
 * @module_man_id: Module manufacturer's JEDEC id, bank byte low, 0 if unknown
 * @form_factor: SMBIOS_MD_FF_...
 * @mem_type: SMBIOS_MD_TYPE_...
 * @ranks: Number of ranks, 0 if unknown
 * @dev_locator: Name of the slot, e.g. "DIMM 0"
 * @bank_locator: Name of the bank or channel, e.g. "P0 CHANNEL A"
 * @manufacturer: Module manufacturer's name, empty if unknown
 * @part_number: Module part number, empty if unknown
 * @serial: Module serial number, empty if unknown
 */
struct memory_dev_info {
	u64 size;
	u16 speed;
	u16 config_speed;
	u16 total_width;
	u16 data_width;
	u16 min_voltage;
	u16 max_voltage;
	u16 config_voltage;
	u16 type_detail;
	u16 module_man_id;
	u8 form_factor;
	u8 mem_type;
	u8 ranks;
	char dev_locator[SYSINFO_MEM_LOCATOR_LEN];
	char bank_locator[SYSINFO_MEM_LOCATOR_LEN];
	char manufacturer[SYSINFO_MEM_STR_LEN];
	char part_number[SYSINFO_MEM_STR_LEN];
	char serial[SYSINFO_MEM_SERIAL_LEN];
};

struct sysinfo_plat {
	struct processor_info *processor;
	struct cache_info *cache;
	/* add other sysinfo structure here */
};

#if defined CONFIG_SYSINFO_SMBIOS
int sysinfo_get_cache_info(u8 level, struct cache_info *cache_info);
void sysinfo_cache_info_default(struct cache_info *ci);
int sysinfo_get_processor_info(struct processor_info *pinfo);

/**
 * sysinfo_get_memory_array_info() - Describe the system's memory array
 *
 * A platform which knows its memory devices implements this and
 * sysinfo_get_memory_dev_info(), so that they appear in SMBIOS types 16, 17
 * and 19.
 *
 * @info: Returns the array's details
 * Return: 0 if OK, -ENOSYS if the platform does not provide this
 */
int sysinfo_get_memory_array_info(struct memory_array_info *info);

/**
 * sysinfo_get_memory_dev_info() - Describe one memory device
 *
 * @idx: Slot index, from 0 to one less than the array's @num_devices
 * @info: Returns the slot's details
 * Return: 0 if OK, -ENOENT if there is no such slot, -ENOSYS if the platform
 *	does not provide this
 */
int sysinfo_get_memory_dev_info(int idx, struct memory_dev_info *info);
#else
static inline int sysinfo_get_cache_info(u8 level,
					 struct cache_info *cache_info)
{
	return -ENOSYS;
}

static inline void sysinfo_cache_info_default(struct cache_info *ci)
{
}

static inline int sysinfo_get_processor_info(struct processor_info *pinfo)
{
	return -ENOSYS;
}

static inline int
sysinfo_get_memory_array_info(struct memory_array_info *info)
{
	return -ENOSYS;
}

static inline int sysinfo_get_memory_dev_info(int idx,
					      struct memory_dev_info *info)
{
	return -ENOSYS;
}
#endif

#endif	/* __SMBIOS_PLAT_H */
