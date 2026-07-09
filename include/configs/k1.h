/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2024, Kongyang Liu <seashell11234455@gmail.com>
 * Copyright (C) 2025-2026, RISCstar Ltd.
 *
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define CFG_SYS_SDRAM_BASE	    0x0

#define CFG_SYS_NS16550_CLK	    14700000
#define CFG_SYS_NS16550_IER	    0x40 /* UART Unit Enable */

#define RISCV_MMODE_TIMER_FREQ	    24000000
#define RISCV_SMODE_TIMER_FREQ	    24000000

/* Load U-Boot proper (u-boot.itb) into RAM over USB DFU in SPL */
#define CFG_EXTRA_ENV_SETTINGS \
	"dfu_alt_info_ram=u-boot.itb ram 0x08000000 0x800000\0"

#endif /* __CONFIG_H */
