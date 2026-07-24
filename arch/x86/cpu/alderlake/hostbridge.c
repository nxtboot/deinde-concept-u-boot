// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Host bridge (system agent). This exists so that the FSP code can find the
 * northbridge; it does not need to do anything yet, since the FSP sets up
 * the system agent itself.
 */

#include <dm.h>

static const struct udevice_id adl_hostbridge_ids[] = {
	{ .compatible = "intel,alderlake-hostbridge" },
	{ }
};

U_BOOT_DRIVER(intel_alderlake_hostbridge) = {
	.name		= "intel_alderlake_hostbridge",
	.id		= UCLASS_NORTHBRIDGE,
	.of_match	= adl_hostbridge_ids,
};
