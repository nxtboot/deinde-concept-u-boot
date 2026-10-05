// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for extlinux boot from a block device
 *
 * Copyright 2021 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_BOOTSTD

#include <asm/cache.h>
#include <bootdev.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <bootstd.h>
#include <command.h>
#include <dm.h>
#include <extlinux.h>
#include <fs_legacy.h>
#include <luks.h>
#include <malloc.h>
#include <mapmem.h>
#include <mmc.h>
#include <part.h>
#include <pxe_utils.h>

static int extlinux_get_state_desc(struct udevice *dev, char *buf, int maxsize)
{
	if (IS_ENABLED(CONFIG_SANDBOX)) {
		int len;

		len = snprintf(buf, maxsize, "OK");

		return len + 1 < maxsize ? 0 : -ENOSPC;
	}

	return 0;
}

static int extlinux_getfile(struct pxe_context *ctx, const char *file_path,
			    ulong *addrp, ulong align, enum bootflow_img_t type,
			    ulong *sizep)
{
	struct extlinux_info *info = ctx->userdata;
	int ret;

	/* Allow up to 1GB */
	*sizep = 1 << 30;
	ret = bootmeth_read_file(info->dev, info->bflow, file_path, addrp,
				 align, type, sizep);
	if (ret)
		return log_msg_ret("read", ret);

	return 0;
}

static int extlinux_check(struct udevice *dev, struct bootflow_iter *iter)
{
	int ret;

	/* This only works on block devices */
	ret = bootflow_iter_check_blk(iter);
	if (ret)
		return log_msg_ret("blk", ret);

	return 0;
}

/**
 * extlinux_check_luks() - Check for LUKS encryption on other partitions
 *
 * This scans all partitions on the same device to check for LUKS encryption.
 * If found, it marks this bootflow as encrypted since it likely boots from
 * an encrypted root partition.
 *
 * @bflow: Bootflow to potentially mark as encrypted
 * Return: 0 on success, -ve on error
 */
static int extlinux_check_luks(struct bootflow *bflow)
{
	struct blk_desc *desc;
	struct disk_partition info;
	int ret, part;

	if (!IS_ENABLED(CONFIG_BLK_LUKS) || !bflow->blk)
		return 0;

	desc = dev_get_uclass_plat(bflow->blk);
	if (!desc || !desc->bdev)
		return 0;

	/*
	 * Check all partitions on this device for LUKS encryption.
	 * Typically partition 1 has the bootloader files and partition 2
	 * has the encrypted root filesystem. Check up to 10 partitions.
	 */
	for (part = 1; part <= 10; part++) {
		ret = part_get_info(desc, part, &info);
		if (ret)
			continue;  /* Partition doesn't exist */

		ret = luks_detect(desc->bdev, &info);
		if (!ret) {
			int luks_ver = luks_get_version(desc->bdev, &info);

			log_debug("LUKS partition %d detected (v%d), marking bootflow as encrypted\n",
				  part, luks_ver);
			bflow->flags |= BOOTFLOWF_ENCRYPTED;
			bflow->luks_version = luks_ver;
			return 0;
		}
	}

	return 0;
}

/**
 * extlinux_parse_config() - Parse the extlinux config and cache the result
 *
 * Parses the configuration file including any include directives, and caches
 * the result in plat->ctx.cfg. The filesystem must still be accessible for
 * loading includes.
 *
 * @dev: Bootmeth device (needed for file-loading callback)
 * @bflow: Bootflow containing the config buffer
 * Return: 0 if OK, -ve on error
 */
static int extlinux_parse_config(struct udevice *dev, struct bootflow *bflow,
				 struct pxe_context *ctx)
{
	struct extlinux_plat *plat = dev_get_plat(dev);
	struct abuf buf;
	ulong addr;
	int ret;

	plat->info.dev = dev;
	plat->info.bflow = bflow;
	ret = pxe_setup_ctx(ctx, extlinux_getfile, &plat->info, true,
			    bflow->fname, false, plat->use_fallback, bflow);
	if (ret)
		return log_msg_ret("ctx", ret);
	ctx->quiet = true;
	ctx->pxe_file_size = bflow->size;

	addr = map_to_sysmem(bflow->buf);
	abuf_init_addr(&buf, addr, bflow->size);
	ctx->cfg = parse_pxefile(ctx, &buf);
	if (!ctx->cfg) {
		pxe_destroy_ctx(ctx);
		return log_msg_ret("prs", -EINVAL);
	}

	ret = pxe_process_includes(ctx, ctx->cfg, addr);
	if (ret) {
		pxe_menu_uninit(ctx->cfg);
		ctx->cfg = NULL;
		pxe_destroy_ctx(ctx);
		return log_msg_ret("inc", ret);
	}

	return 0;
}

/**
 * extlinux_fill_info() - Decode the extlinux file to find out its info
 *
 * On the first call (entry 0), calls extlinux_parse_config() to parse
 * into a context from the alist. For entry > 0, reuses the cached
 * context.
 *
 * @dev: Bootmeth device (needed for file-loading callback)
 * @bflow: Bootflow to process (entry selects which label)
 * Return: 0 if OK, -ENOENT if entry index exceeds available labels, other
 * -ve on error
 */
static int extlinux_fill_info(struct udevice *dev, struct bootflow *bflow)
{
	struct extlinux_priv *priv = dev_get_priv(dev);
	struct pxe_context *ctx;
	struct pxe_label *label;
	const char *name;
	int i;

	log_debug("parsing bflow file size %x entry %d\n", bflow->size,
		  bflow->entry);

	ctx = extlinux_get_ctx(priv, bflow);
	if (!ctx)
		return log_msg_ret("ctx", -ENOMEM);

	/* Parse the config on first entry; reuse the cached result after */
	if (!ctx->cfg) {
		int ret;

		ret = extlinux_parse_config(dev, bflow, ctx);
		if (ret)
			return log_msg_ret("prs", ret);
	}

	/* Walk to the requested label */
	i = 0;
	list_for_each_entry(label, &ctx->cfg->labels, list) {
		if (i == bflow->entry)
			goto found;
		i++;
	}

	return -ENOENT;

found:
	name = label->menu ? label->menu : label->name;
	if (name) {
		bflow->os_name = strdup(name);
		if (!bflow->os_name)
			return log_msg_ret("osn", -ENOMEM);
	}
	if (label->name) {
		bflow->entry_name = strdup(label->name);
		if (!bflow->entry_name)
			return log_msg_ret("ent", -ENOMEM);
	}

	return 0;
}

static int extlinux_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct blk_desc *desc;
	const char *const *prefixes;
	struct udevice *bootstd;
	const char *prefix;
	loff_t size;
	int ret, i;

	log_debug("starting part %d\n", bflow->part);
	ret = uclass_first_device_err(UCLASS_BOOTSTD, &bootstd);
	if (ret) {
		log_debug("no bootstd\n");
		return log_msg_ret("std", ret);
	}

	/* If a block device, we require a partition table */
	if (bflow->blk && !bflow->part) {
		log_debug("no partition table\n");
		return -ENOENT;
	}

	prefixes = bootstd_get_prefixes(bootstd);
	i = 0;
	desc = bflow->blk ? dev_get_uclass_plat(bflow->blk) : NULL;
	do {
		prefix = prefixes ? prefixes[i] : NULL;

		log_debug("try prefix %s\n", prefix);
		ret = bootmeth_try_file(bflow, desc, prefix, EXTLINUX_FNAME);
	} while (ret && prefixes && prefixes[++i]);
	if (ret) {
		log_debug("no file found\n");
		return log_msg_ret("try", ret);
	}
	size = bflow->size;

	ret = bootmeth_alloc_file(bflow, 0x10000, ARCH_DMA_MINALIGN,
				  BFI_EXTLINUX_CFG);
	if (ret)
		return log_msg_ret("read", ret);

	ret = extlinux_fill_info(dev, bflow);
	if (ret)
		return log_msg_ret("inf", ret);

	ret = extlinux_check_luks(bflow);
	if (ret)
		return log_msg_ret("luks", ret);

	return 0;
}

static int extlinux_local_boot(struct udevice *dev, struct bootflow *bflow)
{
	struct extlinux_priv *priv = dev_get_priv(dev);
	struct pxe_context *ctx;

	ctx = extlinux_get_ctx(priv, bflow);
	if (!ctx)
		return log_msg_ret("ctx", -ENOMEM);

	return extlinux_boot(dev, bflow, ctx, extlinux_getfile, true,
			     bflow->fname, false);
}

#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
static int extlinux_local_read_all(struct udevice *dev, struct bootflow *bflow)
{
	struct extlinux_priv *priv = dev_get_priv(dev);
	struct pxe_context *ctx;

	ctx = extlinux_get_ctx(priv, bflow);
	if (!ctx)
		return log_msg_ret("ctx", -ENOMEM);

	return extlinux_read_all(dev, bflow, ctx, extlinux_getfile,
				 true, bflow->fname);
}
#endif

int extlinux_bootmeth_probe(struct udevice *dev)
{
	struct extlinux_priv *priv = dev_get_priv(dev);

	alist_init_struct(&priv->ctxs, struct pxe_context);

	return 0;
}

int extlinux_bootmeth_remove(struct udevice *dev)
{
	struct extlinux_priv *priv = dev_get_priv(dev);
	struct pxe_context *ctx;

	alist_for_each(ctx, &priv->ctxs) {
		if (ctx->cfg)
			pxe_menu_uninit(ctx->cfg);
		pxe_destroy_ctx(ctx);
	}
	alist_uninit(&priv->ctxs);

	return 0;
}

static int extlinux_bootmeth_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = IS_ENABLED(CONFIG_BOOTSTD_FULL) ?
		"Extlinux boot from a block device" : "extlinux";
	plat->flags = BOOTMETHF_MULTI;

	return 0;
}

static struct bootmeth_ops extlinux_bootmeth_ops = {
	.get_state_desc	= extlinux_get_state_desc,
	.check		= extlinux_check,
	.read_bootflow	= extlinux_read_bootflow,
	.read_file	= bootmeth_common_read_file,
	.boot		= extlinux_local_boot,
	.set_property	= extlinux_set_property,
#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
	.read_all	= extlinux_local_read_all,
#endif
};

static const struct udevice_id extlinux_bootmeth_ids[] = {
	{ .compatible = "u-boot,extlinux" },
	{ }
};

/* Put a number before 'extlinux' to provide a default ordering */
U_BOOT_DRIVER(bootmeth_1extlinux) = {
	.name		= "bootmeth_extlinux",
	.id		= UCLASS_BOOTMETH,
	.of_match	= extlinux_bootmeth_ids,
	.ops		= &extlinux_bootmeth_ops,
	.bind		= extlinux_bootmeth_bind,
	.probe		= extlinux_bootmeth_probe,
	.remove		= extlinux_bootmeth_remove,
	.plat_auto	= sizeof(struct extlinux_plat),
	.priv_auto	= sizeof(struct extlinux_priv),
};
