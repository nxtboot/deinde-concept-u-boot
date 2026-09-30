/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Layout of the AMD Turin AP start-up routine, shared with ap_start.S
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_TURIN_AP_H
#define _ASM_ARCH_TURIN_AP_H

/* Where the routine goes in the APs' 64KB reset segment */
#define TURIN_AP_CODE_OFFSET	0xf000
#define TURIN_AP_SEG_SIZE	0x10000
#define TURIN_AP_VECTOR_OFFSET	0xfff0

/* TW_CFG's CombineCr0Cd bit: the high word of the MSR holds bits 63:32 */
#define MSR_AMD64_TW_CFG	0xc0011023
#define TW_CFG_COMBINE_CR0_CD_BIT	49
#define TW_CFG_COMBINE_CR0_CD_HI	(1 << (TW_CFG_COMBINE_CR0_CD_BIT - 32))

#define TURIN_AP_MAX_IDS	256
#define TURIN_AP_MAX_MSRS	48

#ifndef __ASSEMBLY__
/* The routine, its parameter block and its end, in ap_start.S */
extern char turin_ap_start[], turin_ap_params[], turin_ap_end[];
#endif

#endif
