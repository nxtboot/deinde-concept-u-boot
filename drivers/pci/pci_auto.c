// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI autoconfiguration library
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2000 MontaVista Software Inc.
 * Copyright (c) 2021  Maciej W. Rozycki <macro@orcam.me.uk>
 */

#include <config.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <pci.h>
#include <time.h>
#include <linux/bitops.h>
#include "pci_internal.h"

/* the user can define CFG_SYS_PCI_CACHE_LINE_SIZE to avoid problems */
#ifndef CFG_SYS_PCI_CACHE_LINE_SIZE
#define CFG_SYS_PCI_CACHE_LINE_SIZE	8
#endif

int pciauto_bar_count(struct udevice *dev, uint *rom_addrp)
{
	u8 header_type;

	dm_pci_read_config8(dev, PCI_HEADER_TYPE, &header_type);
	switch (header_type & 0x7f) {
	case PCI_HEADER_TYPE_NORMAL:
		*rom_addrp = PCI_ROM_ADDRESS;
		return 6;
	case PCI_HEADER_TYPE_BRIDGE:
		*rom_addrp = PCI_ROM_ADDRESS1;
		return 2;
	default:
		/* CardBus has no BARs; leave unknown header types alone */
		*rom_addrp = 0;
		return 0;
	}
}

pci_size_t pciauto_probe_bar(struct udevice *dev, uint bar, uint *flagsp)
{
	pci_size_t size;
	u32 resp;

	*flagsp = 0;

	/* Tickle the BAR and get the response */
	dm_pci_write_config32(dev, bar, 0xffffffff);
	dm_pci_read_config32(dev, bar, &resp);

	/* If BAR is not implemented (or invalid) go to the next BAR */
	if (!resp || resp == 0xffffffff)
		return 0;

	/* Check the BAR type and set our address mask */
	if (resp & PCI_BASE_ADDRESS_SPACE) {
		size = resp & PCI_BASE_ADDRESS_IO_MASK;
		size &= ~(size - 1);
		*flagsp = PCIAUTO_BAR_IO;
	} else {
		if ((resp & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
		    PCI_BASE_ADDRESS_MEM_TYPE_64) {
			u32 upper;
			u64 resp64;

			dm_pci_write_config32(dev, bar + 4, 0xffffffff);
			dm_pci_read_config32(dev, bar + 4, &upper);
			resp64 = (u64)upper << 32 | resp;
			size = ~(resp64 & PCI_BASE_ADDRESS_MEM_MASK) + 1;
			*flagsp |= PCIAUTO_BAR_64;
		} else {
			size = (u32)(~(resp & PCI_BASE_ADDRESS_MEM_MASK) + 1);
		}
		if (resp & PCI_BASE_ADDRESS_MEM_PREFETCH)
			*flagsp |= PCIAUTO_BAR_PREFETCH;
	}
	log_debug("%s: BAR %x, %s%s%s, size=%llx%s\n", dev->name, bar,
		  *flagsp & PCIAUTO_BAR_IO ? "I/O" : "Mem",
		  *flagsp & PCIAUTO_BAR_PREFETCH ? " prefetch" : "",
		  *flagsp & PCIAUTO_BAR_64 ? " 64" : "",
		  (unsigned long long)size, size ? "" : " (disabled)");

	/*
	 * A disabled device can report a BAR with its type bits set but no
	 * size; there is nothing to allocate for it
	 */
	return size;
}

pci_size_t pciauto_probe_rom(struct udevice *dev, uint rom_addr)
{
	pci_size_t size;
	u32 resp;

	dm_pci_write_config32(dev, rom_addr, 0xfffffffe);
	dm_pci_read_config32(dev, rom_addr, &resp);
	if (!resp)
		return 0;
	size = -(resp & ~1);
	log_debug("%s: ROM, size=%#llx\n", dev->name,
		  (unsigned long long)size);

	return size;
}

void pciauto_write_bar(struct udevice *dev, uint bar, bool is64,
		       pci_addr_t addr)
{
	dm_pci_write_config32(dev, bar, (u32)addr);
	if (is64)
		dm_pci_write_config32(dev, bar + 4, upper_32_bits(addr));
}

void pciauto_finish_device(struct udevice *dev, u16 cmd)
{
	u16 class, cur;

	/* PCI_COMMAND_IO must be set for VGA device */
	dm_pci_read_config16(dev, PCI_CLASS_DEVICE, &class);
	if (class == PCI_CLASS_DISPLAY_VGA)
		cmd |= PCI_COMMAND_IO;

	dm_pci_read_config16(dev, PCI_COMMAND, &cur);
	cur &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	dm_pci_write_config16(dev, PCI_COMMAND, cur | cmd | PCI_COMMAND_MASTER);
	dm_pci_write_config8(dev, PCI_CACHE_LINE_SIZE,
			     CFG_SYS_PCI_CACHE_LINE_SIZE);
	dm_pci_write_config8(dev, PCI_LATENCY_TIMER, 0x80);
}

/* Assign a device's BARs and expansion ROM from the root bus's regions */
static void pciauto_alloc_device(struct udevice *dev)
{
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *hose = dev_get_uclass_priv(ctlr);
	struct pci_region *mem = hose->pci_mem;
	struct pci_region *prefetch = hose->pci_prefetch;
	struct pci_region *io = hose->pci_io;
	pci_size_t bar_size;
	u16 cmdstat = 0;
	int bar;
	int bars_num;
	uint rom_addr;
	pci_addr_t bar_value;
	struct pci_region *bar_res = NULL;
	bool found_mem64;

	bars_num = pciauto_bar_count(dev, &rom_addr);

	for (bar = PCI_BASE_ADDRESS_0;
	     bar < PCI_BASE_ADDRESS_0 + (bars_num * 4); bar += 4) {
		uint flags;
		int ret = 0;

		bar_size = pciauto_probe_bar(dev, bar, &flags);
		found_mem64 = flags & PCIAUTO_BAR_64;

		/* If the BAR is not implemented or is disabled, skip it */
		if (!bar_size) {
			if (found_mem64)
				bar += 4;
			continue;
		}

		if (flags & PCIAUTO_BAR_IO)
			bar_res = io;
		else if (prefetch && (flags & PCIAUTO_BAR_PREFETCH) &&
			 (found_mem64 || prefetch->bus_lower < 0x100000000ULL))
			bar_res = prefetch;
		else
			bar_res = mem;

		ret = pciauto_region_allocate(bar_res, bar_size,
					      &bar_value, found_mem64);
		if (ret)
			printf("PCI: Failed autoconfig bar %x\n", bar);

		if (!ret)
			pciauto_write_bar(dev, bar, found_mem64, bar_value);
		if (found_mem64)
			bar += 4;

		cmdstat |= (flags & PCIAUTO_BAR_IO) ?
			PCI_COMMAND_IO : PCI_COMMAND_MEMORY;
	}

	/* Configure the expansion ROM address */
	if (rom_addr) {
		bar_size = pciauto_probe_rom(dev, rom_addr);
		if (bar_size) {
			if (!pciauto_region_allocate(mem, bar_size, &bar_value,
						     false))
				dm_pci_write_config32(dev, rom_addr, bar_value);
			cmdstat |= PCI_COMMAND_MEMORY;
		}
	}

	pciauto_finish_device(dev, cmdstat);
}

/*
 * Check if the link of a downstream PCIe port operates correctly.
 *
 * For that check if the optional Data Link Layer Link Active status gets
 * on within a 200ms period or failing that wait until the completion of
 * that period and check if link training has shown the completed status
 * continuously throughout the second half of that period.
 *
 * Observation with the ASMedia ASM2824 Gen 3 switch indicates it takes
 * 11-44ms to indicate the Data Link Layer Link Active status at 2.5GT/s,
 * though it may take a couple of link training iterations.
 */
static bool pciauto_exp_link_stable(struct udevice *dev, int pcie_off)
{
	u64 loops = 0, trcount = 0, ntrcount = 0, flips = 0;
	bool dllla, lnktr, plnktr;
	u16 exp_lnksta;
	pci_dev_t bdf;
	u64 end;

	dm_pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA, &exp_lnksta);
	plnktr = !!(exp_lnksta & PCI_EXP_LNKSTA_LT);

	end = get_ticks() + usec_to_tick(200000);
	do {
		dm_pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA,
				     &exp_lnksta);
		dllla = !!(exp_lnksta & PCI_EXP_LNKSTA_DLLLA);
		lnktr = !!(exp_lnksta & PCI_EXP_LNKSTA_LT);

		flips += plnktr ^ lnktr;
		if (lnktr) {
			ntrcount = 0;
			trcount++;
		} else {
			ntrcount++;
		}
		loops++;

		plnktr = lnktr;
	} while (!dllla && get_ticks() < end);

	bdf = dm_pci_get_bdf(dev);
	debug("PCI Autoconfig: %02x.%02x.%02x: Fixup link: DL active: %u; "
	      "%3llu flips, %6llu loops of which %6llu while training, "
	      "final %6llu stable\n",
	      PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf),
	      (unsigned int)dllla,
	      (unsigned long long)flips, (unsigned long long)loops,
	      (unsigned long long)trcount, (unsigned long long)ntrcount);

	return dllla || ntrcount >= loops / 2;
}

/*
 * Retrain the link of a downstream PCIe port by hand if necessary.
 *
 * This is needed at least where a downstream port of the ASMedia ASM2824
 * Gen 3 switch is wired to the upstream port of the Pericom PI7C9X2G304
 * Gen 2 switch, and observed with the Delock Riser Card PCI Express x1 >
 * 2 x PCIe x1 device, P/N 41433, plugged into the SiFive HiFive Unmatched
 * board.
 *
 * In such a configuration the switches are supposed to negotiate the link
 * speed of preferably 5.0GT/s, falling back to 2.5GT/s.  However the link
 * continues switching between the two speeds indefinitely and the data
 * link layer never reaches the active state, with link training reported
 * repeatedly active ~84% of the time.  Forcing the target link speed to
 * 2.5GT/s with the upstream ASM2824 device makes the two switches talk to
 * each other correctly however.  And more interestingly retraining with a
 * higher target link speed afterwards lets the two successfully negotiate
 * 5.0GT/s.
 *
 * As this can potentially happen with any device and is cheap in the case
 * of correctly operating hardware, let's do it for all downstream ports,
 * for root complexes, PCIe switches and PCI/PCI-X to PCIe bridges.
 *
 * First check if automatic link training may have failed to complete, as
 * indicated by the optional Data Link Layer Link Active status being off
 * and the Link Bandwidth Management Status indicating that hardware has
 * changed the link speed or width in an attempt to correct unreliable
 * link operation.  If this is the case, then check if the link operates
 * correctly by seeing whether it is being trained excessively.  If it is,
 * then conclude the link is broken.
 *
 * In that case restrict the speed to 2.5GT/s, observing that the Target
 * Link Speed field is sticky and therefore the link will stay restricted
 * even after a device reset is later made by an OS that is unaware of the
 * problem.  With the speed restricted request that the link be retrained
 * and check again if the link operates correctly.  If not, then set the
 * Target Link Speed back to the original value.
 *
 * This requires the presence of the Link Control 2 register, so make sure
 * the PCI Express Capability Version is at least 2.  Also don't try, for
 * obvious reasons, to limit the speed if 2.5GT/s is the only link speed
 * supported.
 */
static void pciauto_exp_fixup_link(struct udevice *dev, int pcie_off)
{
	u16 exp_lnksta, exp_lnkctl, exp_lnkctl2;
	u16 exp_flags, exp_type, exp_version;
	u32 exp_lnkcap;
	pci_dev_t bdf;

	dm_pci_read_config16(dev, pcie_off + PCI_EXP_FLAGS, &exp_flags);
	exp_version = exp_flags & PCI_EXP_FLAGS_VERS;
	if (exp_version < 2)
		return;

	exp_type = (exp_flags & PCI_EXP_FLAGS_TYPE) >> 4;
	switch (exp_type) {
	case PCI_EXP_TYPE_ROOT_PORT:
	case PCI_EXP_TYPE_DOWNSTREAM:
	case PCI_EXP_TYPE_PCIE_BRIDGE:
		break;
	default:
		return;
	}

	dm_pci_read_config32(dev, pcie_off + PCI_EXP_LNKCAP, &exp_lnkcap);
	if ((exp_lnkcap & PCI_EXP_LNKCAP_SLS) <= PCI_EXP_LNKCAP_SLS_2_5GB)
		return;

	dm_pci_read_config16(dev, pcie_off + PCI_EXP_LNKSTA, &exp_lnksta);
	if ((exp_lnksta & (PCI_EXP_LNKSTA_LBMS | PCI_EXP_LNKSTA_DLLLA)) !=
	    PCI_EXP_LNKSTA_LBMS)
		return;

	if (pciauto_exp_link_stable(dev, pcie_off))
		return;

	bdf = dm_pci_get_bdf(dev);
	printf("PCI Autoconfig: %02x.%02x.%02x: "
	       "Downstream link non-functional\n",
	       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));
	printf("PCI Autoconfig: %02x.%02x.%02x: "
	       "Retrying with speed restricted to 2.5GT/s...\n",
	       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));

	dm_pci_read_config16(dev, pcie_off + PCI_EXP_LNKCTL, &exp_lnkctl);
	dm_pci_read_config16(dev, pcie_off + PCI_EXP_LNKCTL2, &exp_lnkctl2);

	dm_pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL2,
			      (exp_lnkctl2 & ~PCI_EXP_LNKCTL2_TLS) |
			      PCI_EXP_LNKCTL2_TLS_2_5GT);
	dm_pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL,
			      exp_lnkctl | PCI_EXP_LNKCTL_RL);

	if (pciauto_exp_link_stable(dev, pcie_off)) {
		printf("PCI Autoconfig: %02x.%02x.%02x: Succeeded!\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));
	} else {
		printf("PCI Autoconfig: %02x.%02x.%02x: Failed!\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf));

		dm_pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL2,
				      exp_lnkctl2);
		dm_pci_write_config16(dev, pcie_off + PCI_EXP_LNKCTL,
				      exp_lnkctl | PCI_EXP_LNKCTL_RL);
	}
}

/* Start a bridge's windows where the regions' allocation has reached */
static void pciauto_open_windows(struct udevice *dev,
				 struct pci_controller *hose)
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

/* End a bridge's windows where the regions' allocation has reached */
static void pciauto_close_windows(struct udevice *dev,
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

void pciauto_prescan_setup_bridge(struct udevice *dev, int sub_bus)
{
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *ctlr_hose = dev_get_uclass_priv(ctlr);
	int pcie_off;

	/* Configure bus number registers */
	dm_pci_write_config8(dev, PCI_PRIMARY_BUS,
			     PCI_BUS(dm_pci_get_bdf(dev)) - dev_seq(ctlr) +
			     ctlr_hose->bus_base);
	dm_pci_write_config8(dev, PCI_SECONDARY_BUS,
			     sub_bus - dev_seq(ctlr) + ctlr_hose->bus_base);
	dm_pci_write_config8(dev, PCI_SUBORDINATE_BUS, 0xff);

	pciauto_open_windows(dev, ctlr_hose);

	/* For PCIe devices see if we need to retrain the link by hand */
	pcie_off = dm_pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (pcie_off)
		pciauto_exp_fixup_link(dev, pcie_off);
}

void pciauto_postscan_setup_bridge(struct udevice *dev, int sub_bus)
{
	struct udevice *ctlr = pci_get_controller(dev);
	struct pci_controller *ctlr_hose = dev_get_uclass_priv(ctlr);

	/* Configure bus number registers */
	dm_pci_write_config8(dev, PCI_SUBORDINATE_BUS,
			     sub_bus - dev_seq(ctlr) + ctlr_hose->bus_base);

	pciauto_close_windows(dev, ctlr_hose);
}

/*
 * HJF: Changed this to return int. I think this is required
 * to get the correct result when scanning bridges
 */
int pciauto_config_device(struct udevice *dev)
{
	unsigned int sub_bus = PCI_BUS(dm_pci_get_bdf(dev));
	unsigned short class;
	int ret;

	dm_pci_read_config16(dev, PCI_CLASS_DEVICE, &class);
	if (CONFIG_IS_ENABLED(LOG)) {
		u32 vendev;

		dm_pci_read_config32(dev, PCI_VENDOR_ID, &vendev);
		log_debug("dev %s class %x vendev %x\n", dev->name, class,
			  vendev);
	}

	switch (class) {
	case PCI_CLASS_BRIDGE_PCI:
		log_debug("PCI Autoconfig: Found P2P bridge, device %d\n",
			  PCI_DEV(dm_pci_get_bdf(dev)));

		pciauto_alloc_device(dev);

		ret = dm_pci_hose_probe_bus(dev);
		log_debug("hose_probe_bus: ret=%d\n", ret);
		if (ret < 0)
			return log_msg_ret("probe", ret);
		sub_bus = ret;
		break;

	case PCI_CLASS_BRIDGE_CARDBUS:
		/*
		 * just do a minimal setup of the bridge,
		 * let the OS take care of the rest
		 */
		pciauto_alloc_device(dev);

		debug("PCI Autoconfig: Found P2CardBus bridge, device %d\n",
		      PCI_DEV(dm_pci_get_bdf(dev)));

		break;

#if defined(CONFIG_PCIAUTO_SKIP_HOST_BRIDGE)
	case PCI_CLASS_BRIDGE_OTHER:
		debug("PCI Autoconfig: Skipping bridge device %d\n",
		      PCI_DEV(dm_pci_get_bdf(dev)));
		break;
#endif

	default:
		pciauto_alloc_device(dev);
		break;
	}

	return sub_bus;
}
