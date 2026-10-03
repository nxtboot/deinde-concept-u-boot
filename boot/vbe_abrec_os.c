// SPDX-License-Identifier: GPL-2.0+
/*
 * Verified Boot for Embedded (VBE) 'abrec' method (for OS)
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_BOOT

#include <bootm.h>
#include <bootmeth.h>
#include <dm.h>
#include <extlinux.h>
#include <fs_legacy.h>
#include <memalign.h>
#include <mmc.h>
#include <dm/ofnode.h>
#include "vbe_abrec.h"

static const char *const pick_names[VBEP_COUNT] = {"a", "b", "recovery"};

static int vbe_abrec_read_check(struct udevice *dev, struct bootflow_iter *iter)
{
	int ret;

	/* This only works on block devices */
	ret = bootflow_iter_check_blk(iter);
	if (ret)
		return log_msg_ret("blk", ret);

	return 0;
}

static enum vbe_pick_t find_pick(const char *name)
{
	int i;

	for (i = 0; i < VBEP_COUNT; i++)
		if (!strcmp(pick_names[i], name))
			return i;

	return -1;
}

static int vbe_abrec_getfile(struct pxe_context *ctx, const char *file_path,
			     ulong *addrp, ulong align,
			     enum bootflow_img_t type, ulong *sizep)
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

static int decode_state(struct vbe_bflow_priv *priv, oftree tree)
{
	ofnode root, next, osn;
	const char *slot;
	int ret;

	if (!oftree_valid(tree))
		return log_msg_ret("vtr", -ENOENT);

	root = oftree_root(tree);
	if (strcmp("vbe,abrec-state", ofnode_read_string(root, "compatible")))
		return log_msg_ret("vco", -ENOENT);

	osn = ofnode_find_subnode(root, "os");
	if (!oftree_valid(tree))
		return log_msg_ret("vos", -ENOENT);

	next = ofnode_find_subnode(osn, "next-boot");
	if (!oftree_valid(tree))
		return log_msg_ret("vnn", -ENOENT);

	slot = ofnode_read_string(next, "slot");
	if (!slot)
		return log_msg_ret("vnn", -ENOENT);

	ret = find_pick(slot);
	if (ret == -1)
		return log_msg_ret("vsl", -EINVAL);
	priv->pick_slot = ret;

	return 0;
}

static int vbe_abrec_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct abrec_priv *priv = dev_get_priv(dev);
	struct vbe_bflow_priv *bfpriv;
	struct blk_desc *desc;
	struct abuf buf, oem;
	char subdir[20];
	oftree tree;
	int ret;

	// log_debug("part %d\n", bflow->part);
	/* we expect a boot partition; for now we assume it is partition 2 */
	if (bflow->part != 2) {
		// log_debug("wrong partition\n");
		return -ENOENT;
	}
	bflow->state = BOOTFLOWST_FS;

	desc = dev_get_uclass_plat(bflow->blk);

	bflow->subdir = strdup("");
	if (!bflow->subdir)
		return log_msg_ret("bss", -ENOMEM);

	ret = bootmeth_alloc_other(bflow, VBE_STATE_FNAME, BFI_VBE_STATE, &buf);
	if (ret)
		return log_msg_ret("bst", ret);
	if (!buf.size) {
		ret = log_msg_ret("bst", -ENOENT);
		goto err_buf;
	}

	bflow->state = BOOTFLOWST_FILE;

	bfpriv = calloc(1, sizeof(struct vbe_bflow_priv));
	if (!bfpriv) {
		ret = log_msg_ret("val", -ENOMEM);
		goto err_priv;
	}
	bflow->bootmeth_priv = bfpriv;

	tree = oftree_from_fdt(buf.data);
	if (!oftree_valid(tree)) {
		ret = log_msg_ret("vtr", -ENOENT);
		goto err_tree;
	}

	ret = decode_state(bfpriv, tree);
	oftree_dispose(tree);
	if (ret) {
		ret = log_msg_ret("vds", ret);
		goto err_decode;
	}

	printf("VBE: Picked slot %s\n", pick_names[bfpriv->pick_slot]);
	ret = bootmeth_setup_fs(bflow, desc);
	if (ret) {
		ret = log_msg_ret("vsf", ret);
		goto err_fs;
	}

	snprintf(subdir, sizeof(subdir), "%s/", pick_names[bfpriv->pick_slot]);
	free(bflow->subdir);
	bflow->subdir = strdup(subdir);
	if (!bflow->subdir) {
		ret = log_msg_ret("vsd", -ENOMEM);
		if (ret)
			goto err_fs;
	}

	ret = bootmeth_try_file(bflow, desc, subdir, EXTLINUX_FNAME);
	if (ret) {
		log_debug("part %d: ret %d\n", bflow->part, ret);
		ret = log_msg_ret("vtr", ret);
		goto err_file;
	}

	ret = bootmeth_alloc_file(bflow, 0x10000, ARCH_DMA_MINALIGN,
				  BFI_EXTLINUX_CFG);
	if (ret) {
		ret = log_msg_ret("vaf", ret);
		goto err_file;
	}

	if (priv->oem_devicetree) {
		/* Locate the OEM FIT in the same slot, if it exists */
		ret = bootmeth_alloc_other(bflow, VBE_OEM_FIT_FNAME, BFI_VBE_OEM_FIT,
					&oem);
		if (ret == -ENOMEM) {
			ret = log_msg_ret("bst", ret);
			goto err_file;
		}
	}

	bflow->state = BOOTFLOWST_READY;

	return 0;

err_file:
err_fs:
err_decode:
err_tree:
	free(priv);
err_priv:
err_buf:
	abuf_uninit(&buf);

	return ret;
}

static int vbe_abrec_boot(struct udevice *dev, struct bootflow *bflow)
{
	struct abrec_priv *priv = dev_get_priv(dev);
	const struct bootflow_img *img;
	int ret;

	/* load the devicetree first */
	img = bootflow_img_find(bflow, BFI_VBE_OEM_FIT);
	if (img) {
		struct bootm_info bmi;
		char addr_str[BOOTM_STRLEN];
		int states;

		printf("Loading OEM devicetree from FIT\n");
		bootm_init(&bmi);
		bootm_set_addr_img(&bmi, img->addr, addr_str);
		bmi.cmd_name = "vbe_os";
		states = BOOTM_STATE_START | BOOTM_STATE_FINDOS |
			BOOTM_STATE_PRE_LOAD | BOOTM_STATE_FINDOTHER |
			BOOTM_STATE_LOADOS;
		ret = bootm_run_states(&bmi, states);

		/* we should get -ENOPKG, indicating that there was no OS */
		if (ret != -ENOPKG)
			return log_msg_ret("vab", ret ? ret : -EFAULT);
	}

	printf("Loading OS FIT%s\n", img ? " keeping existing FDT" : "");

	return extlinux_boot(dev, bflow, &priv->ctx, vbe_abrec_getfile, true,
			     bflow->fname, img);
}

#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
static int vbe_abrec_read_all(struct udevice *dev, struct bootflow *bflow)
{
	struct abrec_priv *priv = dev_get_priv(dev);

	return extlinux_read_all(dev, bflow, &priv->ctx, vbe_abrec_getfile,
				 true, bflow->fname);
}
#endif

static struct bootmeth_ops bootmeth_vbe_abrec_os_ops = {
	.check		= vbe_abrec_read_check,
	.read_file	= bootmeth_common_read_file,
	.read_bootflow	= vbe_abrec_read_bootflow,
	.boot		= vbe_abrec_boot,
#if IS_ENABLED(CONFIG_BOOTSTD_FULL)
	.read_all	= vbe_abrec_read_all,
#endif
};

static int bootmeth_vbe_abrec_os_probe(struct udevice *dev)
{
	struct abrec_priv *priv = dev_get_priv(dev);

	priv->oem_devicetree = true;

	return 0;
}

static int bootmeth_vbe_abrec_os_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = "VBE A/B/recovery for OS";

	return 0;
}

#if IS_ENABLED(CONFIG_OF_REAL)
static const struct udevice_id vbe_abrec_os_ids[] = {
	{ .compatible = "vbe,abrec-os" },
	{ }
};
#endif

U_BOOT_DRIVER(vbe_abrec_os) = {
	.name	= "vbe_abrec_os",
	.id	= UCLASS_BOOTMETH,
	.of_match = of_match_ptr(vbe_abrec_os_ids),
	.ops	= &bootmeth_vbe_abrec_os_ops,
	.bind	= bootmeth_vbe_abrec_os_bind,
	.probe	= bootmeth_vbe_abrec_os_probe,
	.priv_auto	= sizeof(struct abrec_priv),
	.plat_auto	= sizeof(struct extlinux_plat),
};
