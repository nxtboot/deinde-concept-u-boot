/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2016 Google, Inc
 */

#ifndef __asm_spl_h
#define __asm_spl_h

struct spl_image_info;

enum {
	BOOT_DEVICE_MMC1,
	BOOT_DEVICE_MMC2,
	BOOT_DEVICE_MMC2_2,
	BOOT_DEVICE_BOARD,
	BOOT_DEVICE_VBE,
	BOOT_DEVICE_CPGMAC,
	BOOT_DEVICE_NOR,
	BOOT_DEVICE_SPI,
	BOOT_DEVICE_NAND,
	BOOT_DEVICE_UPL,
};

/**
 * sandbox_find_next_phase() - Find the next phase of U-Boot
 *
 * This function is intended to be called from within sandbox SPL. It uses
 * a few rules to find the filename of the next U-Boot phase. See also
 * os_find_u_boot().
 *
 * @fname:	place to put full path to U-Boot
 * @maxlen:	maximum size of @fname
 * @use_img:	select the 'u-boot.img' file instead of the 'u-boot' ELF file
 */
int sandbox_find_next_phase(char *fname, int maxlen, bool use_img);

/**
 * sandbox_spl_load_fit() - Load the next phase from a FIT
 *
 * Loads a FIT containing the next phase and sets it up for booting
 *
 * @fname: Returns filename loaded
 * @maxlen: Maximum length for @fname including \0
 * @image: Place to put SPL-image information
 * Return: 0 if OK, -ve on error
 */
int sandbox_spl_load_fit(char *fname, int maxlen, struct spl_image_info *image);

/*
 * Address at which sandbox_spl_load_fit_full() stages the FIT it reads.
 *
 * This must sit above everything the FIT's own images are loaded to, since
 * loading an image would otherwise overwrite the FIT still being read from.
 * The U-Boot image goes to CONFIG_TEXT_BASE (400000) and is several MB, so
 * CONFIG_SYS_LOAD_ADDR (0 for sandbox_spl) will not do. It must also stay
 * clear of the nvmxip devices at 8000000. See the memory map in
 * doc/arch/sandbox/sandbox.rst
 */
#define SANDBOX_SPL_FIT_ADDR	0x4000000

/**
 * sandbox_spl_load_fit_full() - Load the next phase from a FIT with the "full" loader
 *
 * Loads a FIT containing the next phase and sets it up for booting, using the
 * "full" FIT loader
 *
 * @fname: Returns filename loaded
 * @maxlen: Maximum length for @fname including \0
 * @image: Place to put SPL-image information
 * Return: 0 if OK, -ve on error
 */
int sandbox_spl_load_fit_full(char *fname, int maxlen,
			      struct spl_image_info *image);

#endif
