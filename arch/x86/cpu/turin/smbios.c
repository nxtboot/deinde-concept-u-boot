// SPDX-License-Identifier: GPL-2.0+
/*
 * SMBIOS memory information for AMD EPYC Turin, from the ABL's APOB
 *
 * The ABL records what it found in the DIMM slots twice over: a summary of
 * each slot (its socket, channel and position, whether a module is fitted
 * and the speed and voltage it runs at) in the SMBIOS group, and the SPD it
 * read from each module in the memory group. The size, organisation and
 * identity of each module come from decoding its SPD.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_ARCH

#include <ddr5_spd.h>
#include <errno.h>
#include <log.h>
#include <smbios_def.h>
#include <smbios_plat.h>
#include <vsprintf.h>
#include <asm/arch/apob.h>
#include <linux/bitops.h>
#include <linux/sizes.h>
#include <linux/string.h>

/* The largest module Turin supports, for the array's maximum capacity */
#define TURIN_MAX_DIMM_SIZE	(256ULL * SZ_1G)

/* the ABL numbers each socket's dies as instances of the SPD entry */
#define APOB_MAX_SPD_INSTANCES	8

/* the DDR5 operating voltage, which the SPD does not record */
#define DDR5_VOLTAGE_MV		1100

/* APOB_TYPE_MEM_SMBIOS: a summary of the slots, then the ranges they map */
struct apob_dmi {
	struct apob_entry hdr;
	u8 mem_type;		/* bits 6:0 memory type, bit 7 ECC capable */
	u8 max_phys;		/* slots, each with a struct apob_dmi_slot */
	u8 max_logical;
	u8 reserved;
};

#define APOB_DMI_ECC		BIT(7)

struct apob_dmi_slot {
	u8 loc;			/* bits 1:0 socket, 5:2 channel, 6 dimm */
	u8 spd_addr;
	u16 handle;
	u16 config_speed;	/* memory clock in MHz */
	u16 config_mv;
};

#define APOB_SLOT_SOCKET(loc)	((loc) & 3)
#define APOB_SLOT_CHANNEL(loc)	(((loc) >> 2) & 0xf)
#define APOB_SLOT_DIMM(loc)	(((loc) >> 6) & 1)
#define APOB_SLOT_PRESENT	BIT(7)

/* APOB_TYPE_MEM_SPD_DATA: the SPD of each module on a die */
struct apob_spd {
	u8 dram_down_valid;
	u8 present;
	u16 reserved;
	u32 addr;
	u8 socket;
	u8 channel;
	u8 dimm;
	u8 shadow_valid;
	u8 data[DDR5_SPD_SIZE];
};

struct apob_spd_data {
	struct apob_entry hdr;
	u8 max_dimms_per_channel;
	u8 max_channels_per_socket;
	u16 reserved;
	struct apob_spd spd[];
};

static const struct apob_dmi *turin_find_dmi(void)
{
	return (void *)turin_apob_find(APOB_GROUP_SMBIOS, APOB_TYPE_MEM_SMBIOS,
				       0);
}

/* Find the SPD the ABL read from a slot, looking through each die's entry */
static const u8 *turin_find_spd(uint socket, uint channel, uint dimm)
{
	uint inst;

	for (inst = 0; inst < APOB_MAX_SPD_INSTANCES; inst++) {
		const struct apob_spd_data *data;
		int count, i;

		data = (void *)turin_apob_find(APOB_GROUP_MEM,
					       APOB_TYPE_MEM_SPD_DATA, inst);
		if (!data)
			continue;
		count = (data->hdr.size - sizeof(*data)) / sizeof(data->spd[0]);
		for (i = 0; i < count; i++) {
			const struct apob_spd *spd = &data->spd[i];

			if (spd->present && spd->socket == socket &&
			    spd->channel == channel && spd->dimm == dimm)
				return spd->data;
		}
	}

	return NULL;
}

int sysinfo_get_memory_array_info(struct memory_array_info *info)
{
	const struct apob_dmi *dmi = turin_find_dmi();

	if (!dmi)
		return -ENOSYS;
	memset(info, '\0', sizeof(*info));
	info->num_devices = dmi->max_phys;
	info->max_capacity = dmi->max_phys * TURIN_MAX_DIMM_SIZE;
	info->location = SMBIOS_MA_LOCATION_MOTHERBOARD;
	info->use = SMBIOS_MA_USE_SYSTEM;
	info->err_corr = dmi->mem_type & APOB_DMI_ECC ?
		SMBIOS_MA_ERRCORR_MBITECC : SMBIOS_MA_ERRCORR_NONE;

	return 0;
}

int sysinfo_get_memory_dev_info(int idx, struct memory_dev_info *info)
{
	const struct apob_dmi *dmi = turin_find_dmi();
	const struct apob_dmi_slot *slot;
	struct ddr5_spd_info spd;
	const char *name;
	const u8 *data;
	uint socket, channel, dimm;

	if (!dmi)
		return -ENOSYS;
	if (idx >= dmi->max_phys)
		return -ENOENT;
	slot = (const struct apob_dmi_slot *)(dmi + 1) + idx;
	socket = APOB_SLOT_SOCKET(slot->loc);
	channel = APOB_SLOT_CHANNEL(slot->loc);
	dimm = APOB_SLOT_DIMM(slot->loc);

	memset(info, '\0', sizeof(*info));
	snprintf(info->dev_locator, sizeof(info->dev_locator), "DIMM %u", dimm);
	snprintf(info->bank_locator, sizeof(info->bank_locator),
		 "P%u CHANNEL %c", socket, 'A' + channel);
	info->total_width = 0xffff;
	info->data_width = 0xffff;
	info->form_factor = SMBIOS_MD_FF_UNKNOWN;
	info->mem_type = SMBIOS_MD_TYPE_UNKNOWN;
	info->type_detail = SMBIOS_MD_TD_UNKNOWN;
	if (!(slot->loc & APOB_SLOT_PRESENT))
		return 0;

	data = turin_find_spd(socket, channel, dimm);
	if (!data || ddr5_spd_decode(data, &spd)) {
		log_warning("No SPD for socket %u channel %u DIMM %u\n", socket,
			    channel, dimm);
		return 0;
	}
	info->size = spd.size;
	info->speed = spd.speed;
	info->config_speed = 2 * slot->config_speed;
	info->data_width = spd.channels * spd.bus_width;
	info->total_width = info->data_width +
		spd.channels * spd.bus_width_ext;
	info->min_voltage = DDR5_VOLTAGE_MV;
	info->max_voltage = DDR5_VOLTAGE_MV;
	info->config_voltage = slot->config_mv;
	info->mem_type = SMBIOS_MD_TYPE_DDR5;
	info->type_detail = SMBIOS_MD_TD_SYNC;
	switch (spd.module_type) {
	case DDR5_SPD_RDIMM:
	case DDR5_SPD_LRDIMM:
	case DDR5_SPD_MRDIMM:
		info->form_factor = SMBIOS_MD_FF_DIMM;
		info->type_detail |= SMBIOS_MD_TD_RGSTD;
		break;
	case DDR5_SPD_UDIMM:
	case DDR5_SPD_CUDIMM:
		info->form_factor = SMBIOS_MD_FF_DIMM;
		info->type_detail |= SMBIOS_MD_TD_UNRGSTD;
		break;
	case DDR5_SPD_SODIMM:
	case DDR5_SPD_CSODIMM:
		info->form_factor = SMBIOS_MD_FF_SODIMM;
		info->type_detail |= SMBIOS_MD_TD_UNRGSTD;
		break;
	}
	if (spd.module_type == DDR5_SPD_LRDIMM)
		info->type_detail |= SMBIOS_MD_TD_LRDIMM;
	info->ranks = spd.ranks;
	info->module_man_id = spd.mfg_id;
	name = ddr5_spd_mfg_name(spd.mfg_id);
	if (name)
		strlcpy(info->manufacturer, name, sizeof(info->manufacturer));
	else
		snprintf(info->manufacturer, sizeof(info->manufacturer),
			 "JEDEC ID %04x", spd.mfg_id);
	strlcpy(info->part_number, spd.part_number, sizeof(info->part_number));
	strlcpy(info->serial, spd.serial, sizeof(info->serial));
	log_debug("slot %d: %s %s %lluMB at %u MT/s\n", idx, info->manufacturer,
		  info->part_number, (unsigned long long)(spd.size >> 20),
		  info->config_speed);

	return 0;
}
