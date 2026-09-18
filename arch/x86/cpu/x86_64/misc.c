// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <init.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

int misc_init_r(void)
{
	return 0;
}

#ifndef CONFIG_SYS_COREBOOT
int checkcpu(void)
{
	return 0;
}
#endif
