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
#include <asm/io.h>
#include <linux/bitops.h>

/* The fast-SPI controller, which holds the flash's memory map */
#define PCH_DEV_FAST_SPI	PCI_BDF(0, 0x1f, 5)
#define FAST_SPI_BASE		0xfe010000

/*
 * The PMC (power-management controller), whose PWRM MMIO block holds the ETR
 * register used to request a global reset. FSP-M asks for a reset after
 * memory init, and that must be a global reset so the CSE is reset too
 */
#define PCH_DEV_PMC		PCI_BDF(0, 0x1f, 2)
#define PWRM_BASE		0xfe000000
#define PWRM_ACTL		0x1bd8
#define PWRM_EN			BIT(8)

/* ACPI I/O base, holding PM1_STS/EN/CNT etc. which FSP-M reads */
#define ACPI_BASE_ADDRESS	0x1800

/*
 * The PMC's ACPI-base BAR is shadowed in the PSF3 fabric, so it is set up
 * there rather than in PCI config. Sideband port ID 0xbc, PMC register base
 * 0x1100, then BAR4 (the ACPI base) and the I/O-enable shadow
 */
#define PID_PSF3		0xbc
#define PSF3_PMC_REG_BASE	0x1100
#define PSF_SHDW_BAR4		0x10
#define PSF_SHDW_PCIEN		0x1c
#define PSF_PCIEN_IOEN		0x01

/* ACPI PM1 register offsets and fields */
#define PM1_STS			0x00
#define PM1_CNT			0x04
#define SLP_TYP_MASK		(7 << 10)
#define SLP_EN			BIT(13)

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
 * psf3_reg() - Get the address of a register in the PSF3 fabric
 *
 * @offset: Register offset from the PMC's PSF3 register base
 * Return: pointer to the register
 */
static void *psf3_reg(uint offset)
{
	return (void *)(CONFIG_PCR_BASE_ADDRESS + (PID_PSF3 << 16) +
			PSF3_PMC_REG_BASE + offset);
}

/**
 * setup_acpi_base() - Set up the PMC's ACPI I/O base
 *
 * Without it FSP-M cannot read the power and sleep state. Note that the PSF
 * is reached through the P2SB sideband BAR, which at this point is only set
 * up when the debug UART is enabled (see adl_early_uart_init()); a later
 * patch sets it up explicitly.
 *
 * Return: true if the base was programmed, false if not reachable
 */
static bool setup_acpi_base(void)
{
	if (readl(psf3_reg(PSF_SHDW_BAR4)) == 0xffffffff) {
		log_warning("ACPI base: PSF3 not reachable\n");
		return false;
	}

	/* Disable I/O decode while changing the address */
	clrbits_le32(psf3_reg(PSF_SHDW_PCIEN), PSF_PCIEN_IOEN);
	writel(ACPI_BASE_ADDRESS, psf3_reg(PSF_SHDW_BAR4));
	setbits_le32(psf3_reg(PSF_SHDW_PCIEN), PSF_PCIEN_IOEN);

	return true;
}

/**
 * setup_pwrmbase() - Set up the PMC's PWRM MMIO base
 *
 * This holds the ETR register, which selects whether a CF9 reset is a global
 * reset. FSP-M requests a reset after memory init and it must be a global one
 * (see fsp_handle_reset()), so PWRM must be decoding by then.
 */
static void setup_pwrmbase(void)
{
	pci_x86_write_config(PCH_DEV_PMC, PCI_BASE_ADDRESS_0, PWRM_BASE,
			     PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_PMC, PCI_COMMAND,
			     PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			     PCI_SIZE_16);
	setbits_le32(PWRM_BASE + PWRM_ACTL, PWRM_EN);
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

	setup_pwrmbase();

	/*
	 * Clear the ACPI power-management status and set the sleep state to
	 * S0, so that FSP-M reads a sane power state rather than a stale
	 * wake status. Skipped when the base is not decoding, since the
	 * writes would go to an undecoded I/O range
	 */
	if (setup_acpi_base()) {
		outw(0xffff, ACPI_BASE_ADDRESS + PM1_STS);
		outl(inl(ACPI_BASE_ADDRESS + PM1_CNT) &
		     ~(SLP_TYP_MASK | SLP_EN),
		     ACPI_BASE_ADDRESS + PM1_CNT);
	}

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
