// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for Boot Loader Specification (BLS) Type #1
 *
 * Copyright 2026 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * This implements support for BLS Type #1 entries as defined in:
 * https://uapi-group.org/specifications/specs/boot_loader_specification/
 *
 * Supported features:
 * - Scans loader/entries/ directory for .conf files
 * - Falls back to single loader/entry.conf if no entries/ directory
 * - Fields: title, version, linux, options, initrd, devicetree
 * - Multiple options lines (concatenated with spaces)
 * - Multiple initrd lines (only first used, PXE limitation)
 * - FITs with #config syntax in linux field
 * - Zero-copy parsing (fields point into bootflow buffer)
 *
 * Current limitations:
 * - Only the first entry file in loader/entries/ is used
 * - Only first initrd used (PXE infrastructure supports one)
 * - No devicetree-overlay support
 * - No architecture/machine-id filtering
 * - No version-based sorting
 * - No UKI/EFI support (Type #2)
 */

#define LOG_CATEGORY UCLASS_BOOTSTD

#include <asm/cache.h>
#include <bls.h>
#include <bootdev.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <bootstd.h>
#include <dm.h>
#include <env.h>
#include <fs_common.h>
#include <fs_legacy.h>
#include <malloc.h>
#include <mapmem.h>
#include <part.h>
#include <pxe_utils.h>
#include <linux/string.h>

/* BLS entry directory and fallback single file */
#define BLS_ENTRIES_DIR		"loader/entries"
#define BLS_ENTRY_FILE		"loader/entry.conf"

/*
 * Minimum partition size, in blocks, that is plausibly a filesystem.
 * Partitions below this are skipped before FS probing to avoid
 * out-of-bounds superblock reads on raw data slots
 */
#define BLS_MIN_PART_BLOCKS	64

/**
 * struct bls_info - context information for BLS getfile callback
 *
 * @dev: Bootmethod device being used to boot
 * @bflow: Bootflow being booted
 */
struct bls_info {
	struct udevice *dev;
	struct bootflow *bflow;
};

static int bls_get_state_desc(struct udevice *dev, char *buf, int maxsize)
{
	if (IS_ENABLED(CONFIG_SANDBOX)) {
		int len;

		len = snprintf(buf, maxsize, "OK");

		return len + 1 < maxsize ? 0 : -ENOSPC;
	}

	return 0;
}

static int bls_getfile(struct pxe_context *ctx, const char *file_path,
		       ulong *addrp, ulong align, enum bootflow_img_t type,
		       ulong *sizep)
{
	struct bls_info *info = ctx->userdata;
	int ret;

	/* Allow up to 1GB */
	*sizep = 1 << 30;
	ret = bootmeth_read_file(info->dev, info->bflow, file_path, addrp,
				 align, type, sizep);
	if (ret)
		return log_msg_ret("read", ret);

	return 0;
}

static int bls_check(struct udevice *dev, struct bootflow_iter *iter)
{
	int ret;

	/* This only works on block devices */
	ret = bootflow_iter_check_blk(iter);
	if (ret)
		return log_msg_ret("blk", ret);

	return 0;
}

/**
 * bls_to_pxe_label() - Convert bootflow to PXE label for boot execution
 *
 * @bflow: Bootflow containing BLS entry and discovered images
 * @labelp: Returns allocated PXE label structure
 * Return: 0 on success, -ENOMEM if out of memory
 */
static int bls_to_pxe_label(struct bootflow *bflow,
			    struct pxe_label **labelp)
{
	struct pxe_label *label;
	struct bootflow_img *img;
	int ret;

	label = calloc(1, sizeof(*label));
	if (!label)
		return log_msg_ret("alloc", -ENOMEM);

	INIT_LIST_HEAD(&label->list);
	alist_init_struct(&label->files, struct pxe_file);
	alist_init_struct(&label->initrds, char *);

	label->name = strdup("");
	label->menu = strdup(bflow->os_name ?: "");
	label->append = strdup(bflow->cmdline ?: "");
	if (!label->name || !label->menu || !label->append) {
		ret = -ENOMEM;
		goto err;
	}

	/* Extract kernel, initrds and FDT from the bootflow images */
	alist_for_each(img, &bflow->images) {
		char *fname;

		fname = strdup(img->fname);
		if (!fname) {
			ret = -ENOMEM;
			goto err;
		}

		switch ((int)img->type) {
		case IH_TYPE_KERNEL:
			if (!label->kernel) {
				label->kernel = fname;
				label->kernel_label = strdup(fname);
			} else {
				free(fname);
			}
			break;
		case IH_TYPE_RAMDISK:
			if (!alist_add(&label->initrds, fname)) {
				free(fname);
				ret = -ENOMEM;
				goto err;
			}
			break;
		case IH_TYPE_FLATDT:
			if (!label->fdt)
				label->fdt = fname;
			else
				free(fname);
			break;
		default:
			free(fname);
			break;
		}
	}

	*labelp = label;
	return 0;

err:
	label_destroy(label);
	return ret;
}

/**
 * bls_entry_init() - Parse entry and register images with bootflow
 *
 * @entry: Entry structure to initialize
 * @bflow: Bootflow to populate
 * @size: Size of BLS entry file in bflow->buf
 * Return: 0 on success, -ve on error
 */
static int bls_entry_init(struct bls_entry *entry, struct bootflow *bflow,
			  loff_t size)
{
	char **initrd;
	int ret;

	/* Parse BLS entry (fields point into bflow->buf) */
	ret = bls_parse_entry(bflow->buf, size, entry);
	if (ret)
		return log_msg_ret("parse", ret);

	/* Save title as os_name */
	if (entry->title) {
		bflow->os_name = strdup(entry->title);
		if (!bflow->os_name)
			return log_msg_ret("name", -ENOMEM);
	}

	/* Transfer cmdline ownership to bflow */
	if (entry->options) {
		bflow->cmdline = entry->options;
		entry->options = NULL;
	}

	/* Register discovered images (not yet loaded, addr=0) */
	if (entry->fit) {
		if (!bootflow_img_add(bflow, entry->fit,
				      (enum bootflow_img_t)IH_TYPE_KERNEL,
				      0, 0))
			return log_msg_ret("imf", -ENOMEM);
	} else if (entry->kernel) {
		if (!bootflow_img_add(bflow, entry->kernel,
				      (enum bootflow_img_t)IH_TYPE_KERNEL,
				      0, 0))
			return log_msg_ret("imk", -ENOMEM);
	}

	alist_for_each(initrd, &entry->initrds) {
		if (!bootflow_img_add(bflow, *initrd,
				      (enum bootflow_img_t)IH_TYPE_RAMDISK,
				      0, 0))
			return log_msg_ret("imi", -ENOMEM);
	}

	if (entry->devicetree) {
		if (!bootflow_img_add(bflow, entry->devicetree,
				      (enum bootflow_img_t)IH_TYPE_FLATDT,
				      0, 0))
			return log_msg_ret("imf", -ENOMEM);
	}

	return 0;
}

/**
 * bls_scan_entries_dir() - Scan loader/entries/ for a .conf file
 *
 * Looks for the Nth .conf file in the BLS entries directory, where N is
 * given by @entry. The filesystem must already be set up for the partition.
 *
 * @prefix: Prefix to prepend to the directory path (e.g. "/boot")
 * @entry: Entry index (0 for first .conf file, 1 for second, etc.)
 * @fname: Buffer to store the full path of the found entry
 * @fname_size: Size of @fname buffer
 * Return: 0 on success, -ENOENT if no more entries
 */
static int bls_scan_entries_dir(const char *prefix, int entry, char *fname,
				int fname_size)
{
	struct fs_dir_stream *dirs;
	struct fs_dirent *dent;
	char dirpath[200];
	int ret = -ENOENT;
	int found = 0;

	snprintf(dirpath, sizeof(dirpath), "%s%s", prefix ? prefix : "",
		 BLS_ENTRIES_DIR);
	log_debug("BLS: scanning dir %s entry %d\n", dirpath, entry);

	dirs = fs_opendir(dirpath);
	if (!dirs)
		return log_msg_ret("opn", -ENOENT);

	while ((dent = fs_readdir(dirs))) {
		int len;

		if (dent->type != FS_DT_REG)
			continue;
		len = strlen(dent->name);
		if (len < 6 || strcmp(dent->name + len - 5, ".conf"))
			continue;

		if (found == entry) {
			snprintf(fname, fname_size, "%s%s/%s",
				 prefix ? prefix : "", BLS_ENTRIES_DIR,
				 dent->name);
			log_debug("BLS: found entry %s\n", fname);
			ret = 0;
			break;
		}
		found++;
	}
	fs_closedir(dirs);

	return ret;
}

static int bls_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct bls_entry entry;
	struct blk_desc *desc;
	const char *const *prefixes;
	struct udevice *bootstd;
	const char *prefix;
	loff_t size;
	int ret, i;

	log_debug("BLS: starting part %d\n", bflow->part);

	/* Get bootstd device for prefixes */
	ret = uclass_first_device_err(UCLASS_BOOTSTD, &bootstd);
	if (ret) {
		log_debug("no bootstd\n");
		return log_msg_ret("std", ret);
	}

	/* Block devices require a partition table */
	if (bflow->blk && !bflow->part) {
		log_debug("no partition table\n");
		return -ENOENT;
	}

	prefixes = bootstd_get_prefixes(bootstd);
	desc = bflow->blk ? dev_get_uclass_plat(bflow->blk) : NULL;

	/*
	 * BOOTMETHF_ANY_PART makes the iterator visit every partition,
	 * including small non-filesystem ones (e.g. ChromeOS kernel slots).
	 * Skip partitions too small to hold any filesystem before probing,
	 * so probe-time reads do not trip the "Read outside partition"
	 * error path
	 */
	if (desc) {
		struct disk_partition info;

		if (!part_get_info(desc, bflow->part, &info) &&
		    info.size < BLS_MIN_PART_BLOCKS)
			return -ENOENT;
	}

	/* Try each prefix: first scan entries/, then fall back to entry.conf */
	i = 0;
	ret = -ENOENT;
	do {
		char fname[200];

		prefix = prefixes ? prefixes[i] : NULL;
		log_debug("trying prefix %s\n", prefix);

		ret = bootmeth_setup_fs(bflow, desc);
		if (ret)
			return log_msg_ret("bfs", ret);

		if (!bls_scan_entries_dir(prefix, bflow->entry, fname,
					  sizeof(fname))) {
			/* fs_closedir() closes the fs, so re-open it */
			ret = bootmeth_setup_fs(bflow, desc);
			if (!ret)
				ret = bootmeth_try_file(bflow, desc, NULL,
							fname);
		} else if (!bflow->entry) {
			/* fs_opendir() closes the fs, so re-open it */
			ret = bootmeth_setup_fs(bflow, desc);
			if (!ret)
				ret = bootmeth_try_file(bflow, desc, prefix,
							BLS_ENTRY_FILE);
		}
	} while (ret && prefixes && prefixes[++i]);

	if (ret) {
		log_debug("no BLS entry file found\n");
		return log_msg_ret("try", ret);
	}

	size = bflow->size;

	/* Read the file */
	ret = bootmeth_alloc_file(bflow, 0x10000, ARCH_DMA_MINALIGN,
				  BFI_BLS_CFG);
	if (ret)
		return log_msg_ret("read", ret);

	ret = bls_entry_init(&entry, bflow, size);
	bls_entry_uninit(&entry);
	if (ret)
		return ret;

	/* Use the title as the entry identifier */
	if (bflow->os_name) {
		bflow->entry_name = strdup(bflow->os_name);
		if (!bflow->entry_name)
			return log_msg_ret("ent", -ENOMEM);
	}

	return 0;
}

/**
 * bls_load_files() - Load files using an existing label
 *
 * @dev: Bootmethod device
 * @bflow: Bootflow to load files for
 * @pxe_ctx: Returns initialized PXE context (caller must destroy)
 * @label: PXE label to use for loading
 * Return: 0 on success, -ve on error
 */
static int bls_load_files(struct udevice *dev, struct bootflow *bflow,
			  struct pxe_context *pxe_ctx,
			  struct pxe_label *label)
{
	const struct bootflow_img *first_img;
	struct bls_info info;
	struct pxe_file *file;
	bool already_loaded;
	int ret;

	/* Check if kernel is already loaded (skip the BLS config image) */
	first_img = alist_get(&bflow->images, 1, struct bootflow_img);
	already_loaded = first_img && first_img->addr;

	/* Set up PXE context */
	info.dev = dev;
	info.bflow = bflow;
	ret = pxe_setup_ctx(pxe_ctx, bls_getfile, &info, true, bflow->fname,
			    false, false, bflow);
	if (ret)
		return log_msg_ret("ctx", ret);

	if (!already_loaded) {
		/* Load files (kernel, initrd, FDT) */
		ret = pxe_load_files(pxe_ctx, label, NULL);
		if (ret) {
			pxe_destroy_ctx(pxe_ctx);
			return log_msg_ret("load", ret);
		}

		/* Update loaded images with their addresses */
		alist_for_each(file, &label->files) {
			struct bootflow_img *img;

			/* Find the corresponding image in bootflow */
			alist_for_each(img, &bflow->images) {
				if (!strcmp(img->fname, file->path)) {
					img->addr = file->addr;
					img->size = file->size;
					break;
				}
			}
		}
	}

	/* Process FDT (apply overlays, etc.) */
	ret = pxe_setup_label(pxe_ctx, label);
	if (ret) {
		pxe_destroy_ctx(pxe_ctx);
		return log_msg_ret("setup", ret);
	}

	return 0;
}

/**
 * bls_load_all() - Load all files needed for boot
 *
 * @dev: Bootmethod device
 * @bflow: Bootflow to load files for
 * @pxe_ctx: Returns initialized PXE context (caller must destroy)
 * @labelp: Returns PXE label (caller must destroy)
 * Return: 0 on success, -ve on error
 */
static int bls_load_all(struct udevice *dev, struct bootflow *bflow,
			struct pxe_context *pxe_ctx,
			struct pxe_label **labelp)
{
	struct pxe_label *label;
	int ret;

	/* Convert bootflow to PXE label for boot execution */
	ret = bls_to_pxe_label(bflow, &label);
	if (ret)
		return log_msg_ret("label", ret);

	ret = bls_load_files(dev, bflow, pxe_ctx, label);
	if (ret) {
		label_destroy(label);
		return ret;
	}

	*labelp = label;

	return 0;
}

static int __maybe_unused bls_read_all(struct udevice *dev,
				       struct bootflow *bflow)
{
	struct pxe_context pxe_ctx;
	struct pxe_label *label;
	int ret;

	ret = bls_load_all(dev, bflow, &pxe_ctx, &label);
	if (ret)
		return ret;

	pxe_destroy_ctx(&pxe_ctx);
	label_destroy(label);

	return 0;
}

static int bls_boot(struct udevice *dev, struct bootflow *bflow)
{
	struct pxe_context pxe_ctx;
	struct pxe_label *label;
	int ret;

	ret = bls_load_all(dev, bflow, &pxe_ctx, &label);
	if (ret)
		return ret;

	/* Set bootargs from BLS options before booting */
	if (label->append)
		env_set("bootargs", label->append);

	/* Boot the label */
	pxe_ctx.label = label;
	ret = pxe_boot(&pxe_ctx);

	/* Cleanup */
	pxe_destroy_ctx(&pxe_ctx);
	label_destroy(label);

	return log_msg_ret("boot", ret);
}

static int bls_bootmeth_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = IS_ENABLED(CONFIG_BOOTSTD_FULL) ?
		"Boot Loader Specification (BLS) Type #1" : "bls";
	plat->flags = BOOTMETHF_MULTI | BOOTMETHF_ANY_PART;

	return 0;
}

static struct bootmeth_ops bls_bootmeth_ops = {
	.get_state_desc	= bls_get_state_desc,
	.check		= bls_check,
	.read_bootflow	= bls_read_bootflow,
	.read_file	= bootmeth_common_read_file,
#if CONFIG_IS_ENABLED(BOOTSTD_FULL)
	.read_all	= bls_read_all,
#endif
	.boot		= bls_boot,
};

static const struct udevice_id bls_bootmeth_ids[] = {
	{ .compatible = "u-boot,boot-loader-specification" },
	{ }
};

/* Put a number before 'bls' to provide a default ordering */
U_BOOT_DRIVER(bootmeth_2bls) = {
	.name		= "bootmeth_bls",
	.id		= UCLASS_BOOTMETH,
	.of_match	= bls_bootmeth_ids,
	.ops		= &bls_bootmeth_ops,
	.bind		= bls_bootmeth_bind,
};
