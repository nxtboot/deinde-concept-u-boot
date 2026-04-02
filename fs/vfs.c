// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Filesystem layer
 *
 * Manages a mount tree using UCLASS_MOUNT devices as children of
 * UCLASS_DIR devices, providing unified path resolution across mounted
 * filesystems. Inspired by the Linux VFS.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_MOUNT

#include <blk.h>
#include <ctype.h>
#include <dir.h>
#include <dm.h>
#include <event.h>
#include <file.h>
#include <fs.h>
#include <fs_common.h>
#include <fs_legacy.h>
#include <malloc.h>
#include <part.h>
#include <sort.h>
#include <vfs.h>
#include "vfs_internal.h"
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <dm/uclass-internal.h>

#define vfs_foreach_mount(mnt, pos) \
	for (uclass_first_device(UCLASS_MOUNT, &(pos)); \
	     (pos) && ((mnt) = dev_get_uclass_priv(pos)); \
	     uclass_next_device(&(pos)))

/**
 * canonicalise_path() - Remove . and .. components from an absolute path
 *
 * Modifies @buf in place. The path must start with '/'.
 *
 * @buf: Absolute path to canonicalise (modified in place)
 */
static int canonicalise_path(char *buf)
{
	char *p, *token;
	char *stack[64];
	int depth = 0;

	/* Tokenise by '/' and resolve . and .. */
	p = buf + 1;	/* skip leading '/' */
	while (*p) {
		token = p;
		while (*p && *p != '/')
			p++;
		if (*p)
			*p++ = '\0';

		if (!*token || !strcmp(token, ".")) {
			continue;
		} else if (!strcmp(token, "..")) {
			if (depth > 0)
				depth--;
		} else {
			if (depth >= ARRAY_SIZE(stack))
				return -ENAMETOOLONG;
			stack[depth++] = token;
		}
	}

	/*
	 * Rebuild the path from the stack. This always fits in buf since
	 * removing . and .. components can only shorten the path.
	 */
	p = buf;
	*p++ = '/';
	for (int i = 0; i < depth; i++) {
		if (i > 0)
			*p++ = '/';
		strcpy(p, stack[i]);
		p += strlen(stack[i]);
	}
	*p = '\0';

	/* Ensure root is "/" not "" */
	if (!buf[1])
		buf[0] = '/';

	return 0;
}

const char *vfs_path_resolve(const char *cwd, const char *path, char *buf,
			     int size)
{
	if (!path || !*path) {
		strncpy(buf, cwd, size);
		buf[size - 1] = '\0';
		return buf;
	}

	if (*path == '/') {
		strncpy(buf, path, size);
		buf[size - 1] = '\0';
	} else {
		int len = strlen(cwd);

		if (len == 1)
			snprintf(buf, size, "/%s", path);
		else
			snprintf(buf, size, "%s/%s", cwd, path);
	}

	if (canonicalise_path(buf))
		return NULL;

	return buf;
}

const char *vfs_getcwd(void)
{
	struct udevice *vfs = vfs_root();
	struct vfs_priv *priv;

	if (!vfs)
		return "/";
	priv = dev_get_priv(vfs);

	return priv->cwd;
}

int vfs_chdir(const char *path)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *vfs, *mnt;
	const char *subpath, *abs;
	struct vfs_priv *priv;
	int len, ret;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("vci", -ENXIO);
	priv = dev_get_priv(vfs);

	abs = vfs_path_resolve(vfs_getcwd(), path, resolved, sizeof(resolved));
	if (!abs)
		return log_msg_ret("vcp", -ENAMETOOLONG);

	/* Verify the path exists by trying to resolve it */
	if (strcmp(abs, "/")) {
		ret = vfs_find_mount(vfs, abs, &mnt, &subpath);
		if (ret)
			return log_msg_ret("vcf", ret);
	}

	len = strlen(abs);

	/* Strip trailing slash (but keep "/" for root) */
	if (len > 1 && abs[len - 1] == '/')
		len--;

	if (len >= sizeof(priv->cwd))
		return log_msg_ret("vcl", -ENAMETOOLONG);

	memcpy(priv->cwd, abs, len);
	priv->cwd[len] = '\0';

	return 0;
}

/**
 * find_mount() - Check whether a directory is a mount point
 *
 * @dir: UCLASS_DIR device to check
 * @mntp: Returns the UCLASS_MOUNT device if found
 * Return: 0 if found, -ENOENT if not
 */
static int find_mount(struct udevice *dir, struct udevice **mntp)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		if (mnt->dir == dir) {
			*mntp = dev;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * find_mount_by_target() - Find the mount for a given FS device
 *
 * @fsdev: UCLASS_FS device to search for
 * @mntp: Returns the UCLASS_MOUNT device if found
 * Return: 0 if found, -ENOENT if not
 */
static int find_mount_by_target(struct udevice *fsdev, struct udevice **mntp)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		if (mnt->target == fsdev) {
			*mntp = dev;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * walk_path() - Walk a path following mount points
 *
 * For each path component, looks up the directory in the current
 * filesystem and checks if it is a mount point. Stops when a component
 * cannot be looked up or is not a mount point.
 *
 * @path: Path to walk (without leading '/')
 * @start: Starting FS device
 * @mntp: Returns the deepest mount found, or NULL if none
 * @remainp: Returns pointer to the remaining unresolved path
 * Return: The FS after the deepest mount crossed (same as @start if none)
 */
static struct udevice *walk_path(const char *path, struct udevice *start,
				 struct udevice **mntp, const char **remainp)
{
	struct udevice *best = NULL, *cur_fs = start;
	const char *best_remain = path;
	const char *p = path;

	while (*p) {
		char component[FS_DIRENT_NAME_LEN];
		struct udevice *comp_dir, *mnt_dev;
		struct vfsmount *mnt;
		const char *slash;
		int len;

		slash = strchr(p, '/');
		len = slash ? slash - p : strlen(p);
		if (len >= sizeof(component))
			break;

		memcpy(component, p, len);
		component[len] = '\0';

		if (fs_lookup_dir(cur_fs, component, &comp_dir))
			break;

		if (find_mount(comp_dir, &mnt_dev))
			break;

		/* Found a mount - record it and cross into the FS */
		best = mnt_dev;
		p += len;
		if (*p == '/')
			p++;
		best_remain = p;

		mnt = dev_get_uclass_priv(mnt_dev);
		cur_fs = mnt->target;
	}

	*mntp = best;
	*remainp = best_remain;

	return cur_fs;
}

/**
 * vfs_mount_path() - Build the full path for a mount device
 *
 * Walks up the device tree to reconstruct the absolute path, using
 * dir_uc_priv->path from each mount's directory to get the component name.
 *
 * @mnt_dev: UCLASS_MOUNT device
 * @buf: Buffer to write path into
 * @size: Size of buffer
 * Return: 0 if OK, -ve on error
 */
static int vfs_mount_path(struct udevice *mnt_dev, char *buf, int size)
{
	struct vfsmount *mnt = dev_get_uclass_priv(mnt_dev);
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(mnt->dir);
	struct udevice *parent_fs = dev_get_parent(mnt->dir);

	if (parent_fs == vfs_root()) {
		snprintf(buf, size, "/%s", uc_priv->path);
	} else {
		char parent_path[FILE_MAX_PATH_LEN];
		struct udevice *pdev;
		int ret;

		ret = find_mount_by_target(parent_fs, &pdev);
		if (ret)
			return ret;

		ret = vfs_mount_path(pdev, parent_path, sizeof(parent_path));
		if (ret)
			return ret;
		snprintf(buf, size, "%s/%s", parent_path, uc_priv->path);
	}

	return 0;
}

/* VFS root filesystem - provides an empty root directory */

static int vfs_rootfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct vfs_priv *priv = dev_get_priv(dev);

	if (uc_priv->mounted)
		return log_msg_ret("rfm", -EISCONN);

	strcpy(priv->cwd, "/");

	uc_priv->mounted = true;

	return 0;
}

static int vfs_rootfs_unmount(struct udevice *dev)
{
	return log_msg_ret("rfu", -EBUSY);
}

static int vfs_rootfs_lookup_dir(struct udevice *dev, const char *path,
				 struct udevice **dirp)
{
	struct dir_uc_priv *uc_priv;
	struct udevice *child, *dir;
	int ret;

	/* Check for an existing dir with this path */
	device_foreach_child(child, dev) {
		if (device_get_uclass_id(child) == UCLASS_DIR) {
			uc_priv = dev_get_uclass_priv(child);
			if (uc_priv->path && !strcmp(uc_priv->path, path)) {
				*dirp = child;
				return 0;
			}
		}
	}

	/* Create a new dir */
	ret = dir_add_probe(dev, DM_DRIVER_GET(vfs_rootfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("rfD", ret);

	*dirp = dir;

	return 0;
}

/* Exported functions */

int vfs_find_mount(struct udevice *vfs, const char *path, struct udevice **mntp,
		   const char **subpathp)
{
	struct udevice *best;
	const char *p;

	p = path;
	if (*p == '/')
		p++;

	walk_path(p, vfs, &best, subpathp);

	if (!best) {
		if (!*p) {
			*mntp = NULL;
			return 0;
		}
		return log_msg_ret("vfn", -ENOENT);
	}

	*mntp = best;

	return 0;
}

/**
 * vfs_resolve_mount() - Resolve a path to its mount and subpath
 *
 * Handles vfs_root lookup, cwd resolution and mount lookup in one call.
 *
 * @path: Absolute or relative VFS path
 * @resolved: Buffer for cwd-resolved path
 * @size: Size of @resolved
 * @mntp: Returns the UCLASS_MOUNT device (NULL for root)
 * @subpathp: Returns the remaining path within the mount
 * Return: 0 if OK, -ve on error
 */
static int vfs_resolve_mount(const char *path, char *resolved, int size,
			     struct udevice **mntp, const char **subpathp)
{
	struct udevice *vfs;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("vrv", -ENXIO);

	path = vfs_path_resolve(vfs_getcwd(), path, resolved, size);
	if (!path)
		return log_msg_ret("vrp", -ENAMETOOLONG);

	return vfs_find_mount(vfs, path, mntp, subpathp);
}

/**
 * vfs_resolve_dir() - Resolve a path to its parent directory and leaf name
 *
 * Resolves the mount, splits the subpath into directory and leaf, and
 * looks up the directory device.
 *
 * @path: Absolute or relative VFS path
 * @dirp: Returns the UCLASS_DIR device for the parent directory
 * @leafp: Returns allocated copy of the leaf filename (caller must free)
 * Return: 0 if OK, -ve on error
 */
static int vfs_resolve_dir(const char *path, struct udevice **dirp,
			   char **leafp)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *mnt;
	struct vfsmount *mnt_priv;
	const char *subpath, *dirpart, *leaf;
	char *sub;
	int ret;

	ret = vfs_resolve_mount(path, resolved,
				sizeof(resolved), &mnt, &subpath);
	if (ret)
		return log_msg_ret("vdm", ret);

	if (!mnt)
		return log_msg_ret("vdr", -ENOENT);

	mnt_priv = dev_get_uclass_priv(mnt);

	/*
	 * Split in place - subpath points into resolved[], so we can
	 * modify it. After the split, dirpart is the directory portion
	 * and leaf points to the leaf filename.
	 */
	sub = (char *)subpath;
	fs_split_path_inplace(sub, &dirpart, &leaf);

	ret = fs_lookup_dir(mnt_priv->target, dirpart, dirp);
	if (ret)
		return log_msg_ret("vdd", ret);

	*leafp = strdup(leaf);
	if (!*leafp)
		return log_msg_ret("vdl", -ENOMEM);

	return 0;
}

int vfs_resolve(struct udevice *vfs, const char *path, struct udevice **dirp)
{
	struct udevice *cur_fs, *best;
	const char *remain;

	if (!path || *path != '/')
		return log_msg_ret("vrp", -EINVAL);

	cur_fs = walk_path(path + 1, vfs, &best, &remain);

	/* Remaining path must be at most one component (the target dir) */
	if (strchr(remain, '/'))
		return log_msg_ret("vrm", -ENOENT);

	return fs_lookup_dir(cur_fs, remain, dirp);
}

int vfs_mount(struct udevice *vfs, struct udevice *dir, struct udevice *fsdev)
{
	int ret;

	ret = fs_mount(fsdev);
	if (ret && ret != -EISCONN)
		return log_msg_ret("vmm", ret);

	ret = fs_mount_init(vfs, dir, fsdev);
	if (ret) {
		fs_unmount(fsdev);
		return log_msg_ret("vmc", ret);
	}

	return 0;
}

int vfs_umount(struct udevice *mnt_dev)
{
	struct vfsmount *mnt = dev_get_uclass_priv(mnt_dev);
	int ret;

	ret = fs_unmount(mnt->target);
	if (ret && ret != -ENOTCONN)
		return log_msg_ret("vuu", ret);

	ret = fs_mount_uninit(mnt_dev);
	if (ret)
		return log_msg_ret("vud", ret);

	return 0;
}

int vfs_umount_path(struct udevice *vfs, const char *path)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *mnt_dev;
	const char *subpath;
	int ret;

	ret = vfs_resolve_mount(path, resolved, sizeof(resolved),
				&mnt_dev, &subpath);
	if (ret)
		return log_msg_ret("vuf", ret);

	/* Make sure the entire path was consumed (exact match) */
	if (!mnt_dev || *subpath)
		return log_msg_ret("vup", -ENOENT);

	return vfs_umount(mnt_dev);
}

bool vfs_is_mount_point(struct udevice *dir)
{
	struct udevice *mnt;

	return !find_mount(dir, &mnt);
}

int vfs_umount_all(void)
{
	struct udevice *dev, *next;
	struct uclass *uc;
	int ret, err = 0;

	ret = uclass_get(UCLASS_MOUNT, &uc);
	if (ret)
		return ret;

	uclass_foreach_dev_safe(dev, next, uc) {
		struct vfsmount *mnt = dev_get_uclass_priv(dev);

		if (!device_active(dev))
			continue;

		ret = fs_unmount(mnt->target);
		if (ret && ret != -ENOTCONN) {
			err = ret;
			continue;
		}

		ret = fs_mount_uninit(dev);
		if (ret)
			err = ret;
	}

	return err;
}

void vfs_print_mounts(void)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		char path[FILE_MAX_PATH_LEN];

		if (!vfs_mount_path(dev, path, sizeof(path)))
			printf("%-20s %s\n", path, mnt->target->name);
	}
}

int vfs_open_file(const char *path, enum dir_open_flags_t oflags,
		  struct udevice **filp)
{
	struct udevice *dir;
	char *leaf;
	int ret;

	ret = vfs_resolve_dir(path, &dir, &leaf);
	if (ret)
		return log_msg_ret("vof", ret);

	ret = dir_open_file(dir, leaf, oflags, filp);
	free(leaf);
	if (ret)
		return log_msg_ret("voo", ret);

	return 0;
}

void vfs_print_df(void)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		char path[FILE_MAX_PATH_LEN];
		struct fs_statfs stats;
		int ret;

		if (vfs_mount_path(dev, path, sizeof(path)))
			continue;

		ret = fs_do_statfs(mnt->target, &stats);
		if (!ret) {
			u64 used = stats.blocks - stats.bfree;

			printf("%-16s %6lu %10llu %10llu %10llu\n", path,
			       stats.bsize, stats.blocks * stats.bsize,
			       used * stats.bsize,
			       stats.bfree * stats.bsize);
		} else {
			printf("%-16s %6s %10s %10s %10s\n", path,
			       "-", "-", "-", "-");
		}
	}
}

int vfs_ln(const char *path, const char *target)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *vfs, *mnt;
	const char *subpath;
	struct vfsmount *mnt_priv;
	int ret;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("vli", -ENXIO);

	path = vfs_path_resolve(vfs_getcwd(), path, resolved, sizeof(resolved));

	if (!path)
		return log_msg_ret("vrp", -ENAMETOOLONG);
	ret = vfs_find_mount(vfs, path, &mnt, &subpath);
	if (ret)
		return log_msg_ret("vlm", ret);

	mnt_priv = dev_get_uclass_priv(mnt);

	return fs_do_ln(mnt_priv->target, subpath, target);
}

int vfs_rename(const char *old_path, const char *new_path)
{
	char resolved_old[FILE_MAX_PATH_LEN];
	char resolved_new[FILE_MAX_PATH_LEN];
	struct udevice *mnt_old, *mnt_new;
	const char *sub_old, *sub_new;
	struct vfsmount *mnt_priv;
	int ret;

	ret = vfs_resolve_mount(old_path, resolved_old,
				sizeof(resolved_old),
				&mnt_old, &sub_old);
	if (ret)
		return log_msg_ret("vro", ret);

	ret = vfs_resolve_mount(new_path, resolved_new,
				sizeof(resolved_new),
				&mnt_new, &sub_new);
	if (ret)
		return log_msg_ret("vrn", ret);

	/* Both paths must be on the same mount */
	if (mnt_old != mnt_new)
		return log_msg_ret("vrx", -EXDEV);

	mnt_priv = dev_get_uclass_priv(mnt_old);

	return fs_do_rename(mnt_priv->target, sub_old, sub_new);
}

int vfs_readlink(const char *path, char *buf, int size)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *mnt;
	struct vfsmount *mnt_priv;
	const char *subpath;
	int ret;

	ret = vfs_resolve_mount(path, resolved,
				sizeof(resolved), &mnt, &subpath);
	if (ret)
		return log_msg_ret("rlm", ret);

	mnt_priv = dev_get_uclass_priv(mnt);

	return fs_readlink(mnt_priv->target, subpath, buf, size);
}

int vfs_statfs(const char *path, struct fs_statfs *stats)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *mnt;
	struct vfsmount *mnt_priv;
	const char *subpath;
	int ret;

	ret = vfs_resolve_mount(path, resolved,
				sizeof(resolved), &mnt, &subpath);
	if (ret)
		return log_msg_ret("dfm", ret);

	mnt_priv = dev_get_uclass_priv(mnt);

	return fs_do_statfs(mnt_priv->target, stats);
}

int vfs_unlink(const char *path)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *vfs, *mnt;
	struct vfsmount *mnt_priv;
	const char *subpath;
	int ret;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("vui", -ENXIO);

	path = vfs_path_resolve(vfs_getcwd(), path, resolved, sizeof(resolved));
	if (!path)
		return log_msg_ret("vup", -ENAMETOOLONG);

	ret = vfs_find_mount(vfs, path, &mnt, &subpath);
	if (ret)
		return log_msg_ret("vum", ret);

	mnt_priv = dev_get_uclass_priv(mnt);

	return fs_do_unlink(mnt_priv->target, subpath);
}

int vfs_mkdir(const char *path)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *vfs, *mnt;
	struct vfsmount *mnt_priv;
	const char *subpath;
	int ret;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("vmi", -ENXIO);

	path = vfs_path_resolve(vfs_getcwd(), path, resolved, sizeof(resolved));
	if (!path)
		return log_msg_ret("vmp", -ENAMETOOLONG);

	ret = vfs_find_mount(vfs, path, &mnt, &subpath);
	if (ret)
		return log_msg_ret("vmm", ret);

	mnt_priv = dev_get_uclass_priv(mnt);

	return fs_do_mkdir(mnt_priv->target, subpath);
}

int vfs_stat(const char *path, struct fs_dirent *dent)
{
	struct fs_dir_stream *strm;
	struct fs_dirent entry;
	struct udevice *dir;
	char *leaf;
	int ret;

	ret = vfs_resolve_dir(path, &dir, &leaf);
	if (ret)
		return log_msg_ret("vsd", ret);

	/* Scan the directory for the matching entry */
	ret = dir_open(dir, &strm);
	if (ret) {
		free(leaf);
		return log_msg_ret("vso", ret);
	}

	while (!(ret = dir_read(dir, strm, &entry))) {
		if (!strcmp(entry.name, leaf)) {
			*dent = entry;
			dir_close(dir, strm);
			free(leaf);
			return 0;
		}
	}

	dir_close(dir, strm);
	free(leaf);

	return log_msg_ret("vsn", -ENOENT);
}

static int dirent_cmp(const void *a, const void *b)
{
	const struct fs_dirent *da = a;
	const struct fs_dirent *db = b;

	return strcmp(da->name, db->name);
}

int vfs_ls(const char *path)
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *mnt, *dir = NULL;
	struct fs_dir_stream *strm;
	struct fs_dirent *entries;
	struct fs_dirent dent;
	const char *subpath;
	int count = 0;
	int max = 64;
	int ret, i;

	ret = vfs_resolve_mount(path, resolved,
				sizeof(resolved), &mnt, &subpath);
	if (ret)
		return ret;

	if (mnt) {
		struct vfsmount *m = dev_get_uclass_priv(mnt);

		ret = fs_lookup_dir(m->target, subpath, &dir);
	} else {
		/* Root "/" - list the VFS root dir */
		struct udevice *vfs = vfs_root();

		ret = fs_lookup_dir(vfs, "", &dir);
	}
	if (ret)
		return ret;

	entries = malloc(max * sizeof(*entries));
	if (!entries)
		return -ENOMEM;

	ret = dir_open(dir, &strm);
	if (ret) {
		free(entries);
		return ret;
	}

	while (!dir_read(dir, strm, &dent)) {
		if (count == max) {
			struct fs_dirent *tmp;

			max *= 2;
			tmp = realloc(entries, max * sizeof(*entries));
			if (!tmp) {
				free(entries);
				return -ENOMEM;
			}
			entries = tmp;
		}
		entries[count++] = dent;
	}

	qsort(entries, count, sizeof(*entries), dirent_cmp);

	for (i = 0; i < count; i++) {
		if (entries[i].type == FS_DT_DIR)
			printf("DIR %10u %s\n", 0, entries[i].name);
		else
			printf("    %10llu %s\n", entries[i].size,
			       entries[i].name);
	}
	free(entries);

	dir_close(dir, strm);

	return 0;
}

int fs_mount_blkdev_auto(struct blk_desc *desc, int part_num,
			 struct disk_partition *part, const char *mountpoint)
{
	struct driver *drv = ll_entry_start(struct driver, driver);
	const int n_ents = ll_entry_count(struct driver, driver);
	int i;

	for (i = 0; i < n_ents; i++, drv++) {
		const char *name = drv->name;
		int len = strlen(name);
		char type[30];
		int ret;

		if (drv->id != UCLASS_FS)
			continue;

		/* Strip "_fs" suffix to get the type name */
		if (len < 4 || strcmp(name + len - 3, "_fs"))
			continue;

		strlcpy(type, name, min((int)sizeof(type), len - 2));

		ret = fs_mount_blkdev(type, desc, part_num, part, mountpoint);
		if (!ret)
			return 0;
		if (ret == -EBUSY)
			return log_msg_ret("fmb", ret);
	}

	return -ENODEV;
}

int fs_mount_blkdev(const char *type, struct blk_desc *desc, int part_num,
		    struct disk_partition *part, const char *mountpoint)
{
	char resolved[FILE_MAX_PATH_LEN];
	static int blkfs_count;
	char drv_name[30], dev_name[30];
	struct udevice *vfs, *mnt, *dir, *dev;
	const char *subpath;
	struct fs_plat *plat;
	char *str;
	int ret;

	vfs = vfs_root();
	if (!vfs)
		return log_msg_ret("fmi", -ENXIO);

	mountpoint = vfs_path_resolve(vfs_getcwd(), mountpoint,
				      resolved, sizeof(resolved));
	if (!mountpoint)
		return log_msg_ret("fmp", -ENAMETOOLONG);

	snprintf(drv_name, sizeof(drv_name), "%s_fs", type);
	snprintf(dev_name, sizeof(dev_name), "%s.%d", drv_name, blkfs_count);

	str = strdup(dev_name);
	if (!str)
		return -ENOMEM;

	ret = device_bind_driver(dm_root(), drv_name, str, &dev);
	if (ret) {
		free(str);
		return log_msg_ret("fmb", ret);
	}
	device_set_name_alloced(dev);

	/* Set block device info in uclass platdata before probing */
	plat = dev_get_uclass_plat(dev);
	plat->desc = desc;
	plat->part = *part;

	ret = device_probe(dev);
	if (ret) {
		device_unbind(dev);
		return log_msg_ret("fmp", ret);
	}

	/* Check if the mountpoint is already in use */
	ret = vfs_find_mount(vfs, mountpoint, &mnt, &subpath);
	if (!ret && mnt && !*subpath) {
		device_remove(dev, DM_REMOVE_NORMAL);
		device_unbind(dev);
		return log_msg_ret("fme", -EBUSY);
	}

	ret = vfs_resolve(vfs, mountpoint, &dir);
	if (ret) {
		device_remove(dev, DM_REMOVE_NORMAL);
		device_unbind(dev);
		return log_msg_ret("fmr", ret);
	}

	ret = vfs_mount(vfs, dir, dev);
	if (ret) {
		device_remove(dev, DM_REMOVE_NORMAL);
		device_unbind(dev);
		return log_msg_ret("fmm", ret);
	}

	blkfs_count++;

	return 0;
}

#ifdef CONFIG_AUTO_COMPLETE
int vfs_complete(char *buf, const char *path, int maxv, char *cmdv[])
{
	char resolved[FILE_MAX_PATH_LEN];
	struct udevice *vfs, *mnt_dev, *dir;
	struct fs_dir_stream *strm;
	const char *subpath, *prefix;
	struct vfsmount *mnt;
	struct fs_dirent dent;
	int n = 0;

	vfs = vfs_root();
	if (!vfs)
		return 0;

	path = vfs_path_resolve(vfs_getcwd(), path, resolved, sizeof(resolved));
	if (!path)
		return 0;

	/*
	 * Split into directory part and prefix to match.
	 * "/host/ar" -> dir="/host", prefix="ar"
	 * "/host/"   -> dir="/host", prefix=""
	 * "/"        -> dir="/", prefix=""
	 */
	prefix = strrchr(path, '/');
	if (!prefix)
		return 0;
	prefix++;	/* skip the '/' */

	/* Complete from root mount points */
	if (prefix == path + 1) {
		struct dir_uc_priv *uc_priv;

		vfs_foreach_mount(mnt, mnt_dev) {
			uc_priv = dev_get_uclass_priv(mnt->dir);
			if (!strncmp(uc_priv->path, prefix, strlen(prefix))) {
				if (n >= maxv - 1)
					break;
				sprintf(buf, "/%s/", uc_priv->path);
				cmdv[n++] = buf;
				buf += strlen(buf) + 1;
			}
		}
		cmdv[n] = NULL;
		return n;
	}

	/* Resolve the directory portion */
	if (vfs_find_mount(vfs, path, &mnt_dev, &subpath))
		return 0;

	mnt = dev_get_uclass_priv(mnt_dev);

	/* Get the directory part of subpath (everything before prefix) */
	{
		char dirpath[FILE_MAX_PATH_LEN];
		int dir_len;

		dir_len = prefix - 1 - (path + strlen(path) - strlen(subpath));
		if (dir_len < 0)
			dir_len = 0;
		memcpy(dirpath, subpath, dir_len);
		dirpath[dir_len] = '\0';

		if (fs_lookup_dir(mnt->target, dirpath, &dir))
			return 0;
	}

	if (dir_open(dir, &strm))
		return 0;

	while (!dir_read(dir, strm, &dent)) {
		if (strncmp(dent.name, prefix, strlen(prefix)))
			continue;
		if (n >= maxv - 1)
			break;
		strcpy(buf, dent.name);
		if (dent.type == FS_DT_DIR)
			strcat(buf, "/");
		cmdv[n++] = buf;
		buf += strlen(buf) + 1;
	}

	dir_close(dir, strm);
	cmdv[n] = NULL;

	return n;
}

int vfs_cmd_complete(int argc, char *const argv[], char last_char,
		     int maxv, char *cmdv[])
{
	static char complete_buf[2048];
	int space = last_char == '\0' || isblank(last_char);

	/* Complete the last argument as a path */
	if (space && argc >= 1)
		return vfs_complete(complete_buf, "", maxv, cmdv);
	if (!space && argc >= 2)
		return vfs_complete(complete_buf, argv[argc - 1], maxv, cmdv);

	return 0;
}
#endif

struct udevice *vfs_root(void)
{
	struct udevice *dev;

	if (uclass_find_device_by_name(UCLASS_FS, "vfs_rootfs", &dev))
		return NULL;
	if (!device_active(dev))
		return NULL;

	return dev;
}

int vfs_init(void)
{
	struct udevice *dev;
	int ret;

	/* Already initialised? */
	dev = vfs_root();
	if (dev)
		return 0;

	ret = device_bind_driver(dm_root(), "vfs_rootfs", "vfs_rootfs", &dev);
	if (ret)
		return log_msg_ret("vib", ret);

	ret = device_probe(dev);
	if (ret)
		return log_msg_ret("vip", ret);

	ret = fs_mount(dev);
	if (ret)
		return log_msg_ret("vim", ret);

	return 0;
}

static const struct fs_ops vfs_rootfs_ops = {
	.mount		= vfs_rootfs_mount,
	.unmount	= vfs_rootfs_unmount,
	.lookup_dir	= vfs_rootfs_lookup_dir,
};

U_BOOT_DRIVER(vfs_rootfs) = {
	.name		= "vfs_rootfs",
	.id		= UCLASS_FS,
	.ops		= &vfs_rootfs_ops,
	.priv_auto	= sizeof(struct vfs_priv),
};

EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, vfs_init);
