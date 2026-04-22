// SPDX-License-Identifier: GPL-2.0+
/*
 *  EFI application memory management
 *
 *  Copyright (c) 2016 Alexander Graf
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_loader.h>
#include <efi_log.h>
#include <efi_stub.h>
#include <init.h>
#include <lmb.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <watchdog.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/sections.h>
#include <linux/list_sort.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

/* Magic number identifying memory allocated from pool */
#define EFI_ALLOC_POOL_MAGIC 0x1fe67ddf6491caa2

efi_uintn_t efi_memory_map_key;

/**
 * struct mem_node - defines an EFI memory record
 *
 * Note that this struct is for use inside U-Boot and is not visible to the
 * EFI application, other than through calls to efi_get_memory_map(), where this
 * internal format is converted to the external struct efi_mem_desc format.
 *
 * @link: Link to prev/next node in list
 * @type: EFI memory-type
 * @base: Start address of region in physical memory
 * @num_pages: Number of EFI pages this record covers (each is EFI_PAGE_SIZE
 *	bytes)
 * @attribute: Memory attributes (see EFI_MEMORY...)
 */
struct mem_node {
	struct list_head link;
	enum efi_memory_type type;
	efi_physical_addr_t base;
	u64 num_pages;
	u64 attribute;
};

#define EFI_CARVE_NO_OVERLAP		-1
#define EFI_CARVE_LOOP_AGAIN		-2
#define EFI_CARVE_OVERLAPS_NONRAM	-3
#define EFI_CARVE_OUT_OF_RESOURCES	-4

/* This list contains all memory map items */
static LIST_HEAD(efi_mem);

#ifdef CONFIG_EFI_LOADER_BOUNCE_BUFFER
void *efi_bounce_buffer;
#endif

/**
 * struct efi_pool_allocation - memory block allocated from pool
 *
 * @num_pages:	number of pages allocated
 * @checksum:	checksum
 * @data:	allocated pool memory
 *
 * U-Boot services each UEFI AllocatePool() request as a separate
 * (multiple) page allocation. We have to track the number of pages
 * to be able to free the correct amount later.
 *
 * The checksum calculated in function checksum() is used in FreePool() to avoid
 * freeing memory not allocated by AllocatePool() and duplicate freeing.
 *
 * EFI requires 8 byte alignment for pool allocations, so we can
 * prepend each allocation with these header fields.
 */
struct efi_pool_allocation {
	u64 num_pages;
	u64 checksum;
	char data[] __aligned(ARCH_DMA_MINALIGN);
};

/**
 * checksum() - calculate checksum for memory allocated from pool
 *
 * @alloc:	allocation header
 * Return:	checksum, always non-zero
 */
static u64 checksum(struct efi_pool_allocation *alloc)
{
	u64 addr = (uintptr_t)alloc;
	u64 ret = (addr >> 32) ^ (addr << 32) ^ alloc->num_pages ^
		  EFI_ALLOC_POOL_MAGIC;
	if (!ret)
		++ret;
	return ret;
}

/**
 * efi_mem_cmp() - comparator function for sorting memory map
 *
 * Sorts the memory list from highest address to lowest address
 *
 * When allocating memory we should always start from the highest
 * address chunk, so sort the memory list such that the first list
 * iterator gets the highest address and goes lower from there.
 *
 * @priv:	unused
 * @a:		first memory area
 * @b:		second memory area
 * Return:	1 if @a is before @b, -1 if @b is before @a, 0 if equal
 */
static int efi_mem_cmp(void *priv, const struct list_head *a,
		       const struct list_head *b)
{
	const struct mem_node *mema = list_entry(a, struct mem_node, link);
	const struct mem_node *memb = list_entry(b, struct mem_node, link);

	if (mema->base == memb->base)
		return 0;
	else if (mema->base < memb->base)
		return 1;
	else
		return -1;
}

/**
 * desc_get_end() - get end address of memory area
 *
 * @node:	memory node
 * Return:	end address + 1
 */
static uint64_t desc_get_end(struct mem_node *node)
{
	return node->base + (node->num_pages << EFI_PAGE_SHIFT);
}

/**
 * efi_mem_sort() - sort memory map
 *
 * Sort the memory map and then try to merge adjacent memory areas.
 */
static void efi_mem_sort(void)
{
	struct mem_node *curmem, *nextmem = NULL;

	list_sort(NULL, &efi_mem, efi_mem_cmp);

	/* Now merge entries that can be merged */
	list_for_each_entry_safe(curmem, nextmem, &efi_mem, link) {
		/* Exit when we've got nothing to compare with */
		if (&nextmem->link == &efi_mem)
			break;

		if ((curmem->base == desc_get_end(nextmem)) &&
		    (curmem->type == nextmem->type) &&
		    (curmem->attribute == nextmem->attribute)) {
			/* There is another similar map coming up, reuse it */
			nextmem->num_pages += curmem->num_pages;
			list_del(&curmem->link);
			free(curmem);
		}
	}
}

/**
 * efi_mem_carve_out() - unmap memory region
 *
 * @map:			memory map
 * @carve_desc:			memory region to unmap
 * @overlap_conventional:	the carved out region may only overlap free,
 *				or conventional memory
 * Return:			the number of overlapping pages which have been
 *				removed from the map,
 *				EFI_CARVE_NO_OVERLAP, if the regions don't
 *				overlap, EFI_CARVE_OVERLAPS_NONRAM, if the carve
 *				and map overlap, and the map contains anything
 *				but free ram(only when overlap_conventional is
 *				true),
 *				EFI_CARVE_LOOP_AGAIN, if the mapping list should
 *				be traversed again, as it has been altered.
 *
 * Unmaps all memory occupied by the carve_desc region from the list entry
 * pointed to by map.
 *
 * In case of EFI_CARVE_OVERLAPS_NONRAM it is the callers responsibility
 * to re-add the already carved out pages to the mapping.
 */
static s64 efi_mem_carve_out(struct mem_node *map, struct mem_node *carve_desc,
			     bool overlap_conventional)
{
	struct mem_node *newmap;
	struct mem_node *map_desc = map;
	u64 map_start = map_desc->base;
	u64 map_end = map_start + (map_desc->num_pages << EFI_PAGE_SHIFT);
	u64 carve_start = carve_desc->base;
	u64 carve_end = carve_start +
			     (carve_desc->num_pages << EFI_PAGE_SHIFT);

	/* check whether we're overlapping */
	if ((carve_end <= map_start) || (carve_start >= map_end))
		return EFI_CARVE_NO_OVERLAP;

	/* We're overlapping with non-RAM, warn the caller if desired */
	if (overlap_conventional && (map_desc->type != EFI_CONVENTIONAL_MEMORY))
		return EFI_CARVE_OVERLAPS_NONRAM;

	/* Sanitize carve_start and carve_end to lie within our bounds */
	carve_start = max(carve_start, map_start);
	carve_end = min(carve_end, map_end);

	/* Carving at the beginning of our map? Just move it! */
	if (carve_start == map_start) {
		if (map_end == carve_end) {
			/* Full overlap, just remove map */
			list_del(&map->link);
			free(map);
		} else {
			map->base = carve_end;
			map->num_pages = (map_end - carve_end)
					      >> EFI_PAGE_SHIFT;
		}

		return (carve_end - carve_start) >> EFI_PAGE_SHIFT;
	}

	/*
	 * Overlapping maps, just split the list map at carve_start,
	 * it will get moved or removed in the next iteration.
	 *
	 * [ map_desc |__carve_start__| newmap ]
	 */

	/* Create a new map from [ carve_start ... map_end ] */
	newmap = calloc(1, sizeof(*newmap));
	if (!newmap)
		return EFI_CARVE_OUT_OF_RESOURCES;
	newmap->type = map->type;
	newmap->base = carve_start;
	newmap->num_pages = (map_end - carve_start) >> EFI_PAGE_SHIFT;
	newmap->attribute = map->attribute;
	/* Insert before current entry (descending address order) */
	list_add_tail(&newmap->link, &map->link);

	/* Shrink the map to [ map_start ... carve_start ] */
	map_desc->num_pages = (carve_start - map_start) >> EFI_PAGE_SHIFT;

	return EFI_CARVE_LOOP_AGAIN;
}

efi_status_t efi_update_memory_map(u64 start, u64 pages, int memory_type,
				   bool overlap_conventional, bool remove)
{
	struct mem_node *lmem;
	struct mem_node *newlist;
	bool carve_again;
	uint64_t carved_pages = 0;
	struct efi_event *evt;

	EFI_PRINT("%s: 0x%llx 0x%llx %d %s %s\n", __func__,
		  start, pages, memory_type, overlap_conventional ?
		  "yes" : "no", remove ? "remove" : "add");

	if (memory_type >= EFI_MAX_MEMORY_TYPE)
		return EFI_INVALID_PARAMETER;

	if (!pages)
		return EFI_SUCCESS;

	++efi_memory_map_key;
	newlist = calloc(1, sizeof(*newlist));
	if (!newlist)
		return EFI_OUT_OF_RESOURCES;
	newlist->type = memory_type;
	newlist->base = start;
	newlist->num_pages = pages;

	switch (memory_type) {
	case EFI_RUNTIME_SERVICES_CODE:
	case EFI_RUNTIME_SERVICES_DATA:
		newlist->attribute = EFI_MEMORY_WB | EFI_MEMORY_RUNTIME;
		break;
	case EFI_MMAP_IO:
		newlist->attribute = EFI_MEMORY_RUNTIME;
		break;
	default:
		newlist->attribute = EFI_MEMORY_WB;
		break;
	}

	/* Add our new map */
	do {
		carve_again = false;
		list_for_each_entry(lmem, &efi_mem, link) {
			s64 r;

			r = efi_mem_carve_out(lmem, newlist,
					      overlap_conventional);
			switch (r) {
			case EFI_CARVE_OUT_OF_RESOURCES:
				free(newlist);
				return EFI_OUT_OF_RESOURCES;
			case EFI_CARVE_OVERLAPS_NONRAM:
				/*
				 * The user requested to only have RAM overlaps,
				 * but we hit a non-RAM region. Error out.
				 */
				free(newlist);
				return EFI_NO_MAPPING;
			case EFI_CARVE_NO_OVERLAP:
				/* Just ignore this list entry */
				break;
			case EFI_CARVE_LOOP_AGAIN:
				/*
				 * We split an entry, but need to loop through
				 * the list again to actually carve it.
				 */
				carve_again = true;
				break;
			default:
				/* We carved a number of pages */
				carved_pages += r;
				carve_again = true;
				break;
			}

			if (carve_again) {
				/* The list changed, we need to start over */
				break;
			}
		}
	} while (carve_again);

	if (overlap_conventional && (carved_pages != pages)) {
		/*
		 * The payload wanted to have RAM overlaps, but we overlapped
		 * with an unallocated region. Error out.
		 */
		free(newlist);
		return EFI_NO_MAPPING;
	}

	/* Add our new map */
	if (!remove)
		list_add_tail(&newlist->link, &efi_mem);
	else
		free(newlist);

	/* And make sure memory is listed in descending order */
	efi_mem_sort();

	/* Notify that the memory map was changed */
	list_for_each_entry(evt, &efi_events, link) {
		if (evt->group &&
		    !guidcmp(evt->group,
			     &efi_guid_event_group_memory_map_change)) {
			efi_signal_event(evt);
			break;
		}
	}

	return EFI_SUCCESS;
}

efi_status_t efi_add_memory_map(u64 start, u64 size, int memory_type)
{
	u64 pages;

	pages = efi_size_in_pages(size + (start & EFI_PAGE_MASK));
	start &= ~EFI_PAGE_MASK;

	return efi_update_memory_map(start, pages, memory_type, false, false);
}

/**
 * efi_check_allocated() - validate address to be freed
 *
 * Check that the address is within allocated memory:
 *
 * * The address must be in a range of the memory map.
 * * The address may not point to EFI_CONVENTIONAL_MEMORY.
 *
 * Page alignment is not checked as this is not a requirement of
 * efi_free_pool().
 *
 * @addr:		address of page to be freed
 * @must_be_allocated:	return success if the page is allocated
 * Return:		status code
 */
static efi_status_t efi_check_allocated(u64 addr, bool must_be_allocated)
{
	struct mem_node *item;

	list_for_each_entry(item, &efi_mem, link) {
		u64 start = item->base;
		u64 end = start + (item->num_pages << EFI_PAGE_SHIFT);

		if (addr >= start && addr < end) {
			if (must_be_allocated ^
			    (item->type == EFI_CONVENTIONAL_MEMORY))
				return EFI_SUCCESS;
			else
				return EFI_NOT_FOUND;
		}
	}

	return EFI_NOT_FOUND;
}

static efi_status_t efi_allocate_pages_(enum efi_allocate_type type,
					enum efi_memory_type memory_type,
					efi_uintn_t pages, uint64_t *memory)
{
	u64 len;
	uint flags;
	efi_status_t ret;
	phys_addr_t addr;

	/* Check import parameters */
	if (memory_type >= EFI_PERSISTENT_MEMORY_TYPE &&
	    memory_type <= 0x6FFFFFFF)
		return EFI_INVALID_PARAMETER;
	if (!memory)
		return EFI_INVALID_PARAMETER;
	len = (u64)pages << EFI_PAGE_SHIFT;
	/* Catch possible overflow on 64bit systems */
	if (sizeof(efi_uintn_t) == sizeof(u64) &&
	    (len >> EFI_PAGE_SHIFT) != (u64)pages)
		return EFI_OUT_OF_RESOURCES;

	flags = LMB_NOOVERWRITE | LMB_NONOTIFY;
	switch (type) {
	case EFI_ALLOCATE_ANY_PAGES:
		/* Any page */
		addr = (u64)lmb_alloc_base(len, EFI_PAGE_SIZE,
						 LMB_ALLOC_ANYWHERE, flags);
		if (!addr)
			return EFI_OUT_OF_RESOURCES;
		break;
	case EFI_ALLOCATE_MAX_ADDRESS:
		/* Max address */
		addr = (u64)lmb_alloc_base(len, EFI_PAGE_SIZE, *memory,
						 flags);
		if (!addr)
			return EFI_OUT_OF_RESOURCES;
		break;
	case EFI_ALLOCATE_ADDRESS:
		if (*memory & EFI_PAGE_MASK)
			return EFI_NOT_FOUND;

		addr = *memory;
		if (lmb_alloc_addr(addr, len, flags))
			return EFI_NOT_FOUND;
		break;
	default:
		/* UEFI doesn't specify other allocation types */
		return EFI_INVALID_PARAMETER;
	}

	/* Reserve that map in our memory maps */
	ret = efi_update_memory_map(addr, pages, memory_type, true, false);
	if (ret != EFI_SUCCESS) {
		/* Map would overlap, bail out */
		lmb_free_flags(addr, (u64)pages << EFI_PAGE_SHIFT, flags);
		return  EFI_OUT_OF_RESOURCES;
	}

	*memory = addr;

	return EFI_SUCCESS;
}

efi_status_t efi_allocate_pages(enum efi_allocate_type type,
				enum efi_memory_type memory_type,
				efi_uintn_t pages, uint64_t *memory)
{
	efi_status_t ret;
	int ofs;

	ofs = efi_logs_allocate_pages(type, memory_type, pages, memory);
	ret = efi_allocate_pages_(type, memory_type, pages, memory);
	efi_loge_allocate_pages(ofs, ret);

	return ret;
}

static efi_status_t efi_free_pages_(uint64_t memory, efi_uintn_t pages)
{
	u64 len;
	long status;
	efi_status_t ret;

	ret = efi_check_allocated(memory, true);
	if (ret != EFI_SUCCESS)
		return ret;

	/* Sanity check */
	if (!memory || (memory & EFI_PAGE_MASK) || !pages) {
		printf("%s: illegal free 0x%llx, 0x%zx\n", __func__,
		       memory, pages);
		return EFI_INVALID_PARAMETER;
	}

	len = (u64)pages << EFI_PAGE_SHIFT;
	status = lmb_free_flags(memory, len, LMB_NOOVERWRITE);
	if (status)
		return EFI_NOT_FOUND;

	return ret;
}

efi_status_t efi_free_pages(u64 memory, efi_uintn_t pages)
{
	efi_status_t ret;
	int ofs;

	ofs = efi_logs_free_pages(memory, pages);
	ret = efi_free_pages_(memory, pages);
	efi_loge_free_pages(ofs, ret);

	return ret;
}

void *efi_alloc_aligned_pages(u64 len, enum efi_memory_type memory_type,
			      size_t align)
{
	u64 req_pages = efi_size_in_pages(len);
	u64 true_pages = req_pages + efi_size_in_pages(align) - 1;
	u64 free_pages;
	u64 aligned_mem;
	efi_status_t r;
	u64 mem;

	/* align must be zero or a power of two */
	if (align & (align - 1))
		return NULL;

	/* Check for overflow */
	if (true_pages < req_pages)
		return NULL;

	if (align < EFI_PAGE_SIZE) {
		r = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES, memory_type,
				       req_pages, &mem);
		return (r == EFI_SUCCESS) ? map_sysmem(mem, len) : NULL;
	}

	r = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES, memory_type,
			       true_pages, &mem);
	if (r != EFI_SUCCESS)
		return NULL;

	/*
	 * Align based on the mapped pointer rather than the physical address,
	 * since on sandbox these differ by the RAM-buffer base address. This
	 * ensures callers get a pointer with the requested alignment.
	 */
	aligned_mem = mem + ALIGN((ulong)map_sysmem(mem, 0), align) -
		(ulong)map_sysmem(mem, 0);

	/* Free pages before alignment */
	free_pages = efi_size_in_pages(aligned_mem - mem);
	if (free_pages)
		efi_free_pages(mem, free_pages);

	/* Free trailing pages */
	free_pages = true_pages - (req_pages + free_pages);
	if (free_pages) {
		mem = aligned_mem + req_pages * EFI_PAGE_SIZE;
		efi_free_pages(mem, free_pages);
	}

	return map_sysmem(aligned_mem, len);
}

static efi_status_t efi_allocate_pool_(enum efi_memory_type pool_type,
				       efi_uintn_t size, void **buffer)
{
	efi_status_t r;
	u64 addr;
	struct efi_pool_allocation *alloc;
	u64 num_pages = efi_size_in_pages(size +
					  sizeof(struct efi_pool_allocation));

	if (!buffer)
		return EFI_INVALID_PARAMETER;

	if (size == 0) {
		*buffer = NULL;
		return EFI_SUCCESS;
	}

	r = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES, pool_type, num_pages,
			       &addr);
	if (r == EFI_SUCCESS) {
		alloc = map_sysmem(addr, size);
		alloc->num_pages = num_pages;
		alloc->checksum = checksum(alloc);
		*buffer = alloc->data;
	}

	return r;
}

efi_status_t efi_allocate_pool(enum efi_memory_type pool_type, efi_uintn_t size,
			       void **buffer)
{
	efi_status_t ret;
	int ofs;

	ofs = efi_logs_allocate_pool(pool_type, size, buffer);
	ret = efi_allocate_pool_(pool_type, size, buffer);
	efi_loge_allocate_pool(ofs, ret);

	return ret;
}

void *efi_alloc(size_t size)
{
	void *buf;

	if (efi_allocate_pool(EFI_BOOT_SERVICES_DATA, size, &buf) !=
	    EFI_SUCCESS) {
		log_err("out of memory\n");
		return NULL;
	}
	memset(buf, 0, size);

	return buf;
}

efi_status_t efi_realloc(void **ptr, size_t size)
{
	efi_status_t ret;
	void *new_ptr;
	struct efi_pool_allocation *alloc;
	u64 num_pages = efi_size_in_pages(size +
					  sizeof(struct efi_pool_allocation));
	size_t old_size;

	if (!*ptr) {
		*ptr = efi_alloc(size);
		if (*ptr)
			return EFI_SUCCESS;
		return EFI_OUT_OF_RESOURCES;
	}

	ret = efi_check_allocated(map_to_sysmem(*ptr), true);
	if (ret != EFI_SUCCESS)
		return ret;

	alloc = container_of(*ptr, struct efi_pool_allocation, data);

	/* Check that this memory was allocated by efi_allocate_pool() */
	if (((uintptr_t)alloc & EFI_PAGE_MASK) ||
	    alloc->checksum != checksum(alloc)) {
		printf("%s: illegal realloc 0x%p\n", __func__, *ptr);
		return EFI_INVALID_PARAMETER;
	}

	/* Don't realloc. The actual size in pages is the same. */
	if (alloc->num_pages == num_pages)
		return EFI_SUCCESS;

	old_size = alloc->num_pages * EFI_PAGE_SIZE -
		sizeof(struct efi_pool_allocation);

	new_ptr = efi_alloc(size);
	if (!new_ptr)
		return EFI_OUT_OF_RESOURCES;

	/* copy old data to new alloced buffer */
	memcpy(new_ptr, *ptr, min(size, old_size));

	/* free the old buffer */
	efi_free_pool(*ptr);

	*ptr = new_ptr;

	return EFI_SUCCESS;
}

static efi_status_t efi_free_pool_(void *buffer)
{
	efi_status_t ret;
	struct efi_pool_allocation *alloc;

	if (!buffer)
		return EFI_INVALID_PARAMETER;

	ret = efi_check_allocated(map_to_sysmem(buffer), true);
	if (ret != EFI_SUCCESS)
		return ret;

	alloc = container_of(buffer, struct efi_pool_allocation, data);

	/* Check that this memory was allocated by efi_allocate_pool() */
	if (((uintptr_t)alloc & EFI_PAGE_MASK) ||
	    alloc->checksum != checksum(alloc)) {
		printf("%s: illegal free 0x%p\n", __func__, buffer);
		return EFI_INVALID_PARAMETER;
	}
	/* Avoid double free */
	alloc->checksum = 0;

	ret = efi_free_pages(map_to_sysmem(alloc), alloc->num_pages);

	return ret;
}

efi_status_t efi_free_pool(void *buffer)
{
	efi_status_t ret;
	int ofs;

	ofs = efi_logs_free_pool(buffer);
	ret = efi_free_pool_(buffer);
	efi_loge_free_pool(ofs, ret);

	return ret;
}

/**
 * efi_get_memory_map() - get map describing memory usage.
 *
 * @memory_map_size:	on entry the size, in bytes, of the memory map buffer,
 *			on exit the size of the copied memory map
 * @memory_map:		buffer to which the memory map is written
 * @map_key:		key for the memory map
 * @descriptor_size:	size of an individual memory descriptor
 * @descriptor_version:	version number of the memory descriptor structure
 * Return:		status code
 */
efi_status_t efi_get_memory_map(efi_uintn_t *memory_map_size,
				struct efi_mem_desc *memory_map,
				efi_uintn_t *map_key,
				efi_uintn_t *descriptor_size,
				uint32_t *descriptor_version)
{
	size_t map_entries;
	efi_uintn_t map_size = 0;
	struct mem_node *lmem;
	efi_uintn_t provided_map_size;

	if (!memory_map_size)
		return EFI_INVALID_PARAMETER;

	provided_map_size = *memory_map_size;

	map_entries = list_count_nodes(&efi_mem);

	map_size = map_entries * sizeof(struct efi_mem_desc);

	*memory_map_size = map_size;

	if (descriptor_size)
		*descriptor_size = sizeof(struct efi_mem_desc);

	if (descriptor_version)
		*descriptor_version = EFI_MEMORY_DESCRIPTOR_VERSION;

	if (provided_map_size < map_size)
		return EFI_BUFFER_TOO_SMALL;

	if (!memory_map)
		return EFI_INVALID_PARAMETER;

	/* Copy list into array */
	/* Return the list in ascending order */
	memory_map = &memory_map[map_entries - 1];
	list_for_each_entry(lmem, &efi_mem, link) {
		memory_map->type = lmem->type;
		memory_map->reserved = 0;
		memory_map->physical_start = (u64)(ulong)map_sysmem(lmem->base,
					lmem->num_pages * EFI_PAGE_SIZE);

		/* virtual and physical are always the same */
		memory_map->virtual_start = memory_map->physical_start;
		memory_map->num_pages = lmem->num_pages;
		memory_map->attribute = lmem->attribute;
		memory_map--;
	}

	if (map_key)
		*map_key = efi_memory_map_key;

	return EFI_SUCCESS;
}

/**
 * efi_get_memory_map_alloc() - allocate map describing memory usage
 *
 * The caller is responsible for calling FreePool() if the call succeeds.
 *
 * @map_size:		size of the memory map
 * @memory_map:		buffer to which the memory map is written
 * Return:		status code
 */
efi_status_t efi_get_memory_map_alloc(efi_uintn_t *map_size,
				      struct efi_mem_desc **memory_map)
{
	efi_status_t ret;

	*memory_map = NULL;
	*map_size = 0;
	ret = efi_get_memory_map(map_size, *memory_map, NULL, NULL, NULL);
	if (ret == EFI_BUFFER_TOO_SMALL) {
		*map_size += sizeof(struct efi_mem_desc); /* for the map */
		ret = efi_allocate_pool(EFI_BOOT_SERVICES_DATA, *map_size,
					(void **)memory_map);
		if (ret != EFI_SUCCESS)
			return ret;
		ret = efi_get_memory_map(map_size, *memory_map,
					 NULL, NULL, NULL);
		if (ret != EFI_SUCCESS) {
			efi_free_pool(*memory_map);
			*memory_map = NULL;
		}
	}

	return ret;
}

__weak void efi_add_known_memory(void)
{
}

/**
 * add_u_boot_and_runtime() - add U-Boot code to memory map
 *
 * Add memory regions for U-Boot's memory and for the runtime services code.
 */
static void add_u_boot_and_runtime(void)
{
	unsigned long runtime_start, runtime_end, runtime_pages;
	unsigned long runtime_mask = EFI_PAGE_MASK;
	unsigned long uboot_start, uboot_pages;
	unsigned long uboot_stack_size = CONFIG_STACK_SIZE;

	if (!IS_ENABLED(CONFIG_SANDBOX)) {
		/* Add U-Boot */
		uboot_start = ((uintptr_t)map_sysmem(gd->start_addr_sp, 0) -
			       uboot_stack_size) & ~EFI_PAGE_MASK;
		uboot_pages = ((uintptr_t)map_sysmem(gd->ram_top - 1, 0) -
			       uboot_start + EFI_PAGE_MASK) >> EFI_PAGE_SHIFT;
		efi_update_memory_map(uboot_start, uboot_pages,
				      EFI_BOOT_SERVICES_CODE, false, false);
	}

#if defined(__aarch64__)
	/*
	 * Runtime Services must be 64KiB aligned according to the
	 * "AArch64 Platforms" section in the UEFI spec (2.7+).
	 */

	runtime_mask = SZ_64K - 1;
#endif

	if (!IS_ENABLED(CONFIG_SANDBOX)) {
		/*
		 * Add Runtime Services. We mark surrounding boottime code as
		 * runtime as well to fulfill the runtime alignment constraints
		 * but avoid padding.
		 *
		 * This is not enabled for sandbox, since we cannot map the
		 * sandbox code into emulated SDRAM
		 */
		runtime_start = (uintptr_t)__efi_runtime_start & ~runtime_mask;
		runtime_end = (uintptr_t)__efi_runtime_stop;
		runtime_end = (runtime_end + runtime_mask) & ~runtime_mask;
		runtime_pages = (runtime_end - runtime_start) >> EFI_PAGE_SHIFT;
		efi_update_memory_map(runtime_start, runtime_pages,
				      EFI_RUNTIME_SERVICES_CODE, false, false);
	}
}

int efi_memory_init(void)
{
	efi_add_known_memory();

#ifdef CONFIG_EFI_STUB
	efi_add_known_memory_from_efi();
#endif

	add_u_boot_and_runtime();

#ifdef CONFIG_EFI_LOADER_BOUNCE_BUFFER
	/* Request a 32bit 64MB bounce buffer region */
	uint64_t efi_bounce_buffer_addr = 0xffffffff;

	if (efi_allocate_pages(EFI_ALLOCATE_MAX_ADDRESS, EFI_BOOT_SERVICES_DATA,
			       SZ_64M >> EFI_PAGE_SHIFT,
			       &efi_bounce_buffer_addr) != EFI_SUCCESS)
		return -1;

	efi_bounce_buffer = map_sysmem(efi_bounce_buffer_addr, SZ_64M);
#endif

	return 0;
}

int efi_map_update_notify(phys_addr_t addr, phys_size_t size,
			  enum lmb_map_op op)
{
	u64 efi_addr;
	u64 pages;
	efi_status_t status;

	efi_addr = (uintptr_t)map_sysmem(addr, 0);
	pages = efi_size_in_pages(size + (efi_addr & EFI_PAGE_MASK));
	efi_addr &= ~EFI_PAGE_MASK;

	status = efi_update_memory_map(efi_addr, pages,
				       op == LMB_MAP_OP_RESERVE ?
				       EFI_BOOT_SERVICES_DATA :
				       EFI_CONVENTIONAL_MEMORY,
				       false, false);
	if (status != EFI_SUCCESS) {
		log_err("LMB Map notify failure %lu\n",
			status & ~EFI_ERROR_MASK);
		return -1;
	}
	unmap_sysmem((void *)(uintptr_t)efi_addr);

	return 0;
}
