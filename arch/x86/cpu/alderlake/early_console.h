/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Assembly macros to bring up the UART console before there is a stack,
 * so that boot progress can be seen even if Cache-as-RAM setup fails.
 * These use only PCI-config I/O and MMIO writes, with no memory access
 * and no calls, so they can run at the very start of TPL.
 *
 * The register details match the C version in early_uart.c
 */

#ifndef _ALDERLAKE_EARLY_CONSOLE_H
#define _ALDERLAKE_EARLY_CONSOLE_H

/* PCI configuration address for bus 0 */
#define PCI_CFG_ADDR(dev, func, reg) \
	(0x80000000 | ((dev) << 11) | ((func) << 8) | ((reg) & 0xfc))

#define P2SB_BAR	PCI_CFG_ADDR(0x1f, 1, 0x10)
#define P2SB_CMD	PCI_CFG_ADDR(0x1f, 1, 0x04)
#define UART0_BAR	PCI_CFG_ADDR(0x1e, 0, 0x10)
#define UART0_CMD	PCI_CFG_ADDR(0x1e, 0, 0x04)
#define PCI_CMD_MEM_MASTER	0x6

/* GPIO community 1 (GPP_S, H, D) and its pad-configuration registers */
#define PID_GPIOCOM1	0x6d
#define PAD_CFG_BASE	0x700

/*
 * Each pad has four config registers (16 bytes). Community 1 starts at
 * GPP_S0, which has 8 pads, so GPP_H0 is pad 8 within the community
 */
#define GPP_H_INDEX	8

/* UART0 uses GPP_H10 (RXD) and GPP_H11 (TXD), in native function 2 */
#define PAD_CFG(pad) \
	(CONFIG_PCR_BASE_ADDRESS + (PID_GPIOCOM1 << 16) + PAD_CFG_BASE + \
	 ((GPP_H_INDEX + (pad)) * 16))
#define PAD_DW0_NF2	0x40000800	/* reset type 'deep', mode NF2 */

/* LPSS registers, relative to the UART's MMIO base */
#define LPSS_CLOCK_CTL	0x200
#define LPSS_RESET_CTL	0x204
#define LPSS_CLK_VAL	0xffff04b5	/* M=0x25a, N=0x7fff, update, enable */
#define LPSS_RST_VAL	3

/* ns16550 registers, at the 32-bit spacing which the LPSS UART uses */
#define UART_THR	0x00
#define UART_DLL	0x00
#define UART_DLM	0x04
#define UART_FCR	0x08
#define UART_LCR	0x0c
#define UART_LSR	0x14
#define UART_LSR_THRE	0x20

/* Write a value to PCI configuration space. Clobbers eax and edx */
.macro write_pci addr, val
	movl	$\addr, %eax
	movw	$0xcf8, %dx
	outl	%eax, %dx
	movl	$\val, %eax
	movw	$0xcfc, %dx
	outl	%eax, %dx
.endm

/*
 * Set up the console UART: route its pads through the P2SB sideband, give
 * the controller a BAR, start its clock and set the baud rate.
 * Clobbers eax and edx
 */
.macro early_console_init
	/* Give the P2SB a BAR so that the GPIO pads can be reached */
	write_pci P2SB_BAR, CONFIG_PCR_BASE_ADDRESS
	write_pci P2SB_CMD, PCI_CMD_MEM_MASTER

	/* Route the two UART0 pads */
	movl	$PAD_DW0_NF2, %eax
	movl	%eax, PAD_CFG(10)
	movl	$0, PAD_CFG(10) + 4		/* DW1: no pull */
	movl	$PAD_DW0_NF2, %eax
	movl	%eax, PAD_CFG(11)
	movl	$0, PAD_CFG(11) + 4

	/* Give the UART a BAR and enable it */
	write_pci UART0_BAR, CONFIG_DEBUG_UART_BASE
	write_pci UART0_CMD, PCI_CMD_MEM_MASTER

	/* Take the UART out of reset and start its clock */
	movl	$CONFIG_DEBUG_UART_BASE, %edx
	movl	$LPSS_RST_VAL, %eax
	movl	%eax, LPSS_RESET_CTL(%edx)
	movl	$LPSS_CLK_VAL, %eax
	movl	%eax, LPSS_CLOCK_CTL(%edx)

	/* Set the baud rate: divisor = clock / (16 * baud) */
	movl	$0x83, %eax			/* 8n1, divisor latch */
	movl	%eax, UART_LCR(%edx)
	movl	$(CONFIG_DEBUG_UART_CLOCK / (16 * CONFIG_BAUDRATE)), %eax
	movl	%eax, UART_DLL(%edx)
	xorl	%eax, %eax
	movl	%eax, UART_DLM(%edx)
	movl	$0x03, %eax			/* 8n1 */
	movl	%eax, UART_LCR(%edx)
	movl	$0x07, %eax			/* enable and clear the FIFOs */
	movl	%eax, UART_FCR(%edx)
.endm

/* Write a character to the console. Clobbers eax and edx */
.macro early_putc ch
	movl	$CONFIG_DEBUG_UART_BASE, %edx
9997:	movl	UART_LSR(%edx), %eax
	testl	$UART_LSR_THRE, %eax
	jz	9997b
	movl	$\ch, %eax
	movl	%eax, UART_THR(%edx)
.endm

#endif
