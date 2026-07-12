/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_ADL_UART_H
#define _ASM_ARCH_ADL_UART_H

/*
 * The LPSS UART is the same as Apollo Lake, so its driver, platform data
 * and init code are used unchanged
 */
#include <asm/arch-apollolake/uart.h>

/**
 * adl_early_uart_init() - Set up the UART0 console before relocation
 *
 * Routes the UART0 pads, sets a temporary MMIO base for the LPSS UART
 * and starts its clock, so the debug UART can be used
 */
void adl_early_uart_init(void);

#endif
