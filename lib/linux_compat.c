
#include <malloc.h>
#include <memalign.h>
#include <string.h>
#include <asm/cache.h>
#include <linux/compat.h>

struct task_struct cur = {
	.pid = 1,
};
__maybe_unused struct task_struct *current = &cur;

void *kmalloc(size_t size, gfp_t flags)
{
	void *p;

	p = malloc_cache_aligned(size);
	if (p && flags & __GFP_ZERO)
		memset(p, 0, size);

	return p;
}

struct kmem_cache *get_mem(int element_sz)
{
	struct kmem_cache *ret;

	ret = memalign(ARCH_DMA_MINALIGN, sizeof(struct kmem_cache));
	if (!ret)
		return NULL;

	ret->sz = element_sz;

	return ret;
}

void *kmem_cache_alloc(struct kmem_cache *obj, gfp_t flag)
{
	return malloc_cache_aligned(obj->sz);
}

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 *
 * Return: newly allocated copy of @src or %NULL in case of error
 */
void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}
