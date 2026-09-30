// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2018
 * Mario Six, Guntermann & Drunck GmbH, mario.six@gdsys.cc
 */

#include <dm.h>
#include <smbios_def.h>
#include <smbios_plat.h>
#include <sysinfo.h>

#include "sandbox.h"

struct sysinfo_sandbox_priv {
	bool called_detect;
	int test_i1;
	int test_i2;
	u32 test_data[2];
};

char vacation_spots[][64] = {"R'lyeh", "Dreamlands", "Plateau of Leng",
			     "Carcosa", "Yuggoth", "The Nameless City"};

int sysinfo_sandbox_detect(struct udevice *dev)
{
	struct sysinfo_sandbox_priv *priv = dev_get_priv(dev);

	priv->called_detect = true;
	priv->test_i2 = 100;
	priv->test_data[0] = 0xabcdabcd;
	priv->test_data[1] = 0xdeadbeef;

	return 0;
}

int sysinfo_sandbox_get_bool(struct udevice *dev, int id, bool *val)
{
	struct sysinfo_sandbox_priv *priv = dev_get_priv(dev);

	switch (id) {
	case BOOL_CALLED_DETECT:
		/* Checks if the dectect method has been called */
		*val = priv->called_detect;
		return 0;
	}

	return -ENOENT;
}

int sysinfo_sandbox_get_int(struct udevice *dev, int id, int *val)
{
	struct sysinfo_sandbox_priv *priv = dev_get_priv(dev);

	switch (id) {
	case INT_TEST1:
		*val = priv->test_i1;
		/* Increments with every call */
		priv->test_i1++;
		return 0;
	case INT_TEST2:
		*val = priv->test_i2;
		/* Decrements with every call */
		priv->test_i2--;
		return 0;
	}

	return -ENOENT;
}

int sysinfo_sandbox_get_str(struct udevice *dev, int id, size_t size, char *val)
{
	struct sysinfo_sandbox_priv *priv = dev_get_priv(dev);
	int i1 = priv->test_i1;
	int i2 = priv->test_i2;
	int index = (i1 * i2) % ARRAY_SIZE(vacation_spots);

	switch (id) {
	case STR_VACATIONSPOT:
		/* Picks a vacation spot depending on i1 and i2 */
		snprintf(val, size, vacation_spots[index]);
		return 0;
	}

	return -ENOENT;
}

/* Two memory slots, one of them populated, for the SMBIOS memory tables */
static const struct memory_array_info sandbox_marray = {
	.max_capacity = 16ULL << 30,
	.num_devices = 2,
	.location = SMBIOS_MA_LOCATION_MOTHERBOARD,
	.use = SMBIOS_MA_USE_SYSTEM,
	.err_corr = SMBIOS_MA_ERRCORR_NONE,
};

static const struct memory_dev_info sandbox_mdevs[] = {
	{
		.size = 8ULL << 30,
		.speed = 4800,
		.config_speed = 4400,
		.total_width = 64,
		.data_width = 64,
		.min_voltage = 1100,
		.max_voltage = 1100,
		.config_voltage = 1100,
		.type_detail = SMBIOS_MD_TD_SYNC,
		.module_man_id = 0x2c80,
		.form_factor = SMBIOS_MD_FF_SODIMM,
		.mem_type = SMBIOS_MD_TYPE_DDR5,
		.ranks = 1,
		.dev_locator = "DIMM 0",
		.bank_locator = "BANK 0",
		.manufacturer = "Sandbox Memory",
		.part_number = "SB-8G-4800",
		.serial = "00000001",
	}, {
		.total_width = 0xffff,
		.data_width = 0xffff,
		.type_detail = SMBIOS_MD_TD_UNKNOWN,
		.form_factor = SMBIOS_MD_FF_UNKNOWN,
		.mem_type = SMBIOS_MD_TYPE_UNKNOWN,
		.dev_locator = "DIMM 1",
		.bank_locator = "BANK 0",
	},
};

int sysinfo_sandbox_get_data(struct udevice *dev, int id, void **buf,
			     size_t *size)
{
	struct sysinfo_sandbox_priv *priv = dev_get_priv(dev);

	switch (id) {
	case DATA_TEST:
		*buf = priv->test_data;
		*size = sizeof(priv->test_data);
		return 0;
	case SYSID_SM_MEMARRAY_INFO:
		*buf = (void *)&sandbox_marray;
		*size = sizeof(sandbox_marray);
		return 0;
	case SYSID_SM_MEMDEV_INFO:
		*buf = (void *)sandbox_mdevs;
		*size = sizeof(sandbox_mdevs);
		return 0;
	}

	return -ENOENT;
}

static int sysinfo_sandbox_get_item_count(struct udevice *dev, int id)
{
	if (id == SYSID_SM_MEMDEV_INFO)
		return ARRAY_SIZE(sandbox_mdevs);

	return -ENOENT;
}

static const struct udevice_id sysinfo_sandbox_ids[] = {
	{ .compatible = "sandbox,sysinfo-sandbox" },
	{ /* sentinel */ }
};

static const struct sysinfo_ops sysinfo_sandbox_ops = {
	.detect = sysinfo_sandbox_detect,
	.get_bool = sysinfo_sandbox_get_bool,
	.get_int = sysinfo_sandbox_get_int,
	.get_str = sysinfo_sandbox_get_str,
	.get_data = sysinfo_sandbox_get_data,
	.get_item_count = sysinfo_sandbox_get_item_count,
};

int sysinfo_sandbox_probe(struct udevice *dev)
{
	return 0;
}

U_BOOT_DRIVER(sysinfo_sandbox) = {
	.name           = "sysinfo_sandbox",
	.id             = UCLASS_SYSINFO,
	.of_match       = sysinfo_sandbox_ids,
	.ops		= &sysinfo_sandbox_ops,
	.priv_auto	= sizeof(struct sysinfo_sandbox_priv),
	.probe          = sysinfo_sandbox_probe,
};
