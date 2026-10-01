/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_GLOBAL_NVS_H
#define _ASM_ARCH_GLOBAL_NVS_H

/* The DSDT uses no global NVS; this keeps the generic x86 ACPI code happy */
struct __packed acpi_global_nvs {
	u8 rsvd[16];
};

#endif
