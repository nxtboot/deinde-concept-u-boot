// SPDX-License-Identifier: GPL-2.0+
/*
 * U-Boot interface for isofs filesystem (Linux port)
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * This provides the interface between U-Boot's filesystem layer and
 * the isofs driver (ISO 9660 CD-ROM filesystem).
 */

#include <blk.h>
#include <fs.h>
#include <fs_legacy.h>
#include <part.h>
#include <malloc.h>
#include <linux/errno.h>
#include <linux/types.h>

#include "isofs.h"

/**
 * struct isofs_state - global mount state for the isofs driver
 *
 * @blk_dev: Block device descriptor
 * @partition: Partition info
 * @sb: Superblock pointer
 * @mounted: Whether a filesystem is currently mounted
 */
static struct isofs_state {
	struct blk_desc *blk_dev;
	struct disk_partition partition;
	struct super_block *sb;
	bool mounted;
} ifs;

/**
 * struct isofs_dir_actor_ctx - Context for directory listing callback
 * @ctx: Base dir_context (must be first)
 * @print: Whether to print entries
 */
struct isofs_dir_actor_ctx {
	struct dir_context ctx;
	bool print;
};

/**
 * struct isofs_dir - isofs directory stream state
 * @parent: base fs_dir_stream structure
 * @dirent: directory entry to return to caller
 * @dir_inode: pointer to directory inode
 * @file: file structure for readdir
 * @entry_found: flag set by actor when entry is captured
 */
struct isofs_dir {
	struct fs_dir_stream parent;
	struct fs_dirent dirent;
	struct inode *dir_inode;
	struct file file;
	bool entry_found;
};

struct isofs_readdir_ctx {
	struct dir_context ctx;
	struct isofs_dir *dir;
};

/**
 * isofs_probe() - Mount an ISO 9660 filesystem
 * @fs_dev_desc: Block device descriptor
 * @fs_partition: Partition info
 * Return: 0 on success, negative on error
 */
int isofs_probe(struct blk_desc *fs_dev_desc,
		struct disk_partition *fs_partition)
{
	struct isofs_options *opt;
	struct super_block *sb;
	struct fs_context fc;
	loff_t part_offset;
	u8 *buf;
	int ret;

	if (!fs_dev_desc)
		return -EINVAL;

	/* Close any previous mount */
	if (ifs.sb)
		isofs_close();

	/* Initialise inode cache */
	ret = isofs_init_inodecache();
	if (ret)
		return ret;

	/* Allocate super_block */
	sb = kzalloc(sizeof(*sb), GFP_KERNEL);
	if (!sb) {
		ret = -ENOMEM;
		goto err_destroy_cache;
	}
	INIT_LIST_HEAD(&sb->s_inodes);

	/* Allocate block_device */
	sb->s_bdev = kzalloc(sizeof(*sb->s_bdev), GFP_KERNEL);
	if (!sb->s_bdev) {
		ret = -ENOMEM;
		goto err_free_sb;
	}

	/* Initialise super_block fields */
	sb->s_bdev->bd_super = sb;
	sb->s_bdev->bd_blk = fs_dev_desc->bdev;
	sb->s_bdev->bd_part_start = fs_partition ? fs_partition->start : 0;
	sb->s_blocksize = ISOFS_BLOCK_SIZE;
	sb->s_blocksize_bits = ISOFS_BLOCK_BITS;
	sb->s_flags = SB_RDONLY;  /* ISO 9660 is always read-only */

	/* Save device info */
	ifs.blk_dev = fs_dev_desc;
	if (fs_partition)
		ifs.partition = *fs_partition;
	else
		memset(&ifs.partition, '\0', sizeof(ifs.partition));
	ifs.mounted = true;

	/* Read first sector to verify it's an ISO filesystem */
	part_offset = fs_partition ?
		(loff_t)fs_partition->start * fs_dev_desc->blksz : 0;

	buf = malloc(ISOFS_BLOCK_SIZE);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_bdev;
	}

	/*
	 * Read system area block 16 where the primary volume descriptor
	 * should be located.
	 */
	if (blk_read(fs_dev_desc->bdev,
		     (part_offset + 16 * ISOFS_BLOCK_SIZE) / fs_dev_desc->blksz,
		     ISOFS_BLOCK_SIZE / fs_dev_desc->blksz, buf) !=
	    ISOFS_BLOCK_SIZE / fs_dev_desc->blksz) {
		ret = -EIO;
		goto err_free_buf;
	}

	/* Quick check for ISO standard ID "CD001" at offset 1 */
	if (strncmp((char *)buf + 1, ISO_STANDARD_ID, 5)) {
		ret = -EINVAL;
		goto err_free_buf;
	}
	free(buf);

	/* Set up mount options */
	opt = kzalloc(sizeof(*opt), GFP_KERNEL);
	if (!opt) {
		ret = -ENOMEM;
		goto err_free_bdev;
	}

	opt->map = 'n';
	opt->rock = 1;
	opt->joliet = 1;
	opt->cruft = 0;
	opt->hide = 0;
	opt->showassoc = 0;
	opt->check = 'u';
	opt->nocompress = 0;
	opt->blocksize = ISOFS_BLOCK_SIZE;
	opt->fmode = ISOFS_INVALID_MODE;
	opt->dmode = ISOFS_INVALID_MODE;
	opt->uid_set = 0;
	opt->gid_set = 0;
	opt->gid = GLOBAL_ROOT_GID;
	opt->uid = GLOBAL_ROOT_UID;
	opt->iocharset = NULL;
	opt->overriderockperm = 0;
	opt->session = -1;
	opt->sbsector = -1;

	/* Set up fs_context */
	memset(&fc, '\0', sizeof(fc));
	fc.fs_private = opt;
	fc.sb_flags = SB_RDONLY;

	/* Mount the filesystem */
	ret = isofs_fill_super(sb, &fc);
	kfree(opt);
	if (ret) {
		printf("isofs: mount failed: %d\n", ret);
		goto err_free_bdev;
	}

	ifs.sb = sb;
	return 0;

err_free_buf:
	free(buf);
err_free_bdev:
	kfree(sb->s_bdev);
err_free_sb:
	kfree(sb);
err_destroy_cache:
	isofs_destroy_inodecache();
	ifs.mounted = false;

	return ret;
}

/**
 * isofs_close() - Unmount the ISO 9660 filesystem
 */
void isofs_close(void)
{
	struct super_block *sb = ifs.sb;

	if (!sb)
		return;

	/* Free all inodes */
	isofs_free_inodes(sb);

	/* Free root dentry */
	kfree(sb->s_root);
	sb->s_root = NULL;

	/* Free sb_info */
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;

	/* Clear cached buffers for this device before freeing it */
	bh_cache_clear(sb->s_bdev);

	/* Free block device */
	kfree(sb->s_bdev);
	kfree(sb);

	ifs.sb = NULL;

	/* Destroy inode cache */
	isofs_destroy_inodecache();

	ifs.blk_dev = NULL;
	ifs.mounted = false;
}

/**
 * isofs_resolve_path() - Resolve a path to an inode
 *
 * This duplicates ext4l_resolve_path_internal(). Consider refactoring
 * into a shared vfs_resolve_path() in linux_fs.c with a lookup callback.
 *
 * @path: Path to resolve
 * @inodep: Output inode pointer
 * Return: 0 on success, negative on error
 */
static int isofs_resolve_path(const char *path, struct inode **inodep)
{
	char *path_copy, *component, *next_component;
	struct dentry *dentry, *result;
	struct inode *dir;
	int ret;

	if (!ifs.mounted)
		return -ENODEV;

	dir = ifs.sb->s_root->d_inode;

	if (!path || !*path || !strcmp(path, "/")) {
		*inodep = dir;
		return 0;
	}

	path_copy = strdup(path);
	if (!path_copy)
		return -ENOMEM;

	component = path_copy;
	if (*component == '/')
		component++;

	while (component && *component) {
		next_component = strchr(component, '/');
		if (next_component) {
			*next_component = '\0';
			next_component++;
		}

		if (!*component || !strcmp(component, ".")) {
			component = next_component;
			continue;
		}

		dentry = kzalloc(sizeof(*dentry), GFP_KERNEL);
		if (!dentry) {
			ret = -ENOMEM;
			goto out_free;
		}

		dentry->d_name.name = component;
		dentry->d_name.len = strlen(component);
		dentry->d_sb = ifs.sb;
		dentry->d_parent = NULL;

		result = isofs_lookup(dir, dentry, 0);

		if (IS_ERR(result)) {
			ret = PTR_ERR(result);
			kfree(dentry);
			goto out_free;
		}

		if (result) {
			if (!result->d_inode) {
				if (result != dentry)
					kfree(dentry);
				kfree(result);
				ret = -ENOENT;
				goto out_free;
			}
			dir = result->d_inode;
			if (result != dentry)
				kfree(dentry);
			kfree(result);
		} else {
			if (!dentry->d_inode) {
				kfree(dentry);
				ret = -ENOENT;
				goto out_free;
			}
			dir = dentry->d_inode;
			kfree(dentry);
		}

		if (!dir) {
			ret = -ENOENT;
			goto out_free;
		}

		/*
		 * Follow symlinks (Rock Ridge). Read the link target and
		 * resolve recursively. Limit depth to prevent loops.
		 */
		if (S_ISLNK(dir->i_mode)) {
			/* Symlinks not yet implemented for isofs */
			ret = -ENOENT;
			goto out_free;
		}

		component = next_component;
	}

	*inodep = dir;
	ret = 0;

out_free:
	free(path_copy);

	return ret;
}

static int isofs_dir_actor(struct dir_context *ctx, const char *name,
			   int namelen, loff_t offset, u64 ino,
			   unsigned int d_type)
{
	char namebuf[256];

	if (namelen >= sizeof(namebuf))
		namelen = sizeof(namebuf) - 1;
	memcpy(namebuf, name, namelen);
	namebuf[namelen] = '\0';

	if (d_type == DT_DIR)
		printf("            %s/\n", namebuf);
	else if (d_type == DT_LNK)
		printf("    <SYM>   %s\n", namebuf);
	else
		printf("            %s\n", namebuf);

	return 0;
}

int isofs_ls(const char *dirname)
{
	struct dir_context ctx;
	struct inode *dir;
	struct file file;
	int ret;

	ret = isofs_resolve_path(dirname, &dir);
	if (ret)
		return ret;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	memset(&file, '\0', sizeof(file));
	file.f_inode = dir;
	file.f_mapping = dir->i_mapping;

	memset(&ctx, '\0', sizeof(ctx));
	ctx.actor = isofs_dir_actor;

	/* Call isofs_readdir via the file operations */
	if (dir->i_fop && dir->i_fop->iterate_shared)
		ret = dir->i_fop->iterate_shared(&file, &ctx);
	else
		ret = -ENOTDIR;

	return ret;
}

int isofs_exists(const char *filename)
{
	struct inode *inode;

	if (!filename)
		return 0;

	if (isofs_resolve_path(filename, &inode))
		return 0;

	return 1;
}

int isofs_size(const char *filename, loff_t *sizep)
{
	struct inode *inode;
	int ret;

	ret = isofs_resolve_path(filename, &inode);
	if (ret)
		return ret;

	*sizep = inode->i_size;

	return 0;
}

int isofs_read(const char *filename, void *buf, loff_t offset, loff_t len,
	       loff_t *actread)
{
	uint copy_len, blk_off, blksize;
	loff_t bytes_left, file_size;
	struct buffer_head *bh;
	struct inode *inode;
	sector_t block;
	char *dst;
	int ret;

	*actread = 0;

	ret = isofs_resolve_path(filename, &inode);
	if (ret) {
		printf("** File not found %s **\n", filename);
		return ret;
	}

	file_size = inode->i_size;
	if (offset >= file_size)
		return 0;

	/* If len is 0, read the whole file from offset */
	if (!len)
		len = file_size - offset;

	/* Clamp to file size */
	if (offset + len > file_size)
		len = file_size - offset;

	blksize = inode->i_sb->s_blocksize;
	bytes_left = len;
	dst = buf;

	while (bytes_left > 0) {
		/* Calculate logical block number and offset within block */
		block = offset / blksize;
		blk_off = offset % blksize;

		/*
		 * Use the uncached read path: the buffer cache is shared
		 * across the whole VFS layer, and populating it from a
		 * streaming file read of an arbitrarily large file would
		 * exhaust the malloc heap before the read completes.
		 */
		bh = isofs_bread_uncached(inode, block);
		if (!bh)
			return -EIO;

		/* Calculate how much to copy from this block */
		copy_len = blksize - blk_off;
		if (copy_len > bytes_left)
			copy_len = bytes_left;

		memcpy(dst, bh->b_data + blk_off, copy_len);
		brelse(bh);

		dst += copy_len;
		offset += copy_len;
		bytes_left -= copy_len;
		*actread += copy_len;
	}

	return 0;
}

static int isofs_opendir_actor(struct dir_context *ctx, const char *name,
			       int namelen, loff_t offset, u64 ino,
			       unsigned int d_type)
{
	struct isofs_readdir_ctx *rctx;
	struct dentry de, *result;
	struct fs_dirent *dent;
	struct isofs_dir *dir;

	rctx = container_of(ctx, struct isofs_readdir_ctx, ctx);
	dir = rctx->dir;
	dent = &dir->dirent;

	namelen = min(namelen, (int)FS_DIRENT_NAME_LEN - 1);
	memcpy(dent->name, name, namelen);
	dent->name[namelen] = '\0';

	dent->size = 0;
	dent->type = FS_DT_REG;

	/* "." and ".." are always directories */
	if (namelen <= 2 && name[0] == '.') {
		dent->type = FS_DT_DIR;
	} else {
		/* Look up the entry to get its inode for size and type */
		memset(&de, '\0', sizeof(de));
		de.d_name.name = (const unsigned char *)dent->name;
		de.d_name.len = namelen;
		de.d_sb = dir->dir_inode->i_sb;

		result = isofs_lookup(dir->dir_inode, &de, 0);
		if (!IS_ERR_OR_NULL(result) && result->d_inode) {
			dent->size = result->d_inode->i_size;
			if (S_ISDIR(result->d_inode->i_mode))
				dent->type = FS_DT_DIR;
			else if (S_ISLNK(result->d_inode->i_mode))
				dent->type = FS_DT_LNK;
		} else if (de.d_inode) {
			dent->size = de.d_inode->i_size;
			if (S_ISDIR(de.d_inode->i_mode))
				dent->type = FS_DT_DIR;
			else if (S_ISLNK(de.d_inode->i_mode))
				dent->type = FS_DT_LNK;
		}
	}

	dir->entry_found = true;

	/* Return non-zero to stop after one entry */
	return 1;
}

int isofs_opendir(const char *filename, struct fs_dir_stream **dirsp)
{
	struct isofs_dir *dir;
	struct inode *inode;
	int ret;

	if (!ifs.mounted)
		return -ENODEV;

	ret = isofs_resolve_path(filename, &inode);
	if (ret)
		return ret;

	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	dir = calloc(1, sizeof(*dir));
	if (!dir)
		return -ENOMEM;

	dir->dir_inode = inode;
	dir->file.f_inode = inode;
	dir->file.f_mapping = inode->i_mapping;

	*dirsp = (struct fs_dir_stream *)dir;

	return 0;
}

int isofs_readdir(struct fs_dir_stream *dirs, struct fs_dirent **dentp)
{
	struct isofs_dir *dir = container_of(dirs, struct isofs_dir, parent);
	struct isofs_readdir_ctx rctx;
	int ret;

	if (!ifs.mounted)
		return -ENODEV;

	memset(&dir->dirent, '\0', sizeof(dir->dirent));
	dir->entry_found = false;

	memset(&rctx, '\0', sizeof(rctx));
	rctx.ctx.actor = isofs_opendir_actor;
	rctx.ctx.pos = dir->file.f_pos;
	rctx.dir = dir;

	if (dir->dir_inode->i_fop && dir->dir_inode->i_fop->iterate_shared)
		ret = dir->dir_inode->i_fop->iterate_shared(&dir->file,
							    &rctx.ctx);
	else
		ret = -ENOTDIR;

	dir->file.f_pos = rctx.ctx.pos;

	if (ret < 0)
		return ret;

	if (!dir->entry_found)
		return -ENOENT;

	*dentp = &dir->dirent;

	return 0;
}

void isofs_closedir(struct fs_dir_stream *dirs)
{
	free(dirs);
}
