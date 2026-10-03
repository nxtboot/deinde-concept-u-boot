/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2012-2012 Henrik Nordstrom <henrik@henriknordstrom.net>
 *
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * Configuration settings for the Allwinner sunxi series of boards.
 */

#ifndef _SUNXI_COMMON_CONFIG_H
#define _SUNXI_COMMON_CONFIG_H

#include <linux/stringify.h>

/****************************************************************************
 *                  base addresses for the SPL UART driver                  *
 ****************************************************************************/
#ifdef CONFIG_MACH_SUNIV
/* suniv doesn't have apb2 and uart is connected to apb1 */
#define CFG_SYS_NS16550_CLK		100000000
#else
#define CFG_SYS_NS16550_CLK		24000000
#endif
#if !IS_ENABLED(CONFIG_DM_SERIAL)
#include <asm/arch/serial.h>
# define CFG_SYS_NS16550_COM1		SUNXI_UART0_BASE
# define CFG_SYS_NS16550_COM2		SUNXI_UART1_BASE
# define CFG_SYS_NS16550_COM3		SUNXI_UART2_BASE
# define CFG_SYS_NS16550_COM4		SUNXI_UART3_BASE
# define CFG_SYS_NS16550_COM5		SUNXI_R_UART_BASE
#endif

/****************************************************************************
 *                             DRAM base address                            *
 ****************************************************************************/
/*
 * The DRAM Base differs between some models. We cannot use macros for the
 * CONFIG_FOO defines which contain the DRAM base address since they end
 * up unexpanded in include/autoconf.mk .
 *
 * So we have to have this #ifdef #else #endif block for these.
 */
#ifdef CONFIG_MACH_SUN9I
#define SDRAM_OFFSET(x) 0x2##x
#define CFG_SYS_SDRAM_BASE		0x20000000
#elif defined(CONFIG_MACH_SUNIV)
#define SDRAM_OFFSET(x) 0x8##x
#define CFG_SYS_SDRAM_BASE		0x80000000
#else
#define SDRAM_OFFSET(x) 0x4##x
#define CFG_SYS_SDRAM_BASE		0x40000000
/* V3s do not have enough memory to place code at 0x4a000000 */
#endif

#define CFG_SYS_INIT_RAM_ADDR	CONFIG_SUNXI_SRAM_ADDRESS
/* FIXME: this may be larger on some SoCs */
#define CFG_SYS_INIT_RAM_SIZE	0x8000 /* 32 KiB */

#define PHYS_SDRAM_0			CFG_SYS_SDRAM_BASE
#define PHYS_SDRAM_0_SIZE		0x80000000 /* 2 GiB */

#endif /* _SUNXI_COMMON_CONFIG_H */
