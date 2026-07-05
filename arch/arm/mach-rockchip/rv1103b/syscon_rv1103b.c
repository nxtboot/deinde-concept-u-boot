// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <dm.h>
#include <syscon.h>
#include <asm/arch-rockchip/clock.h>

static const struct udevice_id rv1103b_syscon_ids[] = {
	{ .compatible = "rockchip,rv1103b-pmu-grf", .data = ROCKCHIP_SYSCON_PMUGRF },
	{ }
};

U_BOOT_DRIVER(syscon_rv1103b) = {
	.name = "rv1103b_syscon",
	.id = UCLASS_SYSCON,
	.of_match = rv1103b_syscon_ids,
};
