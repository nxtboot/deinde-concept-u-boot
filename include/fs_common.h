/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */
#ifndef _FS_COMMON_H
#define _FS_COMMON_H

#include <rtc_def.h>

/**
 * @FS_TYPE_VIRTIO: virtio-fs for access to the host filesystem from QEMU
 */
enum fs_type_t {
	FS_TYPE_ANY = 0,
	FS_TYPE_FAT,
	FS_TYPE_EXT,
	FS_TYPE_SANDBOX,
	FS_TYPE_UBIFS,
	FS_TYPE_BTRFS,
	FS_TYPE_SQUASHFS,
	FS_TYPE_EROFS,
	FS_TYPE_SEMIHOSTING,
	FS_TYPE_EXFAT,
	FS_TYPE_VIRTIO,
	FS_TYPE_ISO,
	FS_TYPE_CBFS,
};

/*
 * Directory entry types, matches the subset of DT_x in posix readdir()
 * which apply to u-boot.
 */
#define FS_DT_DIR  4         /* directory */
#define FS_DT_REG  8         /* regular file */
#define FS_DT_LNK  10        /* symbolic link */

#define FS_DIRENT_NAME_LEN	CONFIG_IS_ENABLED(FS_EXFAT, (1024), (256))

/**
 * struct fs_dirent - directory entry
 *
 * A directory entry, returned by fs_readdir(). Returns information
 * about the file/directory at the current directory entry position.
 */
struct fs_dirent {
	/** @type:		one of FS_DT_x (not a mask) */
	unsigned int type;
	/** @size:		file size */
	loff_t size;
	/** @attr:		attribute flags (FS_ATTR_*) */
	u32 attr;
	/** @create_time:	time of creation */
	struct rtc_time create_time;
	/** @access_time:	time of last access */
	struct rtc_time access_time;
	/** @change_time:	time of last modification */
	struct rtc_time change_time;
	/** @name:		file name */
	char name[FS_DIRENT_NAME_LEN];
};

/**
 * struct fs_dir_stream - Structure representing an opened directory
 *
 * Struct fs_dir_stream should be treated opaque to the user of fs layer.
 * The fields @desc and @part are used by the fs layer.
 * File system drivers pass additional private fields with the pointers
 * to this structure.
 *
 * @dev:	dir device (UCLASS_DIR)
 * @desc:	block device descriptor
 * @part:	partition number
 */
struct fs_dir_stream {
#ifdef CONFIG_FS
	struct udevice *dev;
	u64 fh;
	u64 offset;
#endif
	struct blk_desc *desc;
	int part;
};

#endif
