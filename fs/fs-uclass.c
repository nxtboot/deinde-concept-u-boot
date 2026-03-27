// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of a filesystem, e.g. on a partition
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <bootdev.h>
#include <bootmeth.h>
#include <dir.h>
#include <dm.h>
#include <fs.h>
#include <malloc.h>
#include <dm/device-internal.h>

int fs_split_path(const char *fname, char **subdirp, const char **leafp)
{
	const char *last_slash;
	char *subdir;

	if (!*fname)
		return log_msg_ret("fsp", -EINVAL);

	last_slash = strrchr(fname, '/');
	if (last_slash) {
		int dir_len = last_slash - fname;

		if (!dir_len)
			dir_len = 1;	/* root "/" */
		subdir = malloc(dir_len + 1);
		if (!subdir)
			return log_msg_ret("fsp", -ENOMEM);
		memcpy(subdir, fname, dir_len);
		subdir[dir_len] = '\0';
		*leafp = last_slash + 1;
	} else {
		subdir = strdup("");
		if (!subdir)
			return log_msg_ret("fsp", -ENOMEM);
		*leafp = fname;
	}
	*subdirp = subdir;

	return 0;
}

void fs_split_path_inplace(char *fname, const char **dirp, const char **leafp)
{
	char *last_slash;

	last_slash = strrchr(fname, '/');
	if (last_slash) {
		*leafp = last_slash + 1;
		*last_slash = '\0';
		*dirp = fname;
	} else {
		*leafp = fname;
		*dirp = "";
	}
}

int fs_lookup_dir(struct udevice *dev, const char *path, struct udevice **dirp)
{
	struct fs_ops *ops = fs_get_ops(dev);
	struct udevice *dir;
	int ret;

	if (!path || !strcmp("/", path))
		path = "";

	/* see if we already have this directory */
	device_foreach_child(dir, dev) {
		struct dir_uc_priv *priv;

		if (!device_active(dir))
			continue;
		if (device_get_uclass_id(dir) != UCLASS_DIR)
			continue;

		priv = dev_get_uclass_priv(dir);
		log_debug("dir %s '%s' '%s'\n", dir->name, path, priv->path);
		if (!strcmp(path, priv->path)) {
			*dirp = dir;
			log_debug("found: dev '%s'\n", dir->name);
			return 0;
		}
	}

	ret = ops->lookup_dir(dev, path, &dir);
	if (ret)
		return log_msg_ret("fld", ret);

	*dirp = dir;

	return 0;
}

int fs_mount(struct udevice *dev)
{
	struct fs_ops *ops = fs_get_ops(dev);
	int ret;

	ret = ops->mount(dev);
	if (ret)
		return log_msg_ret("fsm", ret);

	ret = bootdev_setup_for_dev(dev, "fs_bootdev");
	if (ret)
		return log_msg_ret("fss", ret);

	return 0;
}

int fs_unmount(struct udevice *dev)
{
	struct fs_ops *ops = fs_get_ops(dev);

	return ops->unmount(dev);
}

static int fs_get_bootflow(struct udevice *dev, struct bootflow_iter *iter,
			   struct bootflow *bflow)
{
	struct udevice *fsdev = dev_get_parent(dev);
	int ret;

	log_debug("get_bootflow fs '%s'\n", fsdev->name);

	/* for now, always fail here as we don't have FS support in bootmeths */
	return -ENOENT;

	ret = bootmeth_check(bflow->method, iter);
	if (ret)
		return log_msg_ret("check", ret);

	return 0;
}

static int fs_bootdev_bind(struct udevice *dev)
{
	struct bootdev_uc_plat *ucp = dev_get_uclass_plat(dev);

	/*
	 * we don't know what priority to give this, so pick something a little
	 * slow for now
	 */
	ucp->prio = BOOTDEVP_3_INTERNAL_SLOW;

	return 0;
}

static int fs_bootdev_hunt(struct bootdev_hunter *info, bool show)
{
	struct udevice *dev;
	int ret;

	/* mount all filesystems, which will create bootdevs for each */
	uclass_foreach_dev_probe(UCLASS_FS, dev) {
		ret = fs_mount(dev);
		if (ret)
			log_warning("Failed to mount filesystem '%s'\n",
				    dev->name);
	}

	return 0;
}

struct bootdev_ops fs_bootdev_ops = {
	.get_bootflow	= fs_get_bootflow,
};

static const struct udevice_id fs_bootdev_ids[] = {
	{ .compatible = "u-boot,bootdev-fs" },
	{ }
};

U_BOOT_DRIVER(fs_bootdev) = {
	.name		= "fs_bootdev",
	.id		= UCLASS_BOOTDEV,
	.ops		= &fs_bootdev_ops,
	.bind		= fs_bootdev_bind,
	.of_match	= fs_bootdev_ids,
};

BOOTDEV_HUNTER(fs_bootdev_hunter) = {
	.prio		= BOOTDEVP_3_INTERNAL_SLOW,
	.uclass		= UCLASS_FS,
	.hunt		= fs_bootdev_hunt,
	.drv		= DM_DRIVER_REF(fs_bootdev),
};

UCLASS_DRIVER(fs) = {
	.name	= "fs",
	.id	= UCLASS_FS,
	.per_device_auto	= sizeof(struct fs_priv),
	.per_device_plat_auto	= sizeof(struct fs_plat),
};
