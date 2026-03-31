// SPDX-License-Identifier: GPL-2.0+
/*
 * Provides access to the host filesystem from sandbox
 *
 * Copyright (c) 2012, Google Inc.
 */

#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
#include <fs_legacy.h>
#include <malloc.h>
#include <os.h>
#include <sandboxfs.h>

/**
 * struct sandbox_dir_priv - Private info about sandbox directories
 *
 * @head: List of directory entries, or NULL if not known
 * @ptr: Current position in the list
 */
struct sandbox_dir_priv {
	struct os_dirent_node *head, *ptr;
};

/**
 * struct sandbox_dir_priv - Private info about sandbox directories
 *
 * @fd: File descriptor for the file
 */
struct file_priv {
	int fd;
};

/**
 * struct sandbox_fs_priv - Private info about the sandbox filesystem
 *
 * @entries: List of directory entries, or NULL if not scanned yet
 */
struct sandbox_fs_priv {
	struct os_dirent_node *entries;
};

int sandbox_fs_set_blk_dev(struct blk_desc *rbdd, struct disk_partition *info)
{
	/*
	 * Only accept a NULL struct blk_desc for the sandbox, which is when
	 * hostfs interface is used
	 */
	return rbdd != NULL;
}

int sandbox_fs_read_at(const char *filename, loff_t pos, void *buffer,
		       loff_t maxsize, loff_t *actread)
{
	loff_t size;
	int fd, ret;

	fd = os_open(filename, OS_O_RDONLY);
	if (fd < 0)
		return fd;
	ret = os_lseek(fd, pos, OS_SEEK_SET);
	if (ret < 0) {
		os_close(fd);
		return ret;
	}
	if (!maxsize) {
		ret = os_get_filesize(filename, &size);
		if (ret) {
			os_close(fd);
			return ret;
		}

		maxsize = size;
	}

	size = os_read(fd, buffer, maxsize);
	os_close(fd);

	if (size < 0) {
		ret = -1;
	} else {
		ret = 0;
		*actread = size;
	}

	return ret;
}

int sandbox_fs_write_at(const char *filename, loff_t pos, void *buffer,
			loff_t towrite, loff_t *actwrite)
{
	ssize_t size;
	int fd, ret;

	fd = os_open(filename, OS_O_RDWR | OS_O_CREAT);
	if (fd < 0)
		return fd;
	ret = os_lseek(fd, pos, OS_SEEK_SET);
	if (ret < 0) {
		os_close(fd);
		return ret;
	}
	size = os_write(fd, buffer, towrite);
	os_close(fd);

	if (size < 0) {
		ret = -1;
	} else {
		ret = 0;
		*actwrite = size;
	}

	return ret;
}

int sandbox_fs_ls(const char *dirname)
{
	struct os_dirent_node *head, *node;
	int ret;

	ret = os_dirent_ls(dirname, &head);
	if (ret)
		goto out;

	for (node = head; node; node = node->next) {
		printf("%s %10lu %s\n", os_dirent_get_typename(node->type),
		       node->size, node->name);
	}
out:
	os_dirent_free(head);

	return ret;
}

int sandbox_fs_exists(const char *filename)
{
	loff_t size;
	int ret;

	ret = os_get_filesize(filename, &size);
	return ret == 0;
}

int sandbox_fs_size(const char *filename, loff_t *size)
{
	return os_get_filesize(filename, size);
}

void sandbox_fs_close(void)
{
}

int fs_read_sandbox(const char *filename, void *buf, loff_t offset, loff_t len,
		    loff_t *actread)
{
	int ret;

	ret = sandbox_fs_read_at(filename, offset, buf, len, actread);
	if (ret)
		printf("** Unable to read file %s **\n", filename);

	return ret;
}

int fs_write_sandbox(const char *filename, void *buf, loff_t offset,
		     loff_t len, loff_t *actwrite)
{
	int ret;

	ret = sandbox_fs_write_at(filename, offset, buf, len, actwrite);
	if (ret)
		printf("** Unable to write file %s **\n", filename);

	return ret;
}

static int sandbox_fs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_plat *plat = dev_get_uclass_plat(dev);

	if (uc_priv->mounted)
		return log_msg_ret("vfi", -EISCONN);

	/* Reject block-device mounts - sandboxfs uses the host OS */
	if (plat->desc)
		return log_msg_ret("vfb", -ENODEV);

	uc_priv->mounted = true;

	return 0;
}

static int sandbox_fs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("vfu", -ENOTCONN);

	uc_priv->mounted = false;

	return 0;
}

static int sandbox_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct sandbox_dir_priv *priv = dev_get_priv(dev);
	struct dir_uc_priv *dir_uc_priv = dev_get_uclass_priv(dev);
	int ret;

	ret = os_dirent_ls(*dir_uc_priv->path ? dir_uc_priv->path : ".",
			   &priv->head);
	if (ret) {
		log_err("Failed to open directory: %d\n", ret);
		return ret;
	}
	priv->ptr = priv->head;

	return 0;
}

int sandbox_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
		     struct fs_dirent *dent)
{
	struct sandbox_dir_priv *dir_priv = dev_get_priv(dev);
	struct os_dirent_node *ptr = dir_priv->ptr;

	if (!ptr)
		return log_msg_ret("sdr", -ENOENT);

	if (ptr->type == OS_FILET_REG)
		dent->type = FS_DT_REG;
	else if (ptr->type == OS_FILET_DIR)
		dent->type = FS_DT_DIR;
	else if (ptr->type == OS_FILET_LNK)
		dent->type = FS_DT_LNK;
	dent->size = ptr->size;
	strlcpy(dent->name, ptr->name, FS_DIRENT_NAME_LEN);
	dir_priv->ptr = ptr->next;

	return 0;
}

static int sandbox_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct sandbox_dir_priv *dir_priv = dev_get_priv(dev);

	log_debug("close\n");
	os_dirent_free(dir_priv->head);
	dir_priv->ptr = NULL;

	log_debug("close done\n");

	return 0;
}

static ssize_t sandbox_read_iter(struct udevice *dev, struct iov_iter *iter,
				 loff_t pos)
{
	struct file_priv *priv = dev_get_priv(dev);
	ssize_t ret;

	log_debug("start dev '%s' len %lx\n", dev->name, iter->count);
	ret = os_lseek(priv->fd, pos, OS_SEEK_SET);
	if (ret < 0)
		return log_msg_ret("vfs", ret);

	ret = os_read(priv->fd, iter_iov_ptr(iter), iter_iov_avail(iter));
	if (ret < 0)
		return log_msg_ret("vfr", ret);
	iter_advance(iter, ret);
	log_debug("read %lx bytes\n", ret);

	return ret;
}

static ssize_t sandbox_write_iter(struct udevice *dev, struct iov_iter *iter,
				  loff_t pos)
{
	struct file_priv *priv = dev_get_priv(dev);
	ssize_t ret;

	log_debug("start dev '%s' len %lx\n", dev->name, iter->count);
	ret = os_lseek(priv->fd, pos, OS_SEEK_SET);
	if (ret < 0)
		return log_msg_ret("vfs", ret);

	ret = os_write(priv->fd, iter_iov_ptr(iter), iter_iov_avail(iter));
	if (ret < 0)
		return log_msg_ret("vfw", ret);
	iter_advance(iter, ret);
	log_debug("wrote %lx bytes\n", ret);

	return ret;
}

static struct file_ops sandbox_file_ops = {
	.read_iter	= sandbox_read_iter,
	.write_iter	= sandbox_write_iter,
};

static const struct udevice_id file_ids[] = {
	{ .compatible = "virtio-fs,file" },
	{ }
};

U_BOOT_DRIVER(sandbox_file) = {
	.name	= "sandbox_file",
	.id	= UCLASS_FILE,
	.of_match = file_ids,
	.ops	= &sandbox_file_ops,
	.priv_auto	= sizeof(struct file_priv),
	.flags	= DM_FLAG_ACTIVE_DMA,
};

static int sandbox_dir_open_file(struct udevice *dir, const char *leaf,
				 enum dir_open_flags_t oflags,
				 struct udevice **filp)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dir);
	char pathname[FILE_MAX_PATH_LEN];
	struct file_priv *priv;
	int mode, fd, ftype, ret;
	struct udevice *dev;
	off_t size;

	snprintf(pathname, sizeof(pathname), "%s/%s",
		 *uc_priv->path ? uc_priv->path: ".", leaf);
	ftype = os_get_filetype(pathname);
	if (ftype < 0) {
		/* Allow creating new files for write modes */
		if (oflags == DIR_O_RDONLY)
			return log_msg_ret("soF", ftype);
	} else if (ftype != OS_FILET_REG) {
		return log_msg_ret("sOf", -EINVAL);
	}

	if (oflags == DIR_O_RDONLY)
		mode = OS_O_RDONLY;
	else if (oflags == DIR_O_WRONLY)
		mode = OS_O_WRONLY | OS_O_CREAT;
	else if (oflags == DIR_O_RDWR)
		mode = OS_O_RDWR;
	else
		return log_msg_ret("som", -EINVAL);

	ret = os_open(pathname, mode);
	if (ret < 0)
		return log_msg_ret("sOm", ret);
	fd = ret;

	size = os_filesize(fd);
	if (size < 0)
		return log_msg_ret("sos", ret);

	ret = file_add_probe(dir, DM_DRIVER_REF(sandbox_file), leaf, size,
			     oflags, &dev);
	if (ret) {
		os_close(fd);
		return log_msg_ret("sof", ret);
	}

	priv = dev_get_priv(dev);
	priv->fd = fd;
	*filp = dev;

	return 0;
}

static struct dir_ops sandbox_dir_ops = {
	.open	= sandbox_dir_open,
	.read	= sandbox_dir_read,
	.close	= sandbox_dir_close,
	.open_file	= sandbox_dir_open_file,
};

static const struct udevice_id dir_ids[] = {
	{ .compatible = "virtio-fs,directory" },
	{ }
};

U_BOOT_DRIVER(sandbox_dir) = {
	.name	= "sandbox_dir",
	.id	= UCLASS_DIR,
	.of_match = dir_ids,
	.ops	= &sandbox_dir_ops,
	.priv_auto	= sizeof(struct sandbox_dir_priv),
	.flags	= DM_FLAG_ACTIVE_DMA,
};

static int sandbox_fs_lookup_dir(struct udevice *dev, const char *path,
				 struct udevice **dirp)
{
	struct udevice *dir;
	int ftype;
	int ret;

	ftype = os_get_filetype(*path ? path : "/");
	if (ftype < 0)
		return ftype;
	if (ftype != OS_FILET_DIR)
		return log_msg_ret("sld", -ENOTDIR);

	log_debug("looking up path '%s'\n", path);

	ret = dir_add_probe(dev, DM_DRIVER_GET(sandbox_dir), path, &dir);
	if (ret)
		return log_msg_ret("slD", ret);

	log_debug("added new dir '%s'\n", path);

	*dirp = dir;

	return 0;
}

static int sandbox_fs_remove(struct udevice *dev)
{
	return 0;
}

static const struct fs_ops sandbox_fs_ops = {
	.mount		= sandbox_fs_mount,
	.unmount	= sandbox_fs_unmount,
	.lookup_dir	= sandbox_fs_lookup_dir,
};

static const struct udevice_id sandbox_fs_ids[] = {
	{ .compatible = "sandbox,fs" },
	{ }
};

U_BOOT_DRIVER(sandbox_fs) = {
	.name	= "sandbox_fs",
	.id	= UCLASS_FS,
	.of_match = sandbox_fs_ids,
	.ops	= &sandbox_fs_ops,
	.remove	= sandbox_fs_remove,
	.priv_auto	= sizeof(struct sandbox_fs_priv),
	.flags	= DM_FLAG_ACTIVE_DMA,
};
