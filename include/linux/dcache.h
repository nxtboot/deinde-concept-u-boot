/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Dentry cache stubs for U-Boot
 *
 * Based on Linux dcache.h - U-Boot doesn't have a real dentry cache,
 * so these are stubs for compilation.
 */
#ifndef _LINUX_DCACHE_H
#define _LINUX_DCACHE_H

#include <linux/types.h>
#include <linux/list.h>

/* Forward declarations */
struct inode;
struct super_block;

/**
 * struct qstr - quick string for filenames
 * @hash: filename hash
 * @len: filename length
 * @name: filename
 *
 * Note: Also defined in ext4_fscrypt.h for fscrypt support.
 */
struct qstr {
	u32 hash;
	u32 len;
	const unsigned char *name;
};

/**
 * QSTR_INIT - initialise a qstr
 * @n: name string
 * @l: length
 */
#define QSTR_INIT(n, l) { .name = (const unsigned char *)(n), .len = (l) }

/* dotdot_name for ".." lookups */
static const struct qstr dotdot_name = QSTR_INIT("..", 2);

/**
 * struct dentry - directory entry
 * @d_name: filename
 * @d_inode: associated inode
 * @d_sb: superblock
 * @d_parent: parent directory
 *
 * U-Boot stub - minimal fields for ext4l.
 */
struct dentry_operations;

struct dentry {
	struct qstr d_name;
	struct inode *d_inode;
	struct super_block *d_sb;
	struct dentry *d_parent;
	const struct dentry_operations *d_op;
};

/**
 * struct name_snapshot - dentry name snapshot
 * @name: snapshot of the name
 *
 * Used for safe name access during operations.
 */
struct name_snapshot {
	struct qstr name;
};

/* d_inode - get inode from dentry */
#define d_inode(dentry)		((dentry) ? (dentry)->d_inode : NULL)

/* Dentry operations - stubs */
#define d_find_any_alias(i)	({ (void)(i); (struct dentry *)NULL; })
#define dget_parent(d)		({ (void)(d); (struct dentry *)NULL; })
#define dput(d)			do { (void)(d); } while (0)
#define d_splice_alias(i, d)	({ (d)->d_inode = (i); (d); })
#define d_obtain_alias(i)	({ (void)(i); (struct dentry *)NULL; })
#define d_instantiate_new(d, i)	((void)((d)->d_inode = (i)))
#define d_instantiate(d, i)	((void)((d)->d_inode = (i)))
#define d_tmpfile(f, i)		do { (void)(f); (void)(i); } while (0)
#define d_invalidate(d)		do { (void)(d); } while (0)
#define d_alloc(parent, name)	({ (void)(parent); (void)(name); (struct dentry *)NULL; })
#define d_drop(dentry)		do { (void)(dentry); } while (0)

/* Name snapshot operations */
#define take_dentry_name_snapshot(sn, d) \
	do { (sn)->name = (d)->d_name; } while (0)
#define release_dentry_name_snapshot(sn) \
	do { (void)(sn); } while (0)

/* Dentry operations - declarations for stub.c */
void generic_set_sb_d_ops(struct super_block *sb);
struct dentry *d_make_root(struct inode *inode);

#endif /* _LINUX_DCACHE_H */
