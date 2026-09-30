// SPDX-License-Identifier: GPL-2.0+
/*
 * Compatibility functions for pre-driver-model code
 *
 * Copyright (C) 2014 Google, Inc
 */
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <pci.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include "pci_internal.h"

pci_dev_t pci_find_devices(struct pci_device_id *ids, int index)
{
	struct udevice *dev;

	if (pci_find_device_id(ids, index, &dev))
		return -1;
	return dm_pci_get_bdf(dev);
}

struct pci_controller *pci_bus_to_hose(int busnum)
{
	struct udevice *bus;
	int ret;

	ret = pci_get_bus(busnum, &bus);
	if (ret) {
		debug("%s: Cannot get bus %d: ret=%d\n", __func__, busnum, ret);
		return NULL;
	}

	return dev_get_uclass_priv(pci_get_controller(bus));
}
