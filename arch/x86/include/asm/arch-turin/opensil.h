/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Glue between U-Boot and AMD's openSIL library
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __ASM_ARCH_OPENSIL_H
#define __ASM_ARCH_OPENSIL_H

/**
 * turin_opensil_init() - Set up openSIL and run its first timepoint
 *
 * This sends openSIL's messages to U-Boot's log, gives openSIL its memory,
 * fills in its input blocks from the devicetree and runs timepoint 1, which
 * sets up the data fabric, the SMU, the root complexes and their links, the
 * CPU complexes, starting the other threads, and the FCH. It must run before
 * PCI enumeration
 *
 * @ucode: Microcode patch which the boot processor has, for the other
 *	threads, or NULL if none
 * Return: 0 if OK, -ENOSYS if openSIL is not built in, other -ve on error
 */
int turin_opensil_init(const void *ucode);

/**
 * turin_opensil_tp2() - Run openSIL's second timepoint
 *
 * This must run once PCI resources have been assigned
 */
void turin_opensil_tp2(void);

/**
 * turin_opensil_tp3() - Run openSIL's third timepoint
 *
 * This must run once U-Boot's own set-up is done, before the OS starts
 */
void turin_opensil_tp3(void);

#endif
