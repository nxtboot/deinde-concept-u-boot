// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI resource allocation which places the largest resources first
 *
 * Once a root bus has scanned all its buses, this sizes every BAR, expansion
 * ROM and bridge window from the leaves up, then assigns each bus's resources
 * with the largest alignment first, so that they pack without gaps and a
 * large BAR is not left without an aligned block. See pci_auto_simple.c for
 * the smaller allocator which assigns resources as it finds them.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <alist.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <pci.h>
#include <sort.h>
#include <linux/sizes.h>
#include "pci_internal.h"

/* granularity of a bridge's I/O window */
#define BRIDGE_IO_ALIGN		0x1000

/**
 * enum pciauto_type - the types of resource, each allocated from its own region
 *
 * @RES_MEM: non-prefetchable memory
 * @RES_PREF: prefetchable memory
 * @RES_IO: I/O space
 * @RES_COUNT: number of types
 */
enum pciauto_type {
	RES_MEM,
	RES_PREF,
	RES_IO,

	RES_COUNT,
};

/**
 * struct pciauto_res - a resource to allocate
 *
 * A BAR, an expansion ROM or one of a bridge's windows onto the bus behind it.
 * This is kept small since the pre-relocation heap is tiny on some boards
 *
 * @dev: index of the device it belongs to, in the bus's list
 * @index: position in the bus's list, to keep the sort stable
 * @offset: config offset of the BAR or ROM register, or 0 for a window
 * @type: resource type (enum pciauto_type)
 * @is64: true if the resource can take an address above 4GB
 * @size: bytes needed
 * @align: alignment needed: the size for a BAR, for a window the largest
 *	alignment of the resources behind it
 */
struct pciauto_res {
	u16 dev;
	u16 index;
	u8 offset;
	u8 type;
	bool is64;
	pci_size_t size;
	pci_size_t align;
};

/**
 * struct pciauto_dev - a device being configured
 *
 * @dev: the device
 * @child: for a bridge, the bus behind it, else NULL
 * @cmd: PCI_COMMAND bits to enable, for the resource types it decodes
 */
struct pciauto_dev {
	struct udevice *dev;
	struct pciauto_bus *child;
	u16 cmd;
};

/**
 * struct pciauto_bus - the resources needed by a bus and those behind it
 *
 * @devs: its devices (struct pciauto_dev)
 * @res: their resources (struct pciauto_res), largest alignment first once
 *	sized
 * @size: total size needed for each type, packed in that order
 * @align: largest alignment needed for each type
 * @win_base: for the bus behind a bridge, the start of the bridge's window
 *	of each type, once assigned
 * @win_size: size of each window (0 if none)
 */
struct pciauto_bus {
	struct alist devs;
	struct alist res;
	pci_size_t size[RES_COUNT];
	pci_size_t align[RES_COUNT];
	pci_addr_t win_base[RES_COUNT];
	pci_size_t win_size[RES_COUNT];
};

/**
 * struct pciauto_ctx - what the passes need to know about the controller
 *
 * @hose: the root bus's controller, which holds the regions
 * @pref32: true if the prefetchable region lies entirely below 4GB, so that
 *	32-bit BARs and windows can use it
 */
struct pciauto_ctx {
	struct pci_controller *hose;
	bool pref32;
};

/**
 * struct pciauto_scan - what pciauto_add_res() needs to record a resource
 *
 * @ctx: controller context
 * @ab: bus the device is on
 * @idx: index of the device in the bus's list
 * @pref_ok: true if prefetchable BARs can go in the prefetchable region
 */
struct pciauto_scan {
	struct pciauto_ctx *ctx;
	struct pciauto_bus *ab;
	int idx;
	bool pref_ok;
};

static int pciauto_size_bus(struct pciauto_ctx *ctx, struct udevice *bus,
			    bool pref_ok, struct pciauto_bus **abp);

static void pciauto_free_bus(struct pciauto_bus *ab)
{
	struct pciauto_dev *adev;

	if (!ab)
		return;
	alist_for_each(adev, &ab->devs)
		pciauto_free_bus(adev->child);
	alist_uninit(&ab->devs);
	alist_uninit(&ab->res);
	free(ab);
}

/**
 * pciauto_alloc_bus() - Allocate a bus record with room for its devices
 *
 * The lists are sized up front, since a heap which cannot free (before
 * relocation) would otherwise fill up with their outgrown copies
 *
 * @ndevs: number of devices the bus has
 * Return: new record, or NULL if out of memory
 */
static struct pciauto_bus *pciauto_alloc_bus(int ndevs)
{
	struct pciauto_bus *ab;

	ab = calloc(1, sizeof(*ab));
	if (!ab)
		return NULL;
	if (!alist_init(&ab->devs, sizeof(struct pciauto_dev), ndevs) ||
	    !alist_init(&ab->res, sizeof(struct pciauto_res), 2 * ndevs)) {
		pciauto_free_bus(ab);
		return NULL;
	}

	return ab;
}

static const char *pciauto_type_name(uint type)
{
	static const char *const names[RES_COUNT] = { "Mem", "Prf", "I/O" };

	return names[type];
}

/*
 * Add a resource to a bus's list
 *
 * A failure to grow the list sets its ALISTF_FAIL flag, which the caller
 * checks with alist_err() once all resources are added, so that a partial
 * list is never assigned.
 */
static void pciauto_push_res(struct pciauto_bus *ab, int dev, uint offset,
			     enum pciauto_type type, bool is64,
			     pci_size_t size, pci_size_t align)
{
	struct pciauto_res res = {
		.dev = dev,
		.offset = offset,
		.type = type,
		.is64 = is64,
		.size = size,
		.align = align,
		.index = ab->res.count,
	};

	alist_add(&ab->res, res);
}

void pciauto_add_res(struct udevice *dev, uint offset, uint flags,
		     pci_size_t size, void *priv)
{
	struct pciauto_scan *scan = priv;
	bool is64 = flags & PCIAUTO_BAR_64;
	enum pciauto_type type;

	if (flags & PCIAUTO_BAR_IO)
		type = RES_IO;
	else if (scan->pref_ok && (flags & PCIAUTO_BAR_PREFETCH) &&
		 (is64 || scan->ctx->pref32))
		type = RES_PREF;
	else
		type = RES_MEM;
	pciauto_push_res(scan->ab, scan->idx, offset, type, is64, size, size);
}

/**
 * pciauto_size_bars() - Find the sizes of a device's BARs and expansion ROM
 *
 * Each is added to the bus's list with the alignment it needs, which is its
 * size. The BARs are left with the size probe written to them, so they must
 * be assigned (or cleared) afterwards.
 *
 * @ctx: controller context
 * @ab: bus the device is on
 * @idx: index of the device in the bus's list
 * @pref_ok: true if prefetchable BARs can go in the prefetchable region
 */
static void pciauto_size_bars(struct pciauto_ctx *ctx, struct pciauto_bus *ab,
			      int idx, bool pref_ok)
{
	struct pciauto_dev *adev = alist_getw(&ab->devs, idx,
					     struct pciauto_dev);
	struct pciauto_scan scan = {
		.ctx = ctx,
		.ab = ab,
		.idx = idx,
		.pref_ok = pref_ok,
	};

	adev->cmd |= pciauto_find_res(adev->dev, &scan);
}

/**
 * pciauto_size_bridge() - Size the bus behind a bridge and add its windows
 *
 * The bus behind the bridge is sized recursively. For each type it needs, a
 * window is added to the bridge's own bus, rounded up to the granularity of
 * the bridge's base and limit registers and aligned to the largest resource
 * behind it, so that they pack without gaps.
 *
 * @ctx: controller context
 * @ab: bus the bridge is on
 * @idx: index of the bridge in the bus's list
 * @pref_ok: true if prefetchable BARs can go in the prefetchable region
 * Return: 0 if OK, -ve on error
 */
static int pciauto_size_bridge(struct pciauto_ctx *ctx, struct pciauto_bus *ab,
			       int idx, bool pref_ok)
{
	struct pciauto_dev *adev = alist_getw(&ab->devs, idx,
					     struct pciauto_dev);
	struct udevice *dev = adev->dev;
	struct pciauto_bus *child;
	u16 pref_type;
	bool pref64;
	int ret, t;

	dm_pci_read_config16(dev, PCI_PREF_MEMORY_BASE, &pref_type);
	pref64 = (pref_type & PCI_PREF_RANGE_TYPE_MASK) ==
		PCI_PREF_RANGE_TYPE_64;

	/* a 32-bit prefetchable window cannot reach a region above 4GB */
	ret = pciauto_size_bus(ctx, dev, pref_ok && (pref64 || ctx->pref32),
			       &child);
	if (ret)
		return log_msg_ret("size", ret);
	adev->child = child;

	for (t = 0; t < RES_COUNT; t++) {
		pci_size_t gran, size, align;

		if (!child->size[t])
			continue;
		gran = t == RES_IO ? BRIDGE_IO_ALIGN :
			CONFIG_PCI_BRIDGE_MEM_ALIGNMENT;
		size = ALIGN(child->size[t], gran);
		align = max(child->align[t], gran);
		log_debug("%s: %s window, size=%llx align=%llx\n", dev->name,
			  pciauto_type_name(t), (unsigned long long)size,
			  (unsigned long long)align);
		pciauto_push_res(ab, idx, 0, t, t == RES_PREF && pref64, size,
				 align);
	}

	return 0;
}

/* Sort resources by alignment, then size, largest first, keeping the order */
static int pciauto_res_compare(const void *a, const void *b)
{
	const struct pciauto_res *ra = a, *rb = b;

	if (ra->align != rb->align)
		return ra->align > rb->align ? -1 : 1;
	if (ra->size != rb->size)
		return ra->size > rb->size ? -1 : 1;

	return ra->index - rb->index;
}

/**
 * pciauto_size_bus() - Collect the resources of a bus and those behind it
 *
 * Builds the list of resources on the bus (the BARs, ROMs and bridge windows
 * of its devices), sorted with the largest alignment first, and works out
 * the space each type needs when packed in that order. Since each alignment
 * is a power of two and the sizes are multiples of their alignment, this
 * packing has no gaps once the start is aligned to the largest.
 *
 * @ctx: controller context
 * @bus: bus to size
 * @pref_ok: true if prefetchable BARs can go in the prefetchable region
 * @abp: returns the new bus record, to be freed with pciauto_free_bus()
 * Return: 0 if OK, -ve on error
 */
static int pciauto_size_bus(struct pciauto_ctx *ctx, struct udevice *bus,
			    bool pref_ok, struct pciauto_bus **abp)
{
	struct pciauto_bus *ab;
	struct pciauto_res *res;
	struct udevice *dev;
	int count = 0;
	int ret;

	device_foreach_child(dev, bus)
		count++;
	ab = pciauto_alloc_bus(count);
	if (!ab)
		return log_msg_ret("ab", -ENOMEM);

	device_foreach_child(dev, bus) {
		struct pciauto_dev *adev;
		u16 class;
		int idx;

		if (dev_has_ofnode(dev) &&
		    dev_read_bool(dev, "pci,no-autoconfig"))
			continue;
		dm_pci_read_config16(dev, PCI_CLASS_DEVICE, &class);
		if (IS_ENABLED(CONFIG_PCIAUTO_SKIP_HOST_BRIDGE) &&
		    class == PCI_CLASS_BRIDGE_OTHER)
			continue;

		idx = ab->devs.count;
		adev = alist_add_placeholder(&ab->devs);
		if (!adev)
			break;
		adev->dev = dev;
		pciauto_size_bars(ctx, ab, idx, pref_ok);

		/* a bridge which was probed is a bus with devices behind it */
		if (class == PCI_CLASS_BRIDGE_PCI && device_active(dev) &&
		    device_get_uclass_id(dev) == UCLASS_PCI) {
			ret = pciauto_size_bridge(ctx, ab, idx, pref_ok);
			if (ret) {
				pciauto_free_bus(ab);
				return log_msg_ret("bridge", ret);
			}
		}
	}
	if (alist_err(&ab->devs) || alist_err(&ab->res)) {
		pciauto_free_bus(ab);
		return log_msg_ret("mem", -ENOMEM);
	}

	qsort(ab->res.data, ab->res.count, ab->res.obj_size,
	      pciauto_res_compare);
	alist_for_each(res, &ab->res) {
		int t = res->type;

		ab->size[t] = ALIGN(ab->size[t], res->align) + res->size;
		ab->align[t] = max(ab->align[t], res->align);
	}
	*abp = ab;

	return 0;
}

/**
 * pciauto_setup_windows() - Program a bridge's base and limit registers
 *
 * Each window is set to the sub-region allocated for it, or disabled (limit
 * below base) if the bus behind the bridge does not need that type.
 *
 * @dev: the bridge
 * @wins: its window of each type (size 0 if none)
 */
static void pciauto_setup_windows(struct udevice *dev, struct pci_region *wins)
{
	struct pci_region *win;
	pci_addr_t base, limit;
	u16 pref_type;
	u8 io_type;

	win = &wins[RES_MEM];
	if (win->size) {
		base = win->bus_start;
		limit = win->bus_start + win->size - 1;
		dm_pci_write_config16(dev, PCI_MEMORY_BASE,
				      (base >> 16) & PCI_MEMORY_RANGE_MASK);
		dm_pci_write_config16(dev, PCI_MEMORY_LIMIT,
				      (limit >> 16) & PCI_MEMORY_RANGE_MASK);
	} else {
		dm_pci_write_config16(dev, PCI_MEMORY_BASE, 0xfff0);
		dm_pci_write_config16(dev, PCI_MEMORY_LIMIT, 0);
	}

	dm_pci_read_config16(dev, PCI_PREF_MEMORY_BASE, &pref_type);
	pref_type &= PCI_PREF_RANGE_TYPE_MASK;
	win = &wins[RES_PREF];
	if (win->size) {
		base = win->bus_start;
		limit = win->bus_start + win->size - 1;
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_BASE,
				      ((base >> 16) & PCI_PREF_RANGE_MASK) |
				      pref_type);
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT,
				      ((limit >> 16) & PCI_PREF_RANGE_MASK) |
				      pref_type);
		if (pref_type == PCI_PREF_RANGE_TYPE_64) {
			dm_pci_write_config32(dev, PCI_PREF_BASE_UPPER32,
					      upper_32_bits((u64)base));
			dm_pci_write_config32(dev, PCI_PREF_LIMIT_UPPER32,
					      upper_32_bits((u64)limit));
		}
	} else {
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_BASE,
				      0xfff0 | pref_type);
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT, pref_type);
		if (pref_type == PCI_PREF_RANGE_TYPE_64) {
			dm_pci_write_config32(dev, PCI_PREF_BASE_UPPER32, 0);
			dm_pci_write_config32(dev, PCI_PREF_LIMIT_UPPER32, 0);
		}
	}

	dm_pci_read_config8(dev, PCI_IO_BASE, &io_type);
	io_type &= PCI_IO_RANGE_TYPE_MASK;
	win = &wins[RES_IO];
	if (win->size) {
		base = win->bus_start;
		limit = win->bus_start + win->size - 1;
		dm_pci_write_config8(dev, PCI_IO_BASE,
				     ((base >> 8) & PCI_IO_RANGE_MASK) |
				     io_type);
		dm_pci_write_config8(dev, PCI_IO_LIMIT,
				     ((limit >> 8) & PCI_IO_RANGE_MASK) |
				     io_type);
		if (io_type == PCI_IO_RANGE_TYPE_32) {
			dm_pci_write_config16(dev, PCI_IO_BASE_UPPER16,
					      base >> 16);
			dm_pci_write_config16(dev, PCI_IO_LIMIT_UPPER16,
					      limit >> 16);
		}
	} else {
		dm_pci_write_config8(dev, PCI_IO_BASE, 0xf0 | io_type);
		dm_pci_write_config8(dev, PCI_IO_LIMIT, io_type);
		if (io_type == PCI_IO_RANGE_TYPE_32) {
			dm_pci_write_config16(dev, PCI_IO_BASE_UPPER16, 0);
			dm_pci_write_config16(dev, PCI_IO_LIMIT_UPPER16, 0);
		}
	}
}

/**
 * pciauto_assign_bus() - Allocate a bus's resources and program its devices
 *
 * The resources are taken from the regions in the order the sizing pass
 * sorted them, largest alignment first. A bridge's windows become the
 * regions for the bus behind it, which is then assigned in the same way.
 *
 * @ctx: controller context
 * @ab: bus to assign
 * @regs: region for each type, or NULL if the bus has none of that type
 */
static void pciauto_assign_bus(struct pciauto_ctx *ctx, struct pciauto_bus *ab,
			       struct pci_region *regs[RES_COUNT])
{
	struct pciauto_dev *adev;
	struct pciauto_res *res;
	int t;

	alist_for_each(res, &ab->res) {
		struct pci_region *reg = regs[res->type];
		pci_addr_t addr;
		int ret;

		adev = alist_getw(&ab->devs, res->dev, struct pciauto_dev);
		ret = pciauto_region_allocate_aligned(reg, res->size,
						      res->align, &addr,
						      res->is64);
		if (ret) {
			if (res->offset) {
				printf("PCI: Failed autoconfig bar %x\n",
				       res->offset);
			} else {
				printf("PCI: Failed autoconfig %s window for %s\n",
				       pciauto_type_name(res->type),
				       adev->dev->name);
			}
			addr = 0;
		}
		if (res->offset) {
			pciauto_write_bar(adev->dev, res->offset, res->is64,
					  addr);
		} else if (!ret) {
			adev->child->win_base[res->type] = addr;
			adev->child->win_size[res->type] = res->size;
		}
	}

	alist_for_each(adev, &ab->devs) {
		struct udevice *dev = adev->dev;

		if (adev->child) {
			struct pciauto_bus *child = adev->child;
			struct pci_region win[RES_COUNT];
			struct pci_region *sub[RES_COUNT];

			/* the windows are the regions of the bus behind */
			for (t = 0; t < RES_COUNT; t++) {
				struct pci_region *reg = regs[t];
				struct pci_region *w = &win[t];

				w->size = child->win_size[t];
				sub[t] = NULL;
				if (!w->size)
					continue;
				w->bus_start = child->win_base[t];
				w->bus_lower = w->bus_start;
				w->phys_start = reg->phys_start + w->bus_start -
					reg->bus_start;
				w->flags = reg->flags;
				sub[t] = w;
				adev->cmd |= t == RES_IO ? PCI_COMMAND_IO :
					PCI_COMMAND_MEMORY;
			}
			pciauto_setup_windows(adev->dev, win);
			pciauto_assign_bus(ctx, child, sub);
		}

		pciauto_finish_device(dev, adev->cmd);
	}
}

int pciauto_alloc_resources(struct udevice *bus)
{
	struct udevice *ctlr = pci_get_controller(bus);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	struct pci_region *regs[RES_COUNT];
	bool pref_ok = !!hose->pci_prefetch;
	struct pciauto_bus *ab;
	struct pciauto_ctx ctx;
	int ret;

	ctx.hose = hose;
	ctx.pref32 = hose->pci_prefetch &&
		!upper_32_bits((u64)hose->pci_prefetch->bus_start +
			       hose->pci_prefetch->size - 1);
	regs[RES_MEM] = hose->pci_mem;
	regs[RES_PREF] = hose->pci_prefetch;
	regs[RES_IO] = hose->pci_io;

	if (bus == ctlr) {
		ret = pciauto_size_bus(&ctx, bus, pref_ok, &ab);
		if (ret)
			return log_msg_ret("size", ret);
		pciauto_assign_bus(&ctx, ab, regs);
		pciauto_free_bus(ab);
	} else {
		/*
		 * A bridge probed on its own, after its root bus was set up:
		 * treat it as the only device on a bus, so that its windows
		 * are allocated from what the root bus has left
		 */
		struct pciauto_bus *parent;
		struct pciauto_dev *adev;

		parent = pciauto_alloc_bus(1);
		if (!parent)
			return log_msg_ret("late", -ENOMEM);
		adev = alist_add_placeholder(&parent->devs);
		if (!adev) {
			pciauto_free_bus(parent);
			return log_msg_ret("dev", -ENOMEM);
		}
		adev->dev = bus;
		ret = pciauto_size_bridge(&ctx, parent, 0, pref_ok);
		if (!ret && alist_err(&parent->res))
			ret = -ENOMEM;
		if (!ret) {
			qsort(parent->res.data, parent->res.count,
			      parent->res.obj_size, pciauto_res_compare);
			pciauto_assign_bus(&ctx, parent, regs);
		}
		pciauto_free_bus(parent);
		if (ret)
			return log_msg_ret("size", ret);
	}

	return 0;
}
