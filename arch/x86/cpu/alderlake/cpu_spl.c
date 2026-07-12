// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Early (TPL/SPL) init for Alder Lake. For now this does the minimum
 * needed to get TPL running with a console: the CPU comes out of
 * Cache-as-RAM setup (FSP-T, see car.S) and needs only the UART.
 */

#include <dm.h>
#include <init.h>
#include <log.h>
#include <spl.h>

/**
 * arch_cpu_init_tpl() - Set up the console in TPL
 *
 * The CPU comes out of Cache-as-RAM setup (FSP-T) needing only the UART, so
 * start the early console and probe the serial device.
 *
 * Return: 0 if OK, -ve on error
 */
static int arch_cpu_init_tpl(void)
{
	struct udevice *serial;
	int ret;

	gd->baudrate = CONFIG_BAUDRATE;
	ret = uclass_first_device_err(UCLASS_SERIAL, &serial);
	if (ret)
		return log_msg_ret("ser", ret);

	return 0;
}

/**
 * arch_cpu_init_spl() - Set up the BARs and devices which FSP-M needs
 *
 * Return: 0 if OK, -ve on error
 */
static int arch_cpu_init_spl(void)
{
	/* TODO(sjg@chromium.org): Set up BARs and devices needed by FSP-M */
	return 0;
}

/**
 * arch_cpu_init() - Perform early CPU init for the current phase
 *
 * Dispatch to the TPL or SPL setup, depending on the build phase.
 *
 * Return: 0 if OK, -ve on error
 */
int arch_cpu_init(void)
{
	int ret = 0;

	if (xpl_phase() == PHASE_TPL)
		ret = arch_cpu_init_tpl();
	else if (xpl_phase() == PHASE_SPL)
		ret = arch_cpu_init_spl();

	return ret;
}
