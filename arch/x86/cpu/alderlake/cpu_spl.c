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
 * The P2SB, which provides the sideband (PCR) register space. It must be set
 * up before any PCR access, such as the ACPI-base setup below. Its HPTC
 * register enables HPET decode, which FspMemoryInit() relies on to store its
 * global-data pointer
 */
#define PCH_DEV_P2SB		PCI_BDF(0, 0x1f, 1)
#define P2SB_HPTC		0x60
#define P2SB_HPTC_ADDR_ENABLE	BIT(7)

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

/*
 * System-agent (host bridge 00:00.0) fixed BARs, which coreboot programs
 * just before FSP-M, so the MRC can use them. The addresses come from
 * coreboot's soc/intel/alderlake iomap
 */
#define SA_DEV_ROOT		PCI_BDF(0, 0, 0)
#define SA_EPBAR		0x40
#define SA_MCHBAR		0x48
#define SA_DMIBAR		0x68
#define SA_BAR_ENABLE		BIT(0)
#define SA_PAM0			0x80

#define MCH_BASE		0xfedc0000
#define DMI_BASE		0xfeda0000
#define EP_BASE			0xfeda1000
#define EDRAM_BASE		0xfed80000
#define REG_BASE		0xfb000000

/* These two BARs live within the MCHBAR MMIO space */
#define MCHBAR_EDRAMBAR		0x5408
#define MCHBAR_REGBAR		0x5420

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
 * setup_p2sb() - Set up the P2SB sideband BAR and enable HPET decode
 *
 * All PCR accesses go through the P2SB's sideband-register BAR, and its HPTC
 * register enables HPET decode, which FspMemoryInit() uses to store its
 * global-data pointer. This must run before any PCR access, such as
 * setup_acpi_base().
 */
static void setup_p2sb(void)
{
	pci_x86_write_config(PCH_DEV_P2SB, PCI_BASE_ADDRESS_0,
			     CONFIG_PCR_BASE_ADDRESS, PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_P2SB, PCI_BASE_ADDRESS_1, 0,
			     PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_P2SB, PCI_COMMAND,
			     PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
			     PCI_SIZE_16);

	pci_x86_write_config(PCH_DEV_P2SB, P2SB_HPTC, P2SB_HPTC_ADDR_ENABLE,
			     PCI_SIZE_8);
}

/**
 * setup_sa_bars() - Set up the system agent's fixed BARs
 *
 * The MRC uses these during memory init. Also open the PAM region so that
 * 0xc0000-0xfffff is serviced by DRAM. coreboot does this in romstage just
 * before calling FSP-M.
 */
static void setup_sa_bars(void)
{
	uint i;

	/* The upper halves of these 64-bit BARs are zero */
	pci_x86_write_config(SA_DEV_ROOT, SA_MCHBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_MCHBAR, MCH_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_DMIBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_DMIBAR, DMI_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_EPBAR + 4, 0, PCI_SIZE_32);
	pci_x86_write_config(SA_DEV_ROOT, SA_EPBAR, EP_BASE | SA_BAR_ENABLE,
			     PCI_SIZE_32);

	/*
	 * REGBAR and EDRAMBAR are programmed through MCHBAR; as with the
	 * BARs above, write the upper half first so the BAR never decodes
	 * at a stale address
	 */
	writel(0, MCH_BASE + MCHBAR_REGBAR + 4);
	writel(REG_BASE | SA_BAR_ENABLE, MCH_BASE + MCHBAR_REGBAR);
	writel(0, MCH_BASE + MCHBAR_EDRAMBAR + 4);
	writel(EDRAM_BASE | SA_BAR_ENABLE, MCH_BASE + MCHBAR_EDRAMBAR);

	/*
	 * Let reads and writes to the PAM region go to DRAM. Each PAM
	 * register covers two segments, one per nibble, with 3 enabling
	 * both reads and writes; PAM0's lower nibble is reserved
	 */
	pci_x86_write_config(SA_DEV_ROOT, SA_PAM0, 0x30, PCI_SIZE_8);
	for (i = 1; i <= 6; i++)
		pci_x86_write_config(SA_DEV_ROOT, SA_PAM0 + i, 0x33,
				     PCI_SIZE_8);
}

/**
 * check_acpi_base() - Check that the ACPI base has decoded
 *
 * Warn if the P2SB or the ACPI-base shadow does not read back as expected.
 */
static void check_acpi_base(void)
{
	ulong vendev, bar;
	u32 bar4, pcien;

	pci_x86_read_config(PCH_DEV_P2SB, PCI_VENDOR_ID, &vendev,
			    PCI_SIZE_32);
	pci_x86_read_config(PCH_DEV_P2SB, PCI_BASE_ADDRESS_0, &bar,
			    PCI_SIZE_32);
	bar4 = readl(psf3_reg(PSF_SHDW_BAR4));
	pcien = readl(psf3_reg(PSF_SHDW_PCIEN));
	if (vendev == 0xffffffff || (bar4 & ~0xf) != ACPI_BASE_ADDRESS ||
	    !(pcien & PSF_PCIEN_IOEN))
		log_warning("P2SB: vendev %lx, bar %lx, psf3 bar4 %x, pcien %x\n",
			    vendev, bar, bar4, pcien);
	else
		log_debug("P2SB: acpi base %x decoded\n", bar4);
}

/**
 * setup_acpi_base() - Set up the PMC's ACPI I/O base
 *
 * Without it FSP-M cannot read the power and sleep state, and its PMC code
 * polls I/O ports which read as 0xffff, so it never gets through memory
 * init. The PSF is reached through the P2SB sideband BAR, set up in
 * setup_p2sb().
 *
 * Return: true if the base was programmed, false if not reachable
 */
static bool setup_acpi_base(void)
{
	if (readl(psf3_reg(PSF_SHDW_BAR4)) == 0xffffffff) {
		log_warning("PSF3 read failed; ACPI base not set up\n");
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

	/*
	 * The PMC's config header is shadowed in the PSF, so the PWRM setup
	 * (which writes the PMC command register) must come before the ACPI
	 * base setup: writing the command register clears the shadow's
	 * I/O-enable bit, which the ACPI-base setup sets
	 */
	setup_p2sb();
	setup_pwrmbase();
	setup_sa_bars();

	/*
	 * Clear the ACPI power-management status and set the sleep state to
	 * S0, so that FSP-M reads a sane power state rather than a stale
	 * wake status. Skipped when the base is not decoding, since the
	 * writes would go to an undecoded I/O range
	 */
	if (setup_acpi_base()) {
		check_acpi_base();
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
