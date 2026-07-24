// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Reset helpers for x86 and Intel PCH-based platforms
 */

#include <linux/bitops.h>
#include <linux/types.h>
#include <asm/cpu_common.h>
#include <asm/io.h>
#include <asm/processor.h>

/* The PMC's enhanced-test register, at a fixed offset in the PWRM space */
#define PMC_ETR			0x1048
#define ETR_CF9_GLOBAL_RST	BIT(20)

void x86_cf9_reset(uint code)
{
	/*
	 * The reset triggers on the rising edge of RST_CPU, so clear it
	 * first in case it is already set from an earlier request
	 */
	outb(0, IO_PORT_RESET);
	outb(code, IO_PORT_RESET);
}

void intel_global_reset(ulong pwrmbase)
{
	/* Make the following CF9 reset a global one (also resets the CSE) */
	setbits_le32(pwrmbase + PMC_ETR, ETR_CF9_GLOBAL_RST);

	/* Bit 1 (SYS_RST) selects a hard reset; bit 3 (FULL_RST) a cold one */
	x86_cf9_reset(FULL_RST | SYS_RST | RST_CPU);

	for (;;)
		cpu_hlt();
}

void intel_host_reset(ulong pwrmbase, bool cold)
{
	/* A host reset leaves the CSE running, so clear the global-reset bit */
	clrbits_le32(pwrmbase + PMC_ETR, ETR_CF9_GLOBAL_RST);

	/* SYS_RST selects a hard reset; FULL_RST additionally makes it cold */
	x86_cf9_reset((cold ? FULL_RST : 0) | SYS_RST | RST_CPU);

	for (;;)
		cpu_hlt();
}
