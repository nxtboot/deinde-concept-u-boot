// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of files on a filesystem
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <dir.h>
#include <dm.h>
#include <file.h>
#include <malloc.h>
#include <dm/device-internal.h>

int file_add_probe(struct udevice *dir, struct driver *drv, const char *leaf,
		   size_t size, enum dir_open_flags_t flags,
		   struct udevice **devp)
{
	char dev_name[30], *str, *dup_leaf;
	struct file_uc_priv *uc_priv;
	struct udevice *dev;
	int ret;

	snprintf(dev_name, sizeof(dev_name), "%s.file.%x", dir->name,
		 device_get_child_count(dir) + 1);
	ret = -ENOMEM;
	str = strdup(dev_name);
	if (!str)
		goto no_dev_name;
	dup_leaf = strdup(leaf);
	if (!str)
		goto no_dev_path;

	ret = device_bind_with_driver_data(dir, drv, str,  0 /* data */,
					   ofnode_null(), &dev);
	if (ret) {
		log_debug("bind failed %d\n", ret);
		goto no_bind;
	}
	device_set_name_alloced(dev);

	ret = device_probe(dev);
	if (ret)
		goto no_probe;
	uc_priv = dev_get_uclass_priv(dev);

	uc_priv->leaf = dup_leaf;
	uc_priv->size = size;
	*devp = dev;

	return 0;

no_probe:
	device_unbind(dir);
no_bind:
	free(dup_leaf);
no_dev_path:
	free(str);
no_dev_name:

	return ret;
}

long file_read(struct udevice *dev, void *buf, long len)
{
	struct file_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct file_ops *ops = file_get_ops(dev);
	struct iov_iter iter;
	ssize_t ret;

	iter_ubuf(&iter, true, buf, len);

	ret = ops->read_iter(dev, &iter, uc_priv->pos);
	if (ret < 0)
		return log_msg_ret("fir", ret);
	uc_priv->pos += ret;

	return ret;
}

long file_read_at(struct udevice *dev, void *buf, loff_t offset, long len)
{
	struct file_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct file_ops *ops = file_get_ops(dev);
	struct iov_iter iter;
	ssize_t ret;

	if (!len)
		len = uc_priv->size - offset;
	iter_ubuf(&iter, true, buf, len);

	ret = ops->read_iter(dev, &iter, offset);
	if (ret < 0)
		return log_msg_ret("fir", ret);
	uc_priv->pos = offset + ret;

	return ret;
}

long file_write_at(struct udevice *dev, const void *buf, loff_t offset,
		   long len)
{
	struct file_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct file_ops *ops = file_get_ops(dev);
	struct iov_iter iter;
	ssize_t ret;

	if (!ops->write_iter)
		return log_msg_ret("fwn", -ENOSYS);

	iter_ubuf(&iter, false, (void *)buf, len);

	ret = ops->write_iter(dev, &iter, offset);
	if (ret < 0)
		return log_msg_ret("fww", ret);
	uc_priv->pos = offset + ret;

	return ret;
}

UCLASS_DRIVER(file) = {
	.name	= "file",
	.id	= UCLASS_FILE,
	.per_device_auto	= sizeof(struct file_uc_priv),
};
