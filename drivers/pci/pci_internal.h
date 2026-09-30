/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Internal PCI functions, not exported outside drivers/pci
 *
 * Copyright (c) 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __pci_internal_h
#define __pci_internal_h

/* What pciauto_probe_bar() found: a BAR's type and width */
#define PCIAUTO_BAR_IO		BIT(0)	/* I/O space, else memory */
#define PCIAUTO_BAR_PREFETCH	BIT(1)	/* prefetchable memory */
#define PCIAUTO_BAR_64		BIT(2)	/* 64-bit memory BAR, using two registers */

/**
 * pciauto_bar_count() - Find how many BARs a device has
 *
 * @dev: Device to check
 * @rom_addrp: Returns the config offset of the expansion-ROM register, or 0
 *	if the device has none
 * Return: number of BARs: 6 for a normal header, 2 for a bridge, else 0
 */
int pciauto_bar_count(struct udevice *dev, uint *rom_addrp);

/**
 * pciauto_probe_bar() - Find the size and type of a BAR
 *
 * This writes all-ones to the BAR, and to the next register too for a 64-bit
 * BAR, and reads back what sticks, so the caller must write the address (or
 * zero) afterwards and must skip the next register if PCIAUTO_BAR_64 is set.
 *
 * @dev: Device to probe
 * @bar: Config offset of the BAR
 * @flagsp: Returns PCIAUTO_BAR_... flags describing the BAR
 * Return: size in bytes, or 0 if the BAR is not implemented or disabled
 */
pci_size_t pciauto_probe_bar(struct udevice *dev, uint bar, uint *flagsp);

/**
 * pciauto_probe_rom() - Find the size of a device's expansion ROM
 *
 * @dev: Device to probe
 * @rom_addr: Config offset of the ROM register
 * Return: size in bytes, or 0 if the device has no ROM
 */
pci_size_t pciauto_probe_rom(struct udevice *dev, uint rom_addr);

/**
 * pciauto_write_bar() - Write an address to a BAR
 *
 * @dev: Device to write to
 * @bar: Config offset of the BAR
 * @is64: true for a 64-bit BAR, whose upper half is written too
 * @addr: Address to write
 */
void pciauto_write_bar(struct udevice *dev, uint bar, bool is64,
		       pci_addr_t addr);

/**
 * pciauto_finish_device() - Enable a device once its resources are set
 *
 * This enables the decoding the device needs, and bus mastering, and sets
 * its cache-line size and latency timer.
 *
 * @dev: Device to enable
 * @cmd: PCI_COMMAND_IO and/or PCI_COMMAND_MEMORY, for the resources it has
 */
void pciauto_finish_device(struct udevice *dev, u16 cmd);

/**
 * pciauto_prescan_setup_bridge() - Set up a bridge for scanning
 *
 * This gets a bridge ready so that its downstream devices can be scanned.
 * It sets up the bus number registers and retrains the link. Once the scan
 * is completed, pciauto_postscan_setup_bridge() should be called.
 *
 * @dev:	Bridge device to be scanned
 * @sub_bus:	Bus number of the 'other side' of the bridge
 */
void pciauto_prescan_setup_bridge(struct udevice *dev, int sub_bus);

/**
 * pciauto_postscan_setup_bridge() - Finish set up of a bridge after scanning
 *
 * This should be called after a bus scan is complete. It sets the bridge's
 * subordinate bus number to the last bus found on the other side (downstream)
 * of the bridge.
 *
 * @dev:	Bridge device that was scanned
 * @sub_bus:	Bus number of the 'other side' of the bridge
 */
void pciauto_postscan_setup_bridge(struct udevice *dev, int sub_bus);

/**
 * pciauto_config_device() - Configure a PCI device ready for use
 *
 * If the device is a bridge, downstream devices will be probed.
 *
 * @dev:	Device to configure
 * Return: the maximum PCI bus number found by this device. If there are no
 * bridges, this just returns the device's bus number. If the device is a
 * bridge then it will return a larger number, depending on the devices on
 * that bridge. On error, returns a -ve error number.
 */
int pciauto_config_device(struct udevice *dev);

/*
 * The allocator, in pci_auto_simple.c, which assigns resources as the devices
 * are found
 */

/**
 * pciauto_alloc_device() - Assign a device's BARs and expansion ROM
 *
 * The addresses come from the root bus's regions, in the order the devices
 * are found. The device's command register is set up too.
 *
 * @dev: Device to configure
 */
void pciauto_alloc_device(struct udevice *dev);

/**
 * pciauto_open_windows() - Start a bridge's windows before scanning its bus
 *
 * Each window starts where its region's allocation has reached, so that the
 * devices behind the bridge are assigned inside it.
 *
 * @dev: Bridge about to be scanned
 * @hose: Controller of the bridge's root bus, which holds the regions
 */
void pciauto_open_windows(struct udevice *dev, struct pci_controller *hose);

/**
 * pciauto_close_windows() - End a bridge's windows after scanning its bus
 *
 * Each window ends where its region's allocation has reached.
 *
 * @dev: Bridge which was scanned
 * @hose: Controller of the bridge's root bus, which holds the regions
 */
void pciauto_close_windows(struct udevice *dev,
			   struct pci_controller *hose);

/**
 * pci_get_bus() - Get a pointer to a bus, given its number
 *
 * This looks up a PCI bus based on its bus number. The bus is probed if
 * necessary.
 *
 * @busnum:	PCI bus number to look up
 * @busp:	Returns PCI bus on success
 * Return: 0 on success, or -ve error
 */
int pci_get_bus(int busnum, struct udevice **busp);

#endif
