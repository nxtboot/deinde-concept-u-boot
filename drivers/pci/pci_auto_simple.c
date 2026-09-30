// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI resource allocation in the order the devices are found
 *
 * Each BAR is assigned as its device is scanned, from a pointer which moves
 * up through the region, and a bridge's windows cover whatever its bus used
 * between the pre-scan and the post-scan. This is small, but a large BAR can
 * be left without an aligned block once smaller ones have taken the start of
 * the space; pci_auto_sorted.c avoids that at a cost of about 1KB of code.
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2000 MontaVista Software Inc.
 * Copyright (c) 2021  Maciej W. Rozycki <macro@orcam.me.uk>
 */

#include <dm.h>
#include <log.h>
#include <pci.h>
#include "pci_internal.h"

void pciauto_add_res(struct udevice *dev, uint offset, uint flags,
		     pci_size_t size, void *priv)
{
	struct pci_controller *hose = priv;
	struct pci_region *prefetch = hose->pci_prefetch;
	bool is64 = flags & PCIAUTO_BAR_64;
	struct pci_region *res;
	pci_addr_t addr;

	if (flags & PCIAUTO_BAR_IO)
		res = hose->pci_io;
	else if (prefetch && (flags & PCIAUTO_BAR_PREFETCH) &&
		 (is64 || prefetch->bus_lower < 0x100000000ULL))
		res = prefetch;
	else
		res = hose->pci_mem;

	if (pciauto_region_allocate(res, size, &addr, is64))
		printf("PCI: Failed autoconfig bar %x\n", offset);
	else
		pciauto_write_bar(dev, offset, is64, addr);
}

void pciauto_alloc_device(struct udevice *dev)
{
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	u16 cmd;

	cmd = pciauto_find_res(dev, hose);
	pciauto_finish_device(dev, cmd);
}

void pciauto_open_windows(struct udevice *dev, struct pci_controller *hose)
{
	struct pci_region *pci_mem;
	struct pci_region *pci_prefetch;
	struct pci_region *pci_io;
	u16 cmdstat, pref_type;
	u8 io_32;

	pci_mem = hose->pci_mem;
	pci_prefetch = hose->pci_prefetch;
	pci_io = hose->pci_io;

	dm_pci_read_config16(dev, PCI_COMMAND, &cmdstat);
	dm_pci_read_config16(dev, PCI_PREF_MEMORY_BASE, &pref_type);
	pref_type &= PCI_PREF_RANGE_TYPE_MASK;
	dm_pci_read_config8(dev, PCI_IO_BASE, &io_32);
	io_32 &= PCI_IO_RANGE_TYPE_MASK;

	if (pci_mem) {
		/* Round memory allocator */
		pciauto_region_align(pci_mem, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		/*
		 * Set up memory and I/O filter limits, assume 32-bit
		 * I/O space
		 */
		dm_pci_write_config16(dev, PCI_MEMORY_BASE,
				      ((pci_mem->bus_lower & 0xfff00000) >> 16) &
				      PCI_MEMORY_RANGE_MASK);

		cmdstat |= PCI_COMMAND_MEMORY;
	}

	if (pci_prefetch) {
		/* Round memory allocator */
		pciauto_region_align(pci_prefetch, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		/*
		 * Set up memory and I/O filter limits, assume 32-bit
		 * I/O space
		 */
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_BASE,
				      (((pci_prefetch->bus_lower & 0xfff00000) >>
					16) & PCI_PREF_RANGE_MASK) |
				      pref_type);
		if (pref_type == PCI_PREF_RANGE_TYPE_64) {
			u32 upper = upper_32_bits(pci_prefetch->bus_lower);

			dm_pci_write_config32(dev, PCI_PREF_BASE_UPPER32, upper);
		}

		cmdstat |= PCI_COMMAND_MEMORY;
	} else {
		/* We don't support prefetchable memory for now, so disable */
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_BASE, 0xfff0 |
								pref_type);
		dm_pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT, 0x0 |
								pref_type);
		if (pref_type == PCI_PREF_RANGE_TYPE_64) {
			dm_pci_write_config16(dev, PCI_PREF_BASE_UPPER32, 0x0);
			dm_pci_write_config16(dev, PCI_PREF_LIMIT_UPPER32, 0x0);
		}
	}

	if (pci_io) {
		/* Round I/O allocator to 4KB boundary */
		pciauto_region_align(pci_io, 0x1000);

		dm_pci_write_config8(dev, PCI_IO_BASE,
				     (((pci_io->bus_lower & 0x0000f000) >> 8) &
				     PCI_IO_RANGE_MASK) | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32)
			dm_pci_write_config16(dev, PCI_IO_BASE_UPPER16,
					      (pci_io->bus_lower & 0xffff0000) >>
					      16);

		cmdstat |= PCI_COMMAND_IO;
	} else {
		/* Disable I/O if unsupported */
		dm_pci_write_config8(dev, PCI_IO_BASE, 0xf0 | io_32);
		dm_pci_write_config8(dev, PCI_IO_LIMIT, 0x0 | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32) {
			dm_pci_write_config16(dev, PCI_IO_BASE_UPPER16, 0x0);
			dm_pci_write_config16(dev, PCI_IO_LIMIT_UPPER16, 0x0);
		}
	}

	/* Enable memory and I/O accesses, enable bus master */
	dm_pci_write_config16(dev, PCI_COMMAND, cmdstat | PCI_COMMAND_MASTER);
}

void pciauto_close_windows(struct udevice *dev,
			   struct pci_controller *hose)
{
	struct pci_region *pci_mem;
	struct pci_region *pci_prefetch;
	struct pci_region *pci_io;

	pci_mem = hose->pci_mem;
	pci_prefetch = hose->pci_prefetch;
	pci_io = hose->pci_io;

	if (pci_mem) {
		/* Round memory allocator */
		pciauto_region_align(pci_mem, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		dm_pci_write_config16(dev, PCI_MEMORY_LIMIT,
				      ((pci_mem->bus_lower - 1) >> 16) &
				      PCI_MEMORY_RANGE_MASK);
	}

	if (pci_prefetch) {
		u16 pref_type;

		dm_pci_read_config16(dev, PCI_PREF_MEMORY_LIMIT,
				     &pref_type);
		pref_type &= PCI_PREF_RANGE_TYPE_MASK;

		/* Round memory allocator */
		pciauto_region_align(pci_prefetch, CONFIG_PCI_BRIDGE_MEM_ALIGNMENT);

		dm_pci_write_config16(dev, PCI_PREF_MEMORY_LIMIT,
				      (((pci_prefetch->bus_lower - 1) >> 16) &
				       PCI_PREF_RANGE_MASK) | pref_type);
		if (pref_type == PCI_PREF_RANGE_TYPE_64) {
			u32 upper = upper_32_bits(pci_prefetch->bus_lower - 1);

			dm_pci_write_config32(dev, PCI_PREF_LIMIT_UPPER32, upper);
		}
	}

	if (pci_io) {
		u8 io_32;

		dm_pci_read_config8(dev, PCI_IO_LIMIT, &io_32);
		io_32 &= PCI_IO_RANGE_TYPE_MASK;

		/* Round I/O allocator to 4KB boundary */
		pciauto_region_align(pci_io, 0x1000);

		dm_pci_write_config8(dev, PCI_IO_LIMIT,
				     ((((pci_io->bus_lower - 1) & 0x0000f000) >>
				       8) & PCI_IO_RANGE_MASK) | io_32);
		if (io_32 == PCI_IO_RANGE_TYPE_32)
			dm_pci_write_config16(dev, PCI_IO_LIMIT_UPPER16,
					      ((pci_io->bus_lower - 1) &
					       0xffff0000) >> 16);
	}
}
