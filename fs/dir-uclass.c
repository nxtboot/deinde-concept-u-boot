// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of a directory on a filesystem
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_DIR

#include <dm.h>
#include <dir.h>
#include <fs.h>
#include <malloc.h>
#include <dm/device-internal.h>

int dir_add_probe(struct udevice *fsdev, struct driver *drv, const char *path,
		  struct udevice **devp)
{
	char dev_name[30], *str, *dup_path;
	struct dir_uc_priv *uc_priv;
	struct udevice *dev;
	int ret;

	snprintf(dev_name, sizeof(dev_name), "%s.dir", fsdev->name);
	ret = -ENOMEM;
	str = strdup(dev_name);
	if (!str)
		goto no_dev_name;
	dup_path = strdup(path);
	if (!dup_path)
		goto no_dev_path;

	ret = device_bind_with_driver_data(fsdev, drv, str, 0 /* data */,
					   ofnode_null(), &dev);
	if (ret)
		goto no_bind;
	device_set_name_alloced(dev);

	ret = device_probe(dev);
	if (ret)
		goto no_probe;
	uc_priv = dev_get_uclass_priv(dev);
	uc_priv->path = dup_path;

	*devp = dev;

	return 0;

no_probe:
	device_unbind(dev);
no_bind:
	free(dup_path);
no_dev_path:
	free(str);
no_dev_name:

	return ret;
}

int dir_open(struct udevice *dev, struct fs_dir_stream **strmp)
{
	struct dir_ops *ops = dir_get_ops(dev);
	struct fs_dir_stream *strm;
	int ret;

	strm = calloc(1, sizeof(struct fs_dir_stream));
	if (!strm)
		return log_msg_ret("dom", -ENOMEM);

	strm->dev = dev;
	ret = ops->open(dev, strm);
	if (ret) {
		free(strm);
		return log_msg_ret("doo", ret);
	}
	*strmp = strm;

	return 0;
}

int dir_read(struct udevice *dev, struct fs_dir_stream *strm,
	     struct fs_dirent *dent)
{
	struct dir_ops *ops = dir_get_ops(dev);

	log_debug("start %s\n", dev->name);
	memset(dent, '\0', sizeof(struct fs_dirent));

	return ops->read(dev, strm, dent);
}

int dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct dir_ops *ops = dir_get_ops(dev);
	int ret;

	log_debug("start %s\n", dev->name);

	ret = ops->close(dev, strm);
	if (ret)
		return log_msg_ret("dcs", ret);
	free(strm);

	return 0;
}

int dir_open_file(struct udevice *dev, const char *leaf,
		  enum dir_open_flags_t oflags, struct udevice **filp)
{
	struct dir_ops *ops = dir_get_ops(dev);

	log_debug("start %s\n", dev->name);

	return ops->open_file(dev, leaf, oflags, filp);
}

UCLASS_DRIVER(dir) = {
	.name	= "dir",
	.id	= UCLASS_DIR,
	.per_device_auto	= sizeof(struct dir_uc_priv),
};
