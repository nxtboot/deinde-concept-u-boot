# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2000-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.
# Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

obj-$(CONFIG_$(PHASE_)FS_LEGACY) += fs_legacy.o fs_internal.o
obj-$(CONFIG_FS_LINUX) += linux_fs.o
obj-$(CONFIG_$(PHASE_)FS) += fs-uclass.o
obj-$(CONFIG_$(PHASE_)DIR) += dir-uclass.o
obj-$(CONFIG_$(PHASE_)FILE) += file-uclass.o
obj-$(CONFIG_$(PHASE_)VFS) += fs_mount.o mount-uclass.o vfs.o vfs_dir.o

ifdef CONFIG_XPL_BUILD
obj-$(CONFIG_SPL_FS_FAT) += fat/
obj-$(CONFIG_SPL_FS_EXT4) += ext4/
obj-$(CONFIG_SPL_FS_CBFS) += cbfs/
obj-$(CONFIG_SPL_FS_SQUASHFS) += squashfs/
else

obj-$(CONFIG_FS_BTRFS) += btrfs/
obj-$(CONFIG_FS_CBFS) += cbfs/
obj-$(CONFIG_CMD_CRAMFS) += cramfs/
obj-$(CONFIG_FS_EXFAT) += exfat/
obj-$(CONFIG_FS_EXT4) += ext4/
obj-$(CONFIG_FS_EXT4L) += ext4l/
obj-$(CONFIG_EXT4_JOURNAL) += jbd2/
obj-$(CONFIG_FS_FAT) += fat/
obj-$(CONFIG_FS_ISOFS) += isofs/
obj-$(CONFIG_FS_JFFS2) += jffs2/
obj-$(CONFIG_SANDBOX) += sandbox/
obj-$(CONFIG_SEMIHOSTING) += semihostingfs.o
obj-$(CONFIG_CMD_UBIFS) += ubifs/
obj-$(CONFIG_CMD_ZFS) += zfs/
obj-$(CONFIG_FS_SQUASHFS) += squashfs/
obj-$(CONFIG_FS_EROFS) += erofs/
endif
