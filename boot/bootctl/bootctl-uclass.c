// SPDX-License-Identifier: GPL-2.0+
/*
 * Uclass implementation for boot schema
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#define LOG_CATEGORY	UCLASS_BOOTCTL

#include <abuf.h>
#include <bootctl.h>
#include <dm.h>
#include <expo.h>
#include <bootctl/ui.h>

UCLASS_DRIVER(bootctrl) = {
	.id		= UCLASS_BOOTCTL,
	.name		= "bootctrl",
#if CONFIG_IS_ENABLED(OF_REAL)
	.post_bind	= dm_scan_fdt_dev,
#endif
	.per_device_plat_auto	= sizeof(struct bootctl_uc_plat),
};

UCLASS_DRIVER(bootctrl_measure) = {
	.id		= UCLASS_BOOTCTL_MEASURE,
	.name		= "bootctrl_measure",
	.per_device_plat_auto	= sizeof(struct bootctl_uc_plat),
};

UCLASS_DRIVER(bootctrl_oslist) = {
	.id		= UCLASS_BOOTCTL_OSLIST,
	.name		= "bootctrl_oslist",
	.per_device_plat_auto	= sizeof(struct bootctl_uc_plat),
};

UCLASS_DRIVER(bootctrl_state) = {
	.id		= UCLASS_BOOTCTL_STATE,
	.name		= "bootctrl_state",
	.per_device_plat_auto	= sizeof(struct bootctl_uc_plat),
};

static int bootctl_ui_pre_remove(struct udevice *dev)
{
	struct bc_ui_priv *upriv = dev_get_uclass_priv(dev);

	if (upriv->expo)
		expo_destroy(upriv->expo);
	abuf_uninit(&upriv->autoboot_template);

	return 0;
}

UCLASS_DRIVER(bootctrl_ui) = {
	.id		= UCLASS_BOOTCTL_UI,
	.name		= "bootctrl_ui",
	.pre_remove	= bootctl_ui_pre_remove,
	.per_device_plat_auto	= sizeof(struct bootctl_uc_plat),
	.per_device_auto	= sizeof(struct bc_ui_priv),
};
