// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for QEMU qfw
 *
 * Copyright 2023 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_BOOTSTD

#include <abuf.h>
#include <command.h>
#include <bootdev.h>
#include <bootflow.h>
#include <bootm.h>
#include <bootmeth.h>
#include <env.h>
#include <mapmem.h>
#include <qfw.h>
#include <dm.h>

static int qfw_check(struct udevice *dev, struct bootflow_iter *iter)
{
	const struct udevice *media = dev_get_parent(iter->dev);
	enum uclass_id id = device_get_uclass_id(media);

	log_debug("media=%s\n", media->name);
	if (id == UCLASS_QFW)
		return 0;

	return -ENOTSUPP;
}

static int qfw_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct udevice *qfw_dev = dev_get_parent(bflow->dev);
	ulong setup, kern, ramdisk, setup_addr;
	size_t cmdline_size;
	struct abuf cmdline;
	int ret;

	/* Get the size of each region */
	ret = qemu_fwcfg_read_info(qfw_dev, &setup, &kern, &ramdisk, &cmdline,
				   &setup_addr);
	if (ret)
		return log_msg_ret("qri", ret);
	bflow->cmdline = abuf_uninit_move(&cmdline, &cmdline_size);

	bflow->name = strdup("qfw");
	if (!bflow->name)
		return log_msg_ret("name", -ENOMEM);

	/*
	 * create images for each; only cmdline has the actual data; the others
	 * only have a size for now, since the data has yet not been read
	 */
	if (!bootflow_img_add(bflow, "setup",
			      (enum bootflow_img_t)IH_TYPE_X86_SETUP,
			      setup_addr, setup))
		return log_msg_ret("cri", -ENOMEM);
	if (!bootflow_img_add(bflow, "kernel",
			      (enum bootflow_img_t)IH_TYPE_KERNEL, 0, kern))
		return log_msg_ret("qrk", -ENOMEM);
	if (ramdisk && !bootflow_img_add(bflow, "ramdisk",
					 (enum bootflow_img_t)IH_TYPE_RAMDISK,
					 0, ramdisk))
		return log_msg_ret("qrr", -ENOMEM);
	if (!bootflow_img_add(bflow, "cmdline", BFI_CMDLINE,
			      map_to_sysmem(bflow->cmdline), cmdline_size))
		return log_msg_ret("qrc", -ENOMEM);
	bflow->state = BOOTFLOWST_READY;

	return 0;
}

static int qfw_read_files(struct udevice *dev, struct bootflow *bflow,
			  bool re_read, const struct bootflow_img **simgp,
			  const struct bootflow_img **kimgp,
			  const struct bootflow_img **rimgp)
{
	struct udevice *qfw_dev = dev_get_parent(bflow->dev);
	struct bootflow_img *kimg, *rimg;
	struct abuf setup, kern, ramdisk;
	const struct bootflow_img *simg;

	simg = bootflow_img_find(bflow, (enum bootflow_img_t)IH_TYPE_X86_SETUP);
	kimg = bootflow_img_findw(bflow, (enum bootflow_img_t)IH_TYPE_KERNEL);
	rimg = bootflow_img_findw(bflow, (enum bootflow_img_t)IH_TYPE_RAMDISK);
	if (!kimg)
		return log_msg_ret("qfs", -EINVAL);

	/* read files only if not already read */
	if (re_read || !kimg->addr) {
		abuf_init_const_addr(&setup, simg ? simg->addr : 0,
				     simg ? simg->size : 0);
		abuf_init_const_addr(&kern, env_get_hex("kernel_addr_r", 0),
				     kimg->size);
		abuf_init_const_addr(&ramdisk, env_get_hex("ramdisk_addr_r", 0),
				     rimg ? rimg->size : 0);

		qemu_fwcfg_read_files(qfw_dev, &setup, &kern, &ramdisk);
		kimg->addr = abuf_addr(&kern);
		if (rimg)
			rimg->addr = abuf_addr(&ramdisk);
	}

	if (simgp)
		*simgp = simg;
	if (kimgp)
		*kimgp = kimg;
	if (rimgp)
		*rimgp = rimg;

	return 0;
}

static int qfw_read_file(struct udevice *dev, struct bootflow *bflow,
			 const char *file_path, ulong *addrp, ulong align,
			 enum bootflow_img_t type, ulong *sizep)
{
	return -ENOSYS;
}

#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
static int qfw_read_all(struct udevice *dev, struct bootflow *bflow)
{
	struct bootflow_img *kimg;
	int ret;

	kimg = bootflow_img_findw(bflow, (enum bootflow_img_t)IH_TYPE_KERNEL);
	if (!kimg)
		return log_msg_ret("qra", -ENOENT);

	ret = qfw_read_files(dev, bflow, true, NULL, NULL, NULL);
	if (ret)
		return log_msg_ret("qrA", ret);

	return 0;
}
#endif

static int qfw_boot(struct udevice *dev, struct bootflow *bflow)
{
	const struct bootflow_img *simg, *kimg, *rimg;
	char conf_fdt[BOOTM_STRLEN], addr_img[BOOTM_STRLEN], conf_ramdisk[40];
	struct bootm_info bmi;
	int ret;

	/* read the files if not already done */
	ret = qfw_read_files(dev, bflow, false, &simg, &kimg, &rimg);
	if (!kimg)
		return log_msg_ret("qkf", -EINVAL);

	ret = booti_run(&bmi);
	bootm_init(&bmi);
	bootm_set_conf_fdt(&bmi, map_to_sysmem(gd->fdt_blob), conf_fdt);
	bootm_set_addr_img(&bmi, kimg->addr, addr_img);
	snprintf(conf_ramdisk, sizeof(conf_ramdisk), "%lx:%lx", rimg->addr,
		 rimg->size);
	bmi.conf_ramdisk = conf_ramdisk;

	ret = -ENOENT;
	if (IS_ENABLED(CONFIG_LIB_BOOTI))
		ret = booti_run(&bmi);
	if (ret && IS_ENABLED(CONFIG_LIB_BOOTZ))
		ret = bootz_run(&bmi);
	if (ret && IS_ENABLED(CONFIG_ZBOOT) && simg) {
		ret = zboot_run_args(kimg->addr, kimg->size,
				     rimg->addr, rimg->size, simg->addr,
				     *bflow->cmdline ? bflow->cmdline : NULL);
	}

	return ret ? -EIO : 0;
}

static int qfw_bootmeth_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = "QEMU boot using firmware interface";

	return 0;
}

static struct bootmeth_ops qfw_bootmeth_ops = {
	.check		= qfw_check,
	.read_bootflow	= qfw_read_bootflow,
	.read_file	= qfw_read_file,
#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
	.read_all	= qfw_read_all,
#endif
	.boot		= qfw_boot,
};

static const struct udevice_id qfw_bootmeth_ids[] = {
	{ .compatible = "u-boot,qfw-bootmeth" },
	{ }
};

U_BOOT_DRIVER(bootmeth_qfw) = {
	.name		= "bootmeth_qfw",
	.id		= UCLASS_BOOTMETH,
	.of_match	= qfw_bootmeth_ids,
	.ops		= &qfw_bootmeth_ops,
	.bind		= qfw_bootmeth_bind,
};
