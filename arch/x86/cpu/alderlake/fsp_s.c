// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Silicon init (FSP-S) support. This is not implemented yet: for now the
 * hooks which the FSP-2 code requires are provided so that memory init can
 * be brought up first.
 */

#include <log.h>
#include <asm/fsp2/fsp_internal.h>

int arch_fsps_preinit(void)
{
	return 0;
}

int arch_fsp_init_r(void)
{
	/* TODO(sjg@chromium.org): Run FSP-S silicon init here */
	return 0;
}
