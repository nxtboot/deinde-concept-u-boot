// SPDX-License-Identifier: GPL-2.0+
/*
 * U-Boot interface for ext4l filesystem (Linux port)
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * This provides the interface between U-Boot's filesystem layer and
 * the ext4l driver.
 */

#include <blk.h>
#include <env.h>
#include <ext4l.h>
#include <fs.h>
#include <fs_legacy.h>
#include <part.h>
#include <malloc.h>
#include <u-boot/uuid.h>
#include <linux/errno.h>
#include <linux/jbd2.h>
#include <linux/math64.h>
#include <linux/types.h>

#include "ext4_uboot.h"
#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"

/* Global state for the legacy filesystem interface */
static struct ext4l_state efs;

/**
 * ext4l_get_blk() - Get the current block device
 *
 * Return: Block device descriptor or NULL if not mounted
 */
struct udevice *ext4l_get_blk(void)
{
	if (!efs.mounted)
		return NULL;

	return efs.blk;
}

/**
 * ext4l_get_partition() - Get the current partition info
 *
 * Return: Partition info pointer
 */
struct disk_partition *ext4l_get_partition(void)
{
	return &efs.partition;
}

/**
 * ext4l_get_uuid() - Get the filesystem UUID
 *
 * @uuid: Buffer to receive the 16-byte UUID
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_get_uuid(u8 *uuid)
{
	if (!efs.sb)
		return -ENODEV;
	memcpy(uuid, efs.sb->s_uuid.b, 16);
	return 0;
}

/**
 * ext4l_uuid() - Get the filesystem UUID as a string
 *
 * @uuid_str: Buffer to receive the UUID string (must be at least 37 bytes)
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_uuid(char *uuid_str)
{
	u8 uuid[16];
	int ret;

	ret = ext4l_get_uuid(uuid);
	if (ret)
		return ret;
	uuid_bin_to_str(uuid, uuid_str, UUID_STR_FORMAT_STD);

	return 0;
}

/**
 * ext4l_statfs() - Get filesystem statistics
 *
 * @stats: Pointer to fs_statfs structure to fill
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_statfs(struct fs_statfs *stats)
{
	struct ext4_super_block *es;

	if (!efs.sb)
		return -ENODEV;

	es = EXT4_SB(efs.sb)->s_es;
	stats->bsize = efs.sb->s_blocksize;
	stats->blocks = ext4_blocks_count(es);
	stats->bfree = ext4_free_blocks_count(es);

	return 0;
}

/**
 * ext4l_set_blk_dev() - Set the block device for ext4l operations
 *
 * @blk_dev: Block device descriptor
 * @partition: Partition info (can be NULL for whole disk)
 */
void ext4l_set_blk_dev(struct udevice *blk_dev,
		       struct disk_partition *partition)
{
	efs.blk = blk_dev;
	if (partition)
		memcpy(&efs.partition, partition, sizeof(efs.partition));
	else
		memset(&efs.partition, 0, sizeof(efs.partition));
	efs.mounted = true;
}

/**
 * ext4l_clear_blk_dev() - Clear block device (unmount)
 */
void ext4l_clear_blk_dev(void)
{
	/* Clear buffer cache before unmounting */
	bh_cache_clear();

	efs.blk = NULL;
	efs.mounted = false;
}

/**
 * ext4l_free_sb() - Free superblock and associated resources
 * @sb: Superblock to free
 * @skip_io: If true, skip all I/O operations (for forced close)
 *
 * Releases all resources associated with the superblock including the journal,
 * caches, inodes, and the superblock structure itself.
 */
static void ext4l_free_sb(struct super_block *sb, bool skip_io)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	/*
	 * Destroy journal first to properly clean up all buffers.
	 * If skip_io is set, the device may be invalid so skip
	 * journal destroy entirely - it will be recovered on next mount.
	 */
	if (sbi->s_journal && !skip_io)
		ext4_journal_destroy(sbi, sbi->s_journal);

	/* Commit superblock if device is valid and I/O is allowed */
	if (!skip_io)
		ext4_commit_super(sb);

	/* Release superblock buffer */
	brelse(sbi->s_sbh);

	/* Unregister lazy init and free if no longer needed */
	ext4_unregister_li_request(sb);
	ext4_destroy_lazy_init();

	/* Free mballoc data */
	ext4_mb_release(sb);

	/* Release system zone */
	ext4_release_system_zone(sb);

	/* Destroy xattr caches */
	ext4_xattr_destroy_cache(sbi->s_ea_inode_cache);
	sbi->s_ea_inode_cache = NULL;
	ext4_xattr_destroy_cache(sbi->s_ea_block_cache);
	sbi->s_ea_block_cache = NULL;

	/* Free group descriptors and flex groups */
	ext4_group_desc_free(sbi);
	ext4_flex_groups_free(sbi);

	/* Evict all inodes before destroying caches */
	while (!list_empty(&sb->s_inodes)) {
		struct inode *inode;
		struct ext4_inode_info *ei;

		inode = list_first_entry(&sb->s_inodes,
					 struct inode, i_sb_list);
		list_del_init(&inode->i_sb_list);
		/* Clear extent status and free the inode */
		ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
		ei = EXT4_I(inode);
		kfree(ei);
	}

	/* Free root dentry */
	if (sb->s_root) {
		kfree(sb->s_root);
		sb->s_root = NULL;
	}

	/* Free sbi */
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);

	/* Free structures allocated in ext4l_probe() */
	kfree(sb->s_bdev->bd_mapping);
	kfree(sb->s_bdev);
	kfree(sb);
}

/**
 * ext4l_close_internal() - Internal close function
 * @skip_io: If true, skip all I/O operations (for forced close)
 *
 * When called from the safeguard in ext4l_probe(), the device may be
 * invalid (rebound to a different file), so skip_io should be true to
 * avoid crashes when trying to write to the device.
 */
static void ext4l_close_internal(bool skip_io)
{
	struct super_block *sb = efs.sb;

	if (efs.open_dirs > 0)
		return;

	if (sb)
		ext4l_free_sb(sb, skip_io);

	efs.sb = NULL;

	/*
	 * Force cleanup of any remaining journal_heads before clearing
	 * the buffer cache. This ensures no stale journal_head references
	 * survive to the next mount. This is critical even when skip_io
	 * is true - we MUST disconnect journal_heads before freeing
	 * buffer_heads to avoid dangling pointers.
	 */
	bh_cache_release_jbd();

	ext4l_clear_blk_dev();

	/*
	 * Clean up ext4 and JBD2 global state so it can be properly
	 * reinitialised on the next mount. This is important in U-Boot
	 * where we may mount/unmount filesystems multiple times in a
	 * single session.
	 *
	 * Even when skip_io is true (journal wasn't properly destroyed),
	 * we must destroy the caches to free all orphaned journal_heads.
	 * The next mount will reinitialise fresh caches.
	 */
	ext4_exit_system_zone();
	ext4_exit_es();
	if (IS_ENABLED(CONFIG_EXT4_WRITE))
		ext4_exit_mballoc();
	if (IS_ENABLED(CONFIG_EXT4_JOURNAL))
		jbd2_journal_exit_global();
	destroy_inodecache();
}

int ext4l_probe(struct blk_desc *fs_dev_desc,
		struct disk_partition *fs_partition)
{
	struct ext4_fs_context *ctx;
	struct super_block *sb;
	struct fs_context *fc;
	loff_t part_offset;
	__le16 *magic;
	u8 *buf;
	int ret;

	if (!fs_dev_desc)
		return -EINVAL;

	/*
	 * Ensure any previous mount is properly closed before mounting again.
	 * This prevents resource leaks if probe is called without close.
	 *
	 * Since we're being called while a previous mount exists, we can't
	 * trust the old device state (it may have been rebound to a different
	 * file). Use skip_io=true to skip all I/O during close.
	 */
	if (efs.sb)
		ext4l_close_internal(true);

	/* Initialise message buffer for recording ext4 messages */
	ext4l_msg_init();

	/* Initialise CRC32C table for checksum verification */
	ext4l_crc32c_init();

	/* Initialise journal subsystem if enabled */
	if (IS_ENABLED(CONFIG_EXT4_JOURNAL)) {
		ret = jbd2_journal_init_global();
		if (ret)
			return ret;
	}

	/* Initialise multi-block allocator for write support */
	if (IS_ENABLED(CONFIG_EXT4_WRITE)) {
		ret = ext4_init_mballoc();
		if (ret)
			return ret;
	}

	/* Initialise extent status cache */
	ret = ext4_init_es();
	if (ret)
		return ret;

	/* Initialise system zone for block validity checking */
	ret = ext4_init_system_zone();
	if (ret)
		goto err_exit_es;

	/* Allocate super_block */
	sb = kzalloc(sizeof(struct super_block), GFP_KERNEL);
	if (!sb) {
		ret = -ENOMEM;
		goto err_exit_es;
	}
	INIT_LIST_HEAD(&sb->s_inodes);

	/* Allocate block_device */
	sb->s_bdev = kzalloc(sizeof(struct block_device), GFP_KERNEL);
	if (!sb->s_bdev) {
		ret = -ENOMEM;
		goto err_free_sb;
	}

	sb->s_bdev->bd_mapping = kzalloc(sizeof(struct address_space), GFP_KERNEL);
	if (!sb->s_bdev->bd_mapping) {
		ret = -ENOMEM;
		goto err_free_bdev;
	}

	/* Initialise super_block fields */
	sb->s_bdev->bd_super = sb;
	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;
	snprintf(sb->s_id, sizeof(sb->s_id), "ext4l_mmc%d",
		 fs_dev_desc->devnum);
	sb->s_flags = 0;
	sb->s_fs_info = NULL;

	/* Allocate fs_context */
	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL);
	if (!fc) {
		ret = -ENOMEM;
		goto err_free_mapping;
	}

	/* Allocate ext4_fs_context */
	ctx = kzalloc(sizeof(struct ext4_fs_context), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto err_free_fc;
	}

	/* Initialise fs_context fields */
	fc->fs_private = ctx;
	fc->sb_flags |= SB_I_VERSION;
	fc->root = (struct dentry *)sb;	/* Hack: store sb for ext4_fill_super */

	buf = malloc(BLOCK_SIZE + 512);
	if (!buf) {
		ret = -ENOMEM;
		goto err_free_ctx;
	}

	/* Calculate partition offset in bytes */
	part_offset = fs_partition ? (loff_t)fs_partition->start * fs_dev_desc->blksz : 0;

	/* Read sectors containing the superblock */
	if (blk_dread(fs_dev_desc,
		      div_u64(part_offset + BLOCK_SIZE, fs_dev_desc->blksz),
		      2, buf) != 2) {
		ret = -EIO;
		goto err_free_buf;
	}

	/* Check magic number within superblock */
	magic = (__le16 *)(buf + (BLOCK_SIZE % fs_dev_desc->blksz) +
			   offsetof(struct ext4_super_block, s_magic));
	if (le16_to_cpu(*magic) != EXT4_SUPER_MAGIC) {
		ret = -EINVAL;
		goto err_free_buf;
	}

	/* Set block device for buffer I/O */
	ext4l_set_blk_dev(fs_dev_desc->bdev, fs_partition);

	/*
	 * Test if device supports writes by writing back the same data.
	 * If write returns 0, the device is read-only (e.g. LUKS/blkmap_crypt)
	 */
	if (blk_dwrite(fs_dev_desc,
		       div_u64(part_offset + BLOCK_SIZE, fs_dev_desc->blksz),
		       2, buf) != 2) {
		sb->s_bdev->read_only = true;
		sb->s_flags |= SB_RDONLY;
	}
	free(buf);

	/* Mount the filesystem */
	ret = ext4_fill_super(sb, fc);
	if (ret) {
		printf("ext4l: ext4_fill_super failed: %d\n", ret);
		goto err_free_ctx;
	}

	/* Store super_block for later operations */
	efs.sb = sb;

	/* Free mount context - no longer needed after successful mount */
	kfree(ctx);
	kfree(fc);

	/* Print messages if ext4l_msgs environment variable is set */
	if (env_get_yesno("ext4l_msgs") == 1)
		ext4l_print_msgs();

	return 0;

err_free_buf:
	free(buf);
err_free_ctx:
	kfree(ctx);
err_free_fc:
	kfree(fc);
err_free_mapping:
	kfree(sb->s_bdev->bd_mapping);
err_free_bdev:
	kfree(sb->s_bdev);
err_free_sb:
	kfree(sb);
err_exit_es:
	ext4_exit_es();
	return ret;
}

/**
 * ext4l_read_symlink() - Read the target of a symlink inode
 *
 * @inode: Symlink inode
 * @target: Buffer to store target
 * @max_len: Maximum length of target buffer
 * Return: Length of target on success, negative on error
 */
static int ext4l_read_symlink(struct inode *inode, char *target, size_t max_len)
{
	struct buffer_head *bh;
	size_t len;

	if (!S_ISLNK(inode->i_mode))
		return -EINVAL;

	if (ext4_inode_is_fast_symlink(inode)) {
		/* Fast symlink: target stored in i_data */
		len = inode->i_size;
		if (len >= max_len)
			len = max_len - 1;
		memcpy(target, EXT4_I(inode)->i_data, len);
		target[len] = '\0';
		return len;
	}

	/* Slow symlink: target stored in data block */
	bh = ext4_bread(NULL, inode, 0, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);
	if (!bh)
		return -EIO;

	len = inode->i_size;
	if (len >= max_len)
		len = max_len - 1;
	memcpy(target, bh->b_data, len);
	target[len] = '\0';
	brelse(bh);

	return len;
}

/**
 * ext4l_resolve_path_internal() - Resolve path with symlink following
 *
 * @path: Path to resolve
 * @inodep: Output inode pointer
 * @depth: Current recursion depth (for symlink loop detection)
 * Return: 0 on success, negative on error
 */
static int ext4l_resolve_path_internal(const char *path, struct inode **inodep,
				       int depth)
{
	struct inode *dir;
	struct dentry *dentry, *result;
	char *path_copy, *component, *next_component;
	int ret;

	/* Prevent symlink loops */
	if (depth > 8)
		return -ELOOP;

	if (!efs.mounted) {
		ext4_debug("ext4l_resolve_path: filesystem not mounted\n");
		return -ENODEV;
	}

	dir = efs.sb->s_root->d_inode;

	if (!path || !*path || (strcmp(path, "/") == 0)) {
		*inodep = dir;
		return 0;
	}

	path_copy = strdup(path);
	if (!path_copy)
		return -ENOMEM;

	component = path_copy;
	/* Skip leading slash */
	if (*component == '/')
		component++;

	while (component && *component) {
		next_component = strchr(component, '/');
		if (next_component) {
			*next_component = '\0';
			next_component++;
		}

		if (!*component) {
			component = next_component;
			continue;
		}

		/* Handle special directory entries */
		if (strcmp(component, ".") == 0) {
			component = next_component;
			continue;
		}
		if (strcmp(component, "..") == 0) {
			/* Parent directory - look up ".." entry */
			dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
			if (!dentry) {
				free(path_copy);
				return -ENOMEM;
			}
			dentry->d_name.name = "..";
			dentry->d_name.len = 2;
			dentry->d_sb = efs.sb;
			dentry->d_parent = NULL;

			result = ext4_lookup(dir, dentry, 0);
			if (IS_ERR(result)) {
				kfree(dentry);
				free(path_copy);
				return PTR_ERR(result);
			}
			if (result && result->d_inode) {
				dir = result->d_inode;
				if (result != dentry)
					kfree(dentry);
				kfree(result);
			} else if (dentry->d_inode) {
				dir = dentry->d_inode;
				kfree(dentry);
			} else {
				/* ".." not found - stay at root */
				kfree(dentry);
				if (result && result != dentry)
					kfree(result);
			}
			component = next_component;
			continue;
		}

		dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
		if (!dentry) {
			free(path_copy);
			return -ENOMEM;
		}

		dentry->d_name.name = component;
		dentry->d_name.len = strlen(component);
		dentry->d_sb = efs.sb;
		dentry->d_parent = NULL;

		result = ext4_lookup(dir, dentry, 0);

		if (IS_ERR(result)) {
			kfree(dentry);
			free(path_copy);
			return PTR_ERR(result);
		}

		if (result) {
			if (!result->d_inode) {
				if (result != dentry)
					kfree(dentry);
				kfree(result);
				free(path_copy);
				return -ENOENT;
			}
			dir = result->d_inode;
			if (result != dentry)
				kfree(dentry);
			kfree(result);
		} else {
			if (!dentry->d_inode) {
				kfree(dentry);
				free(path_copy);
				return -ENOENT;
			}
			dir = dentry->d_inode;
			kfree(dentry);
		}

		if (!dir) {
			free(path_copy);
			return -ENOENT;
		}

		/* Check if this is a symlink and follow it */
		if (S_ISLNK(dir->i_mode)) {
			char link_target[256];
			char *new_path;

			ret = ext4l_read_symlink(dir, link_target,
						 sizeof(link_target));
			if (ret < 0) {
				free(path_copy);
				return ret;
			}

			/* Build new path: link_target + remaining path */
			if (next_component && *next_component) {
				size_t target_len = strlen(link_target);
				size_t remaining_len = strlen(next_component);

				new_path = malloc(target_len + 1 +
						  remaining_len + 1);
				if (!new_path) {
					free(path_copy);
					return -ENOMEM;
				}
				strcpy(new_path, link_target);
				strcat(new_path, "/");
				strcat(new_path, next_component);
			} else {
				new_path = strdup(link_target);
				if (!new_path) {
					free(path_copy);
					return -ENOMEM;
				}
			}

			free(path_copy);

			/* Recursively resolve the new path */
			ret = ext4l_resolve_path_internal(new_path, inodep,
							  depth + 1);
			free(new_path);
			return ret;
		}

		component = next_component;
	}

	free(path_copy);
	*inodep = dir;
	return 0;
}

/**
 * ext4l_resolve_path() - Resolve path to inode
 *
 * @path: Path to resolve
 * @inodep: Output inode pointer
 * Return: 0 on success, negative on error
 */
static int ext4l_resolve_path(const char *path, struct inode **inodep)
{
	return ext4l_resolve_path_internal(path, inodep, 0);
}

/**
 * ext4l_dir_actor() - Directory entry callback for ext4_readdir
 *
 * @ctx: Directory context
 * @name: Entry name
 * @namelen: Length of name
 * @offset: Directory offset
 * @ino: Inode number
 * @d_type: Entry type
 * Return: 0 to continue iteration
 */
static int ext4l_dir_actor(struct dir_context *ctx, const char *name,
			   int namelen, loff_t offset, u64 ino,
			   unsigned int d_type)
{
	struct inode *inode;
	char namebuf[256];

	/* Copy the name to a null-terminated buffer */
	if (namelen >= sizeof(namebuf))
		namelen = sizeof(namebuf) - 1;
	memcpy(namebuf, name, namelen);
	namebuf[namelen] = '\0';

	/* Look up the inode to get file size */
	inode = ext4_iget(efs.sb, ino, 0);
	if (IS_ERR(inode)) {
		printf(" %8s   %s\n", "?", namebuf);
		return 0;
	}

	if (d_type == DT_DIR || S_ISDIR(inode->i_mode))
		printf("            %s/\n", namebuf);
	else if (d_type == DT_LNK || S_ISLNK(inode->i_mode))
		printf("    <SYM>   %s\n", namebuf);
	else
		printf(" %8lld   %s\n", (long long)inode->i_size, namebuf);

	return 0;
}

int ext4l_ls(const char *dirname)
{
	struct inode *dir;
	struct file file;
	struct dir_context ctx;
	int ret;

	ret = ext4l_resolve_path(dirname, &dir);
	if (ret)
		return ret;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	memset(&file, 0, sizeof(file));
	file.f_inode = dir;
	file.f_mapping = dir->i_mapping;

	/* Allocate private_data for readdir */
	file.private_data = kzalloc(sizeof(struct dir_private_info), GFP_KERNEL);
	if (!file.private_data)
		return -ENOMEM;

	memset(&ctx, 0, sizeof(ctx));
	ctx.actor = ext4l_dir_actor;

	ret = ext4_readdir(&file, &ctx);

	if (file.private_data)
		ext4_htree_free_dir_info(file.private_data);

	return ret;
}

int ext4l_exists(const char *filename)
{
	struct inode *inode;

	if (!filename)
		return 0;

	if (ext4l_resolve_path(filename, &inode))
		return 0;

	return 1;
}

int ext4l_size(const char *filename, loff_t *sizep)
{
	struct inode *inode;
	int ret;

	ret = ext4l_resolve_path(filename, &inode);
	if (ret)
		return ret;

	*sizep = inode->i_size;

	return 0;
}

int ext4l_read(const char *filename, void *buf, loff_t offset, loff_t len,
	       loff_t *actread)
{
	uint copy_len, blk_off, blksize;
	loff_t bytes_left, file_size;
	struct buffer_head *bh;
	struct inode *inode;
	ext4_lblk_t block;
	char *dst;
	int ret;

	*actread = 0;

	ret = ext4l_resolve_path(filename, &inode);
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
		u32 rem;

		/* Calculate logical block number and offset within block */
		block = div_u64_rem(offset, blksize, &rem);
		blk_off = rem;

		/* Read the block */
		bh = ext4_bread(NULL, inode, block, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
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

/**
 * ext4l_resolve_file() - Resolve a file path for write operations
 * @path: Path to process
 * @dir_dentryp: Returns parent directory dentry (caller must kfree)
 * @dentryp: Returns file dentry after lookup (caller must kfree)
 * @path_copyp: Returns path copy (caller must free)
 *
 * Common setup for write operations. Validates inputs, checks read-write
 * mount, parses path, resolves parent directory, creates dentries, and
 * performs lookup.
 *
 * Return: 0 on success, negative on error
 */
static int ext4l_resolve_file(const char *path, struct dentry **dir_dentryp,
			      struct dentry **dentryp, char **path_copyp)
{
	char *path_copy, *dir_path, *last_slash;
	struct dentry *dir_dentry, *dentry, *result;
	struct inode *dir_inode;
	const char *basename;
	int ret;

	if (!efs.sb)
		return -ENODEV;

	if (!path)
		return -EINVAL;

	/* Check if filesystem is mounted read-write */
	if (efs.sb->s_flags & SB_RDONLY)
		return -EROFS;

	/* Parse path to get parent directory and basename */
	path_copy = strdup(path);
	if (!path_copy)
		return -ENOMEM;

	last_slash = strrchr(path_copy, '/');
	if (last_slash) {
		*last_slash = '\0';
		dir_path = path_copy;
		basename = last_slash + 1;
		if (*dir_path == '\0')
			dir_path = "/";
	} else {
		dir_path = "/";
		basename = path;
	}

	/* Resolve parent directory */
	ret = ext4l_resolve_path(dir_path, &dir_inode);
	if (ret) {
		free(path_copy);
		return ret;
	}

	if (!S_ISDIR(dir_inode->i_mode)) {
		free(path_copy);
		return -ENOTDIR;
	}

	/* Create dentry for parent directory */
	dir_dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
	if (!dir_dentry) {
		free(path_copy);
		return -ENOMEM;
	}
	dir_dentry->d_inode = dir_inode;
	dir_dentry->d_sb = dir_inode->i_sb;

	/* Create dentry for the file */
	dentry = kzalloc(sizeof(struct dentry), GFP_KERNEL);
	if (!dentry) {
		kfree(dir_dentry);
		free(path_copy);
		return -ENOMEM;
	}
	dentry->d_name.name = basename;
	dentry->d_name.len = strlen(basename);
	dentry->d_sb = dir_inode->i_sb;
	dentry->d_parent = dir_dentry;

	/* Look up the file */
	result = ext4_lookup(dir_inode, dentry, 0);
	if (IS_ERR(result)) {
		kfree(dentry);
		kfree(dir_dentry);
		free(path_copy);
		return PTR_ERR(result);
	}

	*dir_dentryp = dir_dentry;
	*dentryp = dentry;
	*path_copyp = path_copy;

	return 0;
}

static int ext4l_write_file(struct dentry *dir_dentry, struct dentry *dentry,
			    void *buf, loff_t offset, loff_t len,
			    loff_t *actwrite)
{
	struct inode *dir = dir_dentry->d_inode;
	handle_t *handle = NULL;
	struct buffer_head *bh;
	struct inode *inode;
	loff_t pos, end;
	umode_t mode;
	int ret;

	if (dentry->d_inode) {
		/* File exists - use the existing inode for overwrite */
		inode = dentry->d_inode;
	} else {
		/* File does not exist, create it */
		/* Mode: 0644 (rw-r--r--) | S_IFREG */
		mode = S_IFREG | 0644;
		ret = ext4_create(&nop_mnt_idmap, dir, dentry, mode, true);
		if (ret)
			return ret;

		inode = dentry->d_inode;
	}
	if (!inode)
		return -EIO;

	/*
	 * Attach jinode for journaling if needed (like ext4_file_open does).
	 * This is required for ordered data mode.
	 */
	ret = ext4_inode_attach_jinode(inode);
	if (ret < 0)
		return ret;

	/*
	 * Start a journal handle for the write operation.
	 * U-Boot uses a synchronous single-transaction model where
	 * ext4_journal_stop() commits immediately for crash safety.
	 */
	handle = ext4_journal_start(inode, EXT4_HT_WRITE_PAGE,
				    EXT4_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	/* Write data to file */
	pos = offset;
	end = offset + len;
	while (pos < end) {
		ext4_lblk_t block = pos >> inode->i_blkbits;
		uint block_offset = pos & (inode->i_sb->s_blocksize - 1);
		uint bytes_to_write = inode->i_sb->s_blocksize - block_offset;
		int needed_credits = EXT4_DATA_TRANS_BLOCKS(inode->i_sb);

		if (pos + bytes_to_write > end)
			bytes_to_write = end - pos;

		/*
		 * Ensure we have enough journal credits for this block.
		 * Each block allocation can use up to EXT4_DATA_TRANS_BLOCKS
		 * credits. Try to extend, or restart the transaction if needed.
		 */
		ret = ext4_journal_ensure_credits(handle, needed_credits, 0);
		if (ret < 0)
			goto out_handle;

		bh = ext4_getblk(handle, inode, block, 0);

		if (IS_ERR(bh)) {
			ret = PTR_ERR(bh);
			goto out_handle;
		}
		if (!bh) {
			/* Block doesn't exist, allocate it */
			bh = ext4_getblk(handle, inode, block,
					 EXT4_GET_BLOCKS_CREATE);
			if (IS_ERR(bh)) {
				ret = PTR_ERR(bh);
				goto out_handle;
			}
			if (!bh) {
				ret = -EIO;
				goto out_handle;
			}
		}

		/* Get write access for journaling */
		ret = ext4_journal_get_write_access(handle, inode->i_sb, bh,
						    EXT4_JTR_NONE);
		if (ret) {
			brelse(bh);
			goto out_handle;
		}

		/* Copy data to buffer */
		memcpy(bh->b_data + block_offset, buf + (pos - offset),
		       bytes_to_write);

		/*
		 * In data=journal mode, file data goes through the journal.
		 * In data=ordered mode, write directly to disk.
		 */
		if (ext4_should_journal_data(inode)) {
			/* data=journal: write through journal */
			ret = ext4_handle_dirty_metadata(handle, inode, bh);
			if (ret) {
				brelse(bh);
				goto out_handle;
			}
		} else {
			/* data=ordered: write directly to disk */
			mark_buffer_dirty(bh);
			ret = sync_dirty_buffer(bh);
			if (ret) {
				brelse(bh);
				goto out_handle;
			}
		}

		brelse(bh);
		pos += bytes_to_write;
	}

	/* Update inode size */
	if (end > inode->i_size) {
		i_size_write(inode, end);
		/*
		 * Also update i_disksize in ext4_inode_info - this is what gets
		 * written to disk via ext4_fill_raw_inode -> ext4_isize_set
		 */
		EXT4_I(inode)->i_disksize = end;
		/* Mark inode dirty to update on disk */
		ext4_mark_inode_dirty(handle, inode);
	}

	*actwrite = len;
	ret = 0;

out_handle:
	/* Stop handle - this commits the transaction synchronously in U-Boot */
	if (handle) {
		int stop_ret = ext4_journal_stop(handle);

		if (stop_ret && !ret)
			ret = stop_ret;
	}

	return ret;
}

int ext4l_write(const char *filename, void *buf, loff_t offset, loff_t len,
		loff_t *actwrite)
{
	struct dentry *dir_dentry, *dentry;
	char *path_copy;
	int ret;

	if (!buf || !actwrite)
		return -EINVAL;

	ret = ext4l_resolve_file(filename, &dir_dentry, &dentry, &path_copy);
	if (ret)
		return ret;

	/* Call write implementation */
	ret = ext4l_write_file(dir_dentry, dentry, buf, offset, len, actwrite);

	/* Sync all dirty buffers - U-Boot has no journal thread */
	if (!ret) {
		int sync_ret = bh_cache_sync();

		if (sync_ret)
			ret = sync_ret;
	}

	kfree(dentry);
	kfree(dir_dentry);
	free(path_copy);
	return ret;
}

int ext4l_unlink(const char *filename)
{
	struct dentry *dentry, *dir_dentry;
	char *path_copy;
	int ret;

	ret = ext4l_resolve_file(filename, &dir_dentry, &dentry, &path_copy);
	if (ret)
		return ret;

	/* Check if file exists */
	if (!dentry->d_inode) {
		ret = -ENOENT;
		goto out;
	}

	/* Cannot unlink directories with unlink - use rmdir */
	if (S_ISDIR(dentry->d_inode->i_mode)) {
		ret = -EISDIR;
		goto out;
	}

	/* Unlink the file */
	ret = __ext4_unlink(dir_dentry->d_inode, &dentry->d_name,
			    dentry->d_inode, dentry);

	/*
	 * Release inode - this triggers ext4_evict_inode for nlink=0 inodes,
	 * which frees the data blocks and inode.
	 */
	if (dentry->d_inode) {
		iput(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* Sync all dirty buffers after inode eviction */
	if (!ret) {
		int sync_ret = bh_cache_sync();

		if (sync_ret)
			ret = sync_ret;
		/* Commit superblock with updated free counts */
		ext4_commit_super(efs.sb);
	}

out:
	kfree(dentry);
	kfree(dir_dentry);
	free(path_copy);
	return ret;
}

int ext4l_mkdir(const char *dirname)
{
	struct dentry *dentry, *dir_dentry, *result;
	char *path_copy;
	int ret;

	ret = ext4l_resolve_file(dirname, &dir_dentry, &dentry, &path_copy);
	if (ret)
		return ret;

	if (dentry->d_inode) {
		/* Directory already exists */
		ret = -EEXIST;
		goto out;
	}

	/* Create the directory with mode 0755 (rwxr-xr-x) */
	result = ext4_mkdir(&nop_mnt_idmap, dir_dentry->d_inode, dentry,
			    S_IFDIR | 0755);
	if (IS_ERR(result)) {
		ret = PTR_ERR(result);
		goto out;
	}

	ret = 0;

	/* Sync all dirty buffers */
	{
		int sync_ret = bh_cache_sync();

		if (sync_ret)
			ret = sync_ret;
		/* Commit superblock with updated free counts */
		ext4_commit_super(efs.sb);
	}

out:
	kfree(dentry);
	kfree(dir_dentry);
	free(path_copy);
	return ret;
}

int ext4l_ln(const char *filename, const char *linkname)
{
	struct dentry *dentry, *dir_dentry;
	char *path_copy;
	int ret;

	/*
	 * Note: The parameter naming follows U-Boot's convention:
	 * - filename: the target file the link should point to
	 * - linkname: the path of the symlink to create
	 */
	if (!filename)
		return -EINVAL;

	ret = ext4l_resolve_file(linkname, &dir_dentry, &dentry, &path_copy);
	if (ret)
		return ret;

	if (dentry->d_inode) {
		/* File already exists - delete it first (like ln -sf) */
		if (S_ISDIR(dentry->d_inode->i_mode)) {
			/* Cannot replace a directory with a symlink */
			ret = -EISDIR;
			goto out;
		}

		ret = __ext4_unlink(dir_dentry->d_inode, &dentry->d_name,
				    dentry->d_inode, dentry);
		if (ret)
			goto out;

		/* Release inode to free data blocks */
		iput(dentry->d_inode);
		dentry->d_inode = NULL;
	}

	/* Create the symlink - filename is what the link points to */
	ret = ext4_symlink(&nop_mnt_idmap, dir_dentry->d_inode, dentry,
			   filename);
	if (ret)
		goto out;

	/* Sync all dirty buffers */
	{
		int sync_ret = bh_cache_sync();

		if (sync_ret)
			ret = sync_ret;
		/* Commit superblock with updated free counts */
		ext4_commit_super(efs.sb);
	}

out:
	kfree(dentry);
	kfree(dir_dentry);
	free(path_copy);

	return ret;
}

int ext4l_rename(const char *old_path, const char *new_path)
{
	struct dentry *old_dentry, *new_dentry;
	struct dentry *old_dir_dentry, *new_dir_dentry;
	char *old_path_copy, *new_path_copy;
	int ret;

	/* Check new_path before ext4l_resolve_file checks old_path */
	if (!new_path)
		return -EINVAL;

	ret = ext4l_resolve_file(old_path, &old_dir_dentry, &old_dentry,
				 &old_path_copy);
	if (ret)
		return ret;

	if (!old_dentry->d_inode) {
		/* Source file doesn't exist */
		ret = -ENOENT;
		goto out_old;
	}

	ret = ext4l_resolve_file(new_path, &new_dir_dentry, &new_dentry,
				 &new_path_copy);
	if (ret)
		goto out_old;

	/* Perform the rename */
	ret = ext4_rename(&nop_mnt_idmap, old_dir_dentry->d_inode, old_dentry,
			  new_dir_dentry->d_inode, new_dentry, 0);
	if (ret)
		goto out_new;

	/* Sync all dirty buffers */
	{
		int sync_ret = bh_cache_sync();

		if (sync_ret)
			ret = sync_ret;
		/* Commit superblock with updated free counts */
		ext4_commit_super(efs.sb);
	}

out_new:
	kfree(new_dentry);
	kfree(new_dir_dentry);
	free(new_path_copy);
out_old:
	kfree(old_dentry);
	kfree(old_dir_dentry);
	free(old_path_copy);
	return ret;
}

void ext4l_close(void)
{
	ext4l_close_internal(false);
}

/**
 * struct ext4l_dir - ext4l directory stream state
 * @parent: base fs_dir_stream structure
 * @dirent: directory entry to return to caller
 * @dir_inode: pointer to directory inode
 * @file: file structure for ext4_readdir
 * @entry_found: flag set by actor when entry is captured
 * @last_ino: inode number of last returned entry (to skip on next call)
 * @skip_last: true if we need to skip the last_ino entry
 *
 * The filesystem stays mounted while directory streams are open (ext4l_close
 * checks efs.open_dirs), so we can keep direct pointers to inodes.
 */
struct ext4l_dir {
	struct fs_dir_stream parent;
	struct fs_dirent dirent;
	struct inode *dir_inode;
	struct file file;
	bool entry_found;
	u64 last_ino;
	bool skip_last;
};

/**
 * struct ext4l_readdir_ctx - Extended dir_context with back-pointer
 * @ctx: base dir_context structure (must be first)
 * @dir: pointer to ext4l_dir for state updates
 */
struct ext4l_readdir_ctx {
	struct dir_context ctx;
	struct ext4l_dir *dir;
};

/**
 * ext4l_opendir_actor() - dir_context actor that captures single entry
 *
 * This actor is called by ext4_readdir for each directory entry. It captures
 * the first entry found (skipping the previously returned entry if needed)
 * and returns non-zero to stop iteration.
 */
static int ext4l_opendir_actor(struct dir_context *ctx, const char *name,
			       int namelen, loff_t offset, u64 ino,
			       unsigned int d_type)
{
	struct ext4l_readdir_ctx *rctx;
	struct ext4l_dir *dir;
	struct fs_dirent *dent;
	struct inode *inode;

	rctx = container_of(ctx, struct ext4l_readdir_ctx, ctx);
	dir = rctx->dir;

	/*
	 * Skip the entry we returned last time. The htree code may call us
	 * with the same entry again due to its extra_fname handling.
	 */
	if (dir->skip_last && ino == dir->last_ino) {
		dir->skip_last = false;
		return 0;  /* Continue to next entry */
	}

	dent = &dir->dirent;

	/* Copy name */
	if (namelen >= FS_DIRENT_NAME_LEN)
		namelen = FS_DIRENT_NAME_LEN - 1;
	memcpy(dent->name, name, namelen);
	dent->name[namelen] = '\0';

	/* Set type based on d_type hint */
	switch (d_type) {
	case DT_DIR:
		dent->type = FS_DT_DIR;
		break;
	case DT_LNK:
		dent->type = FS_DT_LNK;
		break;
	default:
		dent->type = FS_DT_REG;
		break;
	}

	/* Look up inode to get size and other attributes */
	inode = ext4_iget(efs.sb, ino, 0);
	if (!IS_ERR(inode)) {
		dent->size = inode->i_size;
		/* Refine type from inode mode if needed */
		if (S_ISDIR(inode->i_mode))
			dent->type = FS_DT_DIR;
		else if (S_ISLNK(inode->i_mode))
			dent->type = FS_DT_LNK;
		else
			dent->type = FS_DT_REG;
	} else {
		dent->size = 0;
	}

	dir->entry_found = true;
	dir->last_ino = ino;

	/*
	 * Return non-zero to stop iteration after one entry.
	 * dir_emit() returns (actor(...) == 0), so:
	 *   actor returns 0 -> dir_emit returns 1 (continue)
	 *   actor returns non-zero -> dir_emit returns 0 (stop)
	 */
	return 1;
}

int ext4l_opendir(const char *filename, struct fs_dir_stream **dirsp)
{
	struct ext4l_dir *dir;
	struct inode *inode;
	int ret;

	if (!efs.mounted)
		return -ENODEV;

	ret = ext4l_resolve_path(filename, &inode);
	if (ret)
		return ret;

	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	dir = calloc(1, sizeof(*dir));
	if (!dir)
		return -ENOMEM;

	dir->dir_inode = inode;
	dir->entry_found = false;

	/* Set up file structure for ext4_readdir */
	dir->file.f_inode = inode;
	dir->file.f_mapping = inode->i_mapping;
	dir->file.private_data = kzalloc(sizeof(struct dir_private_info),
					 GFP_KERNEL);
	if (!dir->file.private_data) {
		free(dir);
		return -ENOMEM;
	}

	/* Increment open dir count to prevent unmount */
	efs.open_dirs++;

	*dirsp = (struct fs_dir_stream *)dir;

	return 0;
}

int ext4l_readdir(struct fs_dir_stream *dirs, struct fs_dirent **dentp)
{
	struct ext4l_dir *dir = (struct ext4l_dir *)dirs;
	struct ext4l_readdir_ctx ctx;
	int ret;

	if (!efs.mounted)
		return -ENODEV;

	memset(&dir->dirent, '\0', sizeof(dir->dirent));
	dir->entry_found = false;

	/* Skip the entry we returned last time (htree may re-emit it) */
	if (dir->last_ino)
		dir->skip_last = true;

	/* Set up extended dir_context for this iteration */
	memset(&ctx, '\0', sizeof(ctx));
	ctx.ctx.actor = ext4l_opendir_actor;
	ctx.ctx.pos = dir->file.f_pos;
	ctx.dir = dir;

	ret = ext4_readdir(&dir->file, &ctx.ctx);

	/* Update file position for next call */
	dir->file.f_pos = ctx.ctx.pos;

	if (ret < 0)
		return ret;

	if (!dir->entry_found)
		return -ENOENT;

	*dentp = &dir->dirent;

	return 0;
}

void ext4l_closedir(struct fs_dir_stream *dirs)
{
	struct ext4l_dir *dir = (struct ext4l_dir *)dirs;

	if (dir) {
		if (dir->file.private_data)
			ext4_htree_free_dir_info(dir->file.private_data);
		free(dir);
	}

	/* Decrement open dir count */
	if (efs.open_dirs > 0)
		efs.open_dirs--;
}
