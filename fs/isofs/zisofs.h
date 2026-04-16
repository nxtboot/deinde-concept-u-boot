/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ZISOFS stub header for U-Boot
 * Transparent decompression not supported in U-Boot.
 */
#ifndef _ISOFS_ZISOFS_H
#define _ISOFS_ZISOFS_H

static inline int zisofs_init(void) { return 0; }
static inline void zisofs_cleanup(void) { }

#endif
