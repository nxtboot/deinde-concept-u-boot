/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Internal PCI functions, not exported outside drivers/pci
 *
 * Copyright (c) 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __pci_internal_h
#define __pci_internal_h

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
 * It sets up the bus number and memory range registers. Once the scan is
 * completed, pciauto_postscan_setup_bridge() should be called.
 *
 * @dev:	Bridge device to be scanned
 * @sub_bus:	Bus number of the 'other side' of the bridge
 */
void pciauto_prescan_setup_bridge(struct udevice *dev, int sub_bus);

/**
 * pciauto_postscan_setup_bridge() - Finish set up of a bridge after scanning
 *
 * This should be called after a bus scan is complete. It adjusts the memory
 * ranges to fit with the devices actually found on the other side (downstream)
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
