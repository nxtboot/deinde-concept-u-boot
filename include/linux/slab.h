/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Mark Hemment, 1996 (markhe@nextd.demon.co.uk).
 *
 * (C) SGI 2006, Christoph Lameter
 *	Cleaned up and restructured to ease the addition of alternative
 *	implementations of SLAB allocators.
 * (C) Linux Foundation 2008-2013
 *      Unified interface for all slab allocators
 *
 * Memory allocation functions for Linux kernel compatibility.
 * These map to U-Boot's malloc/free infrastructure.
 */
#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <malloc.h>
#include <linux/types.h>
#include <linux/string.h>

#ifndef GFP_ATOMIC
#define GFP_ATOMIC	((gfp_t)0)
#endif
#ifndef GFP_KERNEL
#define GFP_KERNEL	((gfp_t)0)
#endif
#ifndef GFP_NOFS
#define GFP_NOFS	((gfp_t)0)
#endif
#ifndef GFP_USER
#define GFP_USER	((gfp_t)0)
#endif
#ifndef GFP_NOWAIT
#define GFP_NOWAIT	((gfp_t)0)
#endif
#ifndef __GFP_NOWARN
#define __GFP_NOWARN	((gfp_t)0)
#endif
#ifndef __GFP_ZERO
#define __GFP_ZERO	((__force gfp_t)0x8000u)
#endif
#ifndef __GFP_NOFAIL
#define __GFP_NOFAIL	((gfp_t)0)
#endif
#ifndef __GFP_MOVABLE
#define __GFP_MOVABLE	((gfp_t)0)
#endif
#ifndef __GFP_FS
#define __GFP_FS	((gfp_t)0)
#endif
#ifndef GFP_NOIO
#define GFP_NOIO	((gfp_t)0)
#endif

/* Slab cache creation flags */
#define SLAB_RECLAIM_ACCOUNT	0x00020000UL	/* Track pages reclaimed */
#define SLAB_ACCOUNT		0x00000000UL	/* Account to memcg (no-op) */

/*
 * Check if pointer is zero or in the zero page (used by SLUB allocator).
 * PAGE_SIZE fallback for when this header is included standalone.
 */
#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif
#define ZERO_OR_NULL_PTR(x)	((unsigned long)(x) <= PAGE_SIZE)

void *kmalloc(size_t size, gfp_t flags);

static inline void *kzalloc(size_t size, gfp_t flags)
{
	return kmalloc(size, flags | __GFP_ZERO);
}

#define kzalloc_obj(obj, ...)	kzalloc(sizeof(obj), GFP_KERNEL)

static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return kmalloc(n * size, flags | __GFP_ZERO);
}

static inline void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags | __GFP_ZERO);
}

static inline void kfree(const void *block)
{
	free((void *)block);
}

static inline void kvfree(const void *addr)
{
	kfree(addr);
}

static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	return kmalloc_array(n, size, flags);
}

static inline void *krealloc(const void *p, size_t new_size, gfp_t flags)
{
	return realloc((void *)p, new_size);
}

void *kmemdup(const void *src, size_t len, gfp_t gfp);

/**
 * kmemdup_nul - Duplicate a string with null termination
 * @s: Source string
 * @len: Maximum length to copy
 * @gfp: GFP flags for allocation
 *
 * Allocates len + 1 bytes, copies up to @len bytes from @s, and
 * ensures the result is null-terminated.
 *
 * Return: pointer to new string, or NULL on allocation failure
 */
char *kmemdup_nul(const char *s, size_t len, gfp_t gfp);

/* kmem_cache stubs */
struct kmem_cache {
	int sz;
};

struct kmem_cache *get_mem(int element_sz);
#define kmem_cache_create(a, sz, c, d, e)	({ (void)(a); (void)(e); get_mem(sz); })
#define kmem_cache_create_usercopy(n, sz, al, fl, uo, us, c) \
	kmem_cache_create(n, sz, al, fl, c)

/**
 * KMEM_CACHE - shorthand for creating a named kmem_cache
 * @s: struct type name
 * @flags: cache creation flags
 *
 * Creates a cache for objects of type struct @s with the specified flags.
 */
#define KMEM_CACHE(s, flags)	kmem_cache_create(#s, sizeof(struct s), 0, flags, NULL)

void *kmem_cache_alloc(struct kmem_cache *obj, gfp_t flag);

static inline void *kmem_cache_zalloc(struct kmem_cache *obj, gfp_t flags)
{
	void *ret = kmem_cache_alloc(obj, flags);

	if (ret)
		memset(ret, 0, obj->sz);
	return ret;
}

static inline void kmem_cache_free(struct kmem_cache *cachep, void *obj)
{
	free(obj);
}

static inline void kmem_cache_destroy(struct kmem_cache *cachep)
{
	free(cachep);
}

#endif /* _LINUX_SLAB_H */
