/* SPDX-License-Identifier: Intel */
/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * FSP-T Updateable Product Data (UPD) for Alder Lake. The layout must
 * match the FsptUpd.h shipped with the FSP binary in use.
 */

#ifndef _ASM_ARCH_FSP_T_UPD_H
#define _ASM_ARCH_FSP_T_UPD_H

#include <linux/types.h>
#include <asm/fsp2/fsp_api.h>
#include <asm/arch/fsp/fsp_configs.h>

#define FSPT_UPD_TERMINATOR	0x55aa

/**
 * struct fspt_arch_upd - architectural settings for FSP-T
 *
 * @revision: Revision of this structure
 * @length: Size of this structure in bytes
 * @debug_handler: Optional debug-message handler (0 for none)
 */
struct fspt_arch_upd {
	u8 revision;
	u8 reserved[3];
	u32 length;
	u32 debug_handler;
	u8 reserved1[20];
} __packed;

/**
 * struct fspt_core_upd - settings which TempRamInit needs
 *
 * @microcode_region_base: Address of the microcode region, or 0 if the
 *	microcode is loaded another way (on Alder Lake the FIT does this
 *	before the CPU executes the reset vector)
 * @microcode_region_size: Size of the microcode region, or 0
 * @code_region_base: Base address of the region to cache for
 *	execute-in-place code, i.e. the memory-mapped part of the SPI flash
 * @code_region_size: Size of that region
 */
struct fspt_core_upd {
	u32 microcode_region_base;
	u32 microcode_region_size;
	u32 code_region_base;
	u32 code_region_size;
	u8 reserved[16];
} __packed;

/**
 * struct fspt_config - FSP-T configuration
 *
 * Most of this is the FSP's debug-UART configuration, which is not used
 * since U-Boot sets up its own console. The PCI-express (ECAM) fields
 * matter: FSP-T programs the host bridge's PCIEXBAR from them (see
 * SecHostBridgeLib in the FSP source) and the rest of the FSP does all
 * its PCI configuration access through that window, so a zero base makes
 * every FSP config access read garbage.
 *
 * @pci_express_base: Base address for the ECAM (PCI mmconfig) window
 * @pci_express_region_length: Size of the ECAM window
 */
struct fspt_config {
	u8 reserved0[8];
	u64 pci_express_base;
	u32 pci_express_region_length;
	u8 reserved1[0x64];
} __packed;

/**
 * struct fspt_upd - complete FSP-T UPD region, passed to TempRamInit
 *
 * The FSP's own copy of this (in its configuration region) has a zero
 * code region, so a filled-in copy must be supplied or TempRamInit fails
 *
 * @config: FSP-T configuration; the ECAM fields must be set
 * @terminator: Must be FSPT_UPD_TERMINATOR
 */
struct fspt_upd {
	struct fsp_upd_header header;
	struct fspt_arch_upd arch;
	struct fspt_core_upd core;
	struct fspt_config config;
	u8 reserved[6];
	u16 terminator;
} __packed;

/* The FSP-T configuration, which car.S passes to TempRamInit */
extern const struct fspt_upd temp_ram_init_params;

#endif
