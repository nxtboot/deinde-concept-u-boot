// SPDX-License-Identifier: GPL-2.0+

/*
 * SPL loader for SPI NAND devices using the MTD subsystem.
 *
 * Based on spl_spi.c, which is:
 *
 * Copyright (C) 2011 OMICRON electronics GmbH
 *
 * based on drivers/mtd/nand/raw/nand_spl_load.c
 *
 * Copyright (C) 2011
 * Heiko Schocher, DENX Software Engineering, hs@denx.de.
 */

#include <config.h>
#include <image.h>
#include <log.h>
#include <errno.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <dm/ofnode.h>
#include <dm/uclass.h>
#include <mtd.h>

static struct mtd_info *spl_spi_nand_get_mtd(void)
{
	struct udevice *dev;
	int ret;

	for (ret = uclass_first_device_err(UCLASS_MTD, &dev);
	     dev;
	     ret = uclass_next_device_err(&dev)) {
		if (ret)
			continue;
		if (device_is_compatible(dev, "spi-nand"))
			return dev_get_uclass_priv(dev);
	}

	return NULL;
}

static ulong spl_spinand_fit_read(struct spl_load_info *load, ulong offs,
				  ulong size, void *buf)
{
	struct mtd_info *mtd = load->priv;
	size_t retlen = 0;
	int ret;

	ret = mtd_read(mtd, offs, size, &retlen, buf);
	if (ret && ret != -EUCLEAN) {
		printf("SPI NAND read failed offs=0x%lx size=0x%lx ret=%d\n",
		       offs, size, ret);
		return 0;
	}
	if (retlen != size)
		return 0;

	return retlen;
}

static int spl_spinand_load_image(struct spl_image_info *spl_image,
				  struct spl_boot_device *bootdev)
{
	struct spl_load_info load;
	struct mtd_info *mtd;

	mtd = spl_spi_nand_get_mtd();
	if (!mtd) {
		puts("SPI NAND probe failed.\n");
		return -ENODEV;
	}

	spl_load_init(&load, spl_spinand_fit_read, mtd, 1);

	return spl_load(spl_image, bootdev, &load, 0, CONFIG_SYS_SPI_U_BOOT_OFFS);
}

/* Use priority 1 so that boards can override this */
SPL_LOAD_IMAGE_METHOD("SPI NAND", 1, BOOT_DEVICE_SPI, spl_spinand_load_image);
