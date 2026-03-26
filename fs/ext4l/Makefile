# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ext4l filesystem (Linux port)
#

obj-y := interface.o support.o stub.o
obj-$(CONFIG_$(PHASE_)VFS) += fs.o

# Core ext4 objects (always needed)
obj-y	+= balloc.o bitmap.o block_validity.o dir.o ext4_jbd2.o extents.o \
		extents_status.o file.o fsmap.o fsync.o hash.o ialloc.o \
		inode.o mmp.o namei.o \
		page-io.o readpage.o super.o symlink.o fast_commit.o orphan.o

# Optional features
obj-$(CONFIG_EXT4_MBALLOC)	+= mballoc.o
obj-$(CONFIG_EXT4_RESIZE)	+= resize.o
obj-$(CONFIG_EXT4_MIGRATE)	+= migrate.o
obj-$(CONFIG_EXT4_MOVE_EXTENT)	+= move_extent.o
obj-$(CONFIG_EXT4_XATTR)	+= xattr.o xattr_hurd.o xattr_trusted.o \
				   xattr_user.o
obj-$(CONFIG_EXT4_INLINE_DATA)	+= inline.o
obj-$(CONFIG_EXT4_INDIRECT)	+= indirect.o
obj-$(CONFIG_EXT4_FS_POSIX_ACL)	+= acl.o
obj-$(CONFIG_EXT4_FS_SECURITY)	+= xattr_security.o
