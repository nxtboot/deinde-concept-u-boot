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
#include <asm/fast_spi.h>
#include <asm/pci.h>

/* The fast-SPI controller, which holds the flash's memory map */
#define PCH_DEV_FAST_SPI	PCI_BDF(0, 0x1f, 5)
#define FAST_SPI_BASE		0xfe010000

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
	/*
	 * Give the fast-SPI controller its BAR. FSP-M is read through the
	 * flash's memory map, which is found from this controller's
	 * registers, and nothing has set it up at this point. The shared
	 * helper also enables prefetching and write access
	 */
	fast_spi_early_init(PCH_DEV_FAST_SPI, FAST_SPI_BASE);

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
