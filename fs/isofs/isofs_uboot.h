/* SPDX-License-Identifier: GPL-2.0 */
/*
 * U-Boot compatibility header for isofs filesystem
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * This provides minimal definitions to allow Linux isofs code to compile
 * in U-Boot. The isofs driver is read-only and much simpler than ext4,
 * so the compatibility layer is lighter.
 */

#ifndef __ISOFS_UBOOT_H__
#define __ISOFS_UBOOT_H__

/*
 * Suppress warnings for unused static functions and variables in Linux isofs
 * source files.
 */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

/* U-Boot headers */
#include <memalign.h>
#include <vsprintf.h>

/* Linux types - must come first */
#include <linux/types.h>

/* Linux headers */
#include <asm/byteorder.h>
#include <asm-generic/unaligned.h>
#include <asm-generic/atomic.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/nls.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/cred.h>
#include <linux/ctype.h>
#include <linux/dcache.h>
#include <linux/exportfs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>

/*
 * Suppress printk messages from Linux isofs code. In Linux these are
 * filtered by log level; in U-Boot printk maps to printf for all levels.
 * The isofs code uses printk only for debug and error messages that are
 * not useful in U-Boot's context.
 */
#undef printk
#define printk(fmt, ...)	({ if (0) printf(fmt, ##__VA_ARGS__); 0; })

/* Page allocation wrappers */
#ifndef __get_free_page
#define __get_free_page(gfp) \
	((unsigned long)memalign(PAGE_SIZE, PAGE_SIZE))
#endif
void free_page(unsigned long addr);

#define alloc_page(gfp) \
	((struct page *)memalign(PAGE_SIZE, PAGE_SIZE))
#define __free_page(page)	free(page)

/* Joliet support */
#ifndef CFG_NLS_DEFAULT
#define CFG_NLS_DEFAULT		"utf8"
#endif

/* CDROM support stubs - not available in U-Boot */
struct cdrom_device_info;

static inline struct cdrom_device_info *disk_to_cdi(void *disk)
{
	return NULL;
}

#define CDROM_LBA		0
#define CDROM_DATA_TRACK	4

struct cdrom_tocentry {
	int cdte_track;
	int cdte_format;
	struct { int lba; } cdte_addr;
	int cdte_ctrl;
};

struct cdrom_multisession {
	int addr_format;
	int xa_flag;
	struct { int lba; } addr;
};

static inline int cdrom_read_tocentry(struct cdrom_device_info *cdi,
				      struct cdrom_tocentry *te)
{
	return -ENODEV;
}

static inline int cdrom_multisession(struct cdrom_device_info *cdi,
				     struct cdrom_multisession *ms)
{
	return -ENODEV;
}

/* set_default_d_op - no dentry ops needed in U-Boot */
static inline void set_default_d_op(struct super_block *sb,
				    const struct dentry_operations *ops) { }

/* inode_nohighmem stub */
static inline void inode_nohighmem(struct inode *inode) { }

/* generic_ro_fops, page_symlink_inode_operations - defined in linux/fs.h */

/* generic_setlease - lease management stub */
struct file_lease;
static inline int generic_setlease(struct file *fp, int arg,
				   struct file_lease **flp, void **priv)
{
	return -EINVAL;
}

/* init_special_inode - override the macro from fs.h */
#undef init_special_inode
static inline void init_special_inode(struct inode *inode, umode_t mode,
				      dev_t rdev)
{
	inode->i_mode = mode;
	inode->i_rdev = rdev;
}

/* User namespace helpers for mount options */
static inline uid_t from_kuid_munged(void *ns, kuid_t uid)
{
	return uid.val;
}

static inline gid_t from_kgid_munged(void *ns, kgid_t gid)
{
	return gid.val;
}

/* gendisk is forward-declared in linux/blkdev.h */

/*
 * mpage stubs - isofs uses mpage_read_folio/mpage_readahead for
 * regular file I/O. We stub these since file reading goes through
 * the interface layer directly.
 */
static inline int mpage_read_folio(struct folio *folio,
				   get_block_t *get_block)
{
	return -EIO;
}

static inline void mpage_readahead(struct readahead_control *rac,
				   get_block_t *get_block) { }

/* generic_block_bmap stub */
static inline sector_t generic_block_bmap(struct address_space *mapping,
					  sector_t block,
					  get_block_t *get_block)
{
	return 0;
}

/* huge_encode_dev - encode device for statfs */
static inline u64 huge_encode_dev(dev_t dev)
{
	return dev;
}

/* u64_to_fsid - convert u64 to kernel_fsid_t */
static inline __kernel_fsid_t u64_to_fsid(u64 v)
{
	__kernel_fsid_t fsid;

	fsid.val[0] = v;
	fsid.val[1] = v >> 32;
	return fsid;
}

/* NAME_MAX */
#ifndef NAME_MAX
#define NAME_MAX	255
#endif

/* S_IXUGO */
#ifndef S_IXUGO
#define S_IXUGO		(S_IXUSR | S_IXGRP | S_IXOTH)
#endif

/* FILEID_INVALID for export.c */
#ifndef FILEID_INVALID
#define FILEID_INVALID	0xff
#endif

/* page_address - for page used as buffer */
static inline void *page_address(void *page)
{
	return page;
}

/* folio_address is defined in linux/pagemap.h */
/* folio_end_read is defined as a macro in linux/pagemap.h */

/* Dentry operations - needed by isofs for case-insensitive lookup */
struct dentry_operations {
	int (*d_hash)(const struct dentry *dentry, struct qstr *qstr);
	int (*d_compare)(const struct dentry *dentry, unsigned int len,
			 const char *str, const struct qstr *name);
};

/* Name hash functions for dentry operations */
static inline unsigned long init_name_hash(const struct dentry *dentry)
{
	return 0;
}

static inline unsigned long partial_name_hash(unsigned long c,
					      unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

static inline unsigned long end_name_hash(unsigned long hash)
{
	return (unsigned int)hash;
}

static inline unsigned int full_name_hash(const struct dentry *dentry,
					  const char *name, unsigned int len)
{
	unsigned long hash = init_name_hash(dentry);

	while (len--)
		hash = partial_name_hash((unsigned char)*name++, hash);
	return end_name_hash(hash);
}

/* generic_file_llseek - stub for directory operations */
static inline loff_t generic_file_llseek(struct file *file, loff_t offset,
					 int whence)
{
	return offset;
}

/* isofs_close - forward declaration for interface.c */
void isofs_close(void);

/*
 * iget5_locked - isofs uses custom test/set callbacks for inode lookup.
 * Implemented in support.c.
 */
struct inode *iget5_locked(struct super_block *sb, unsigned long hashval,
			   int (*test)(struct inode *, void *),
			   int (*set)(struct inode *, void *), void *data);

/* iget_failed - override the macro from fs.h */
#undef iget_failed
void iget_failed(struct inode *inode);

/*
 * d_obtain_alias - override the stub macro from dcache.h.
 * Needed by export.c for NFS file handle lookup.
 */
#undef d_obtain_alias
static inline struct dentry *d_obtain_alias(struct inode *inode)
{
	struct dentry *d;

	if (IS_ERR(inode))
		return ERR_CAST(inode);
	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	d->d_inode = inode;
	return d;
}

/* dir_emit, dir_emit_dot, dir_emit_dotdot - defined in linux/fs.h */

/* sb_bread, brelse, etc. are now in fs/linux_fs.c */
struct buffer_head *sb_bread(struct super_block *sb, sector_t block);
void bh_cache_clear(struct block_device *bdev);
void isofs_free_inodes(struct super_block *sb);

/* isofs inode.c exports */
int isofs_fill_super(struct super_block *s, struct fs_context *fc);
int isofs_init_inodecache(void);
void isofs_destroy_inodecache(void);

#endif /* __ISOFS_UBOOT_H__ */
