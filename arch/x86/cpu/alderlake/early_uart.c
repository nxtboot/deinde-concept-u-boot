// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Early console setup for Alder Lake-P: route the two UART0 pads to the
 * LPSS UART, give the controller a temporary MMIO base and start its
 * clock, so that the debug UART works long before PCI enumeration.
 * Register details are from the coreboot Alder Lake support.
 */

#include <dm.h>
#include <asm/io.h>
#include <asm/pci.h>
#include <asm/arch/uart.h>

/* Sideband port ID of GPIO community 1 (GPP_S, H, D) */
#define PID_GPIOCOM1		0x6d

/* Offset of the pad-configuration registers within a GPIO community */
#define PAD_CFG_BASE		0x700

/*
 * Each pad has four config registers (16 bytes). Community 1 starts at
 * GPP_S0, which has 8 pads, so GPP_H0 is pad 8 within the community
 */
#define GPP_H_CFG_INDEX		8
#define PAD_CFG_SIZE		16

/* Pad-configuration DW0 fields */
#define PAD_RESET_DEEP		(1 << 30)
#define PAD_MODE_NF2		(2 << 10)

/* UART0 pads: GPP_H10 (RXD) and GPP_H11 (TXD), both native function 2 */
#define UART0_RXD_PAD		(GPP_H_CFG_INDEX + 10)
#define UART0_TXD_PAD		(GPP_H_CFG_INDEX + 11)

/* PCI functions involved, all on bus 0 */
#define PCH_DEV_P2SB		PCI_BDF(0, 0x1f, 1)
#define PCH_DEV_UART0		PCI_BDF(0, 0x1e, 0)

static void *pad_cfg_ptr(uint cfg_index)
{
	return (void *)(CONFIG_PCR_BASE_ADDRESS + (PID_GPIOCOM1 << 16) +
			PAD_CFG_BASE + cfg_index * PAD_CFG_SIZE);
}

static void configure_uart_pad(uint cfg_index)
{
	void *cfg = pad_cfg_ptr(cfg_index);

	writel(PAD_RESET_DEEP | PAD_MODE_NF2, cfg);	/* DW0 */
	writel(0, cfg + 4);				/* DW1: no pull */
}

void adl_early_uart_init(void)
{
	/*
	 * Set up the P2SB sideband BAR so that the GPIO communities can be
	 * reached. The BAR is normally locked down later, by FSP-S.
	 */
	pci_x86_write_config(PCH_DEV_P2SB, PCI_BASE_ADDRESS_0,
			     CONFIG_PCR_BASE_ADDRESS, PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_P2SB, PCI_COMMAND,
			     PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			     PCI_SIZE_16);

	configure_uart_pad(UART0_RXD_PAD);
	configure_uart_pad(UART0_TXD_PAD);

	/*
	 * Give the UART a temporary BAR and start its clock; the LPSS UART
	 * is the same as Apollo Lake's, so its init code is shared
	 */
	apl_uart_init(PCH_DEV_UART0, CONFIG_DEBUG_UART_BASE);
}

void board_debug_uart_init(void)
{
	adl_early_uart_init();
}
