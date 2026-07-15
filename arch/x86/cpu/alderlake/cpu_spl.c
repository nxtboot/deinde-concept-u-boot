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
#include <time.h>
#include <linux/delay.h>
#include <asm/cpu.h>
#include <asm/fast_spi.h>
#include <asm/msr.h>
#include <asm/pci.h>
#include <asm/arch/gpio.h>
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
#define SMI_EN			0x30
#define SMI_STS			0x34

/* Global-reset and host-partial-reset cause registers, in the PWRM space */
#define GBLRST_CAUSE0		0x1924
#define GBLRST_CAUSE1		0x1928
#define HPR_CAUSE0		0x192c

/* TCO watchdog status; SECOND_TO_STS in TCO2 means the watchdog rebooted */
#define TCO1_STS		0x04
#define TCO2_STS		0x06
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

/*
 * GPIO pad configuration for the NVMe SSD, from coreboot's felwinter GPIO
 * tables. Each pad has 16 bytes of config registers at PAD_CFG_BASE within
 * its community's sideband space; the index is relative to the community's
 * first pad (community 0 starts at GPP_B0, community 1 at GPP_S0)
 */
#define PAD_CFG_BASE		0x700
#define PAD_CFG_SIZE		16

/* Pad-configuration DW0 fields */
#define PAD_RESET_DEEP		BIT(30)
#define PAD_TRIG_OFF		(2 << 25)
#define PAD_MODE_NF1		BIT(10)
#define PAD_RX_DISABLE		BIT(9)
#define PAD_TX_DISABLE		BIT(8)
#define PAD_GPO(val)		(PAD_RESET_DEEP | PAD_TRIG_OFF | \
				 PAD_RX_DISABLE | (val))
#define PAD_GPI			(PAD_RESET_DEEP | PAD_TRIG_OFF | \
				 PAD_TX_DISABLE)

/*
 * The SMBus controller, which also hosts the TCO watchdog. The FSP uses the
 * watchdog during memory init, so the TCO I/O base must decode
 */
#define PCH_DEV_SMBUS		PCI_BDF(0, 0x1f, 4)
#define SMBUS_IO_BASE		0xefa0
#define SMBUS_HOSTC		0x40	/* config space */
#define SMBUS_HOSTC_HST_EN	BIT(0)
/* offsets within the I/O BAR */
#define SMBHSTSTAT		0x0
#define SMBHSTCTL		0x2
#define SMBUS_TCOBASE		0x50
#define SMBUS_TCOCTL		0x54
#define SMBUS_TCOCTL_EN		BIT(8)
#define TCO_BASE_ADDRESS	0x400
#define TCO1_CNT		0x08
#define TCO1_TMR_HLT		BIT(11)

/* The DMI sideband port holds the general-purpose memory range registers */
#define PID_DMI			0x88
#define GPMR_TCOBASE		0x2778
#define GPMR_TCOEN		BIT(1)

/*
 * The eSPI/LPC bridge's I/O decode ranges. The fixed enables cover the EC's
 * ports (62/66 etc.) and the generic ranges cover brya's EC host command,
 * data and memory-map windows. Each is mirrored into the DMI fabric
 */
#define PCH_DEV_ESPI		PCI_BDF(0, 0x1f, 0)
#define LPC_IO_ENABLES		0x82
#define LPC_GEN1_DEC		0x84
#define LPC_FIXED_IOE		0x3f0c
#define GPMR_LPCLGIR1		0x2730
#define GPMR_LPCIOE		0x2774

/* RTC configuration in its sideband port; enables the upper CMOS bank */
#define PID_RTC			0xc3
#define PCR_RTC_CONF		0x3400
#define PCR_RTC_CONF_UCMOS_EN	BIT(2)

/* Writing zero to this MSR unlocks the memory configuration (TXT) */
#define MSR_LT_UNLOCK_MEMORY	0x2e6

/* CPUID.1 ecx flags showing that the CPU supports TXT */
#define CPUID_ECX_VMX		BIT(5)
#define CPUID_ECX_SMX		BIT(6)

/*
 * The PMC IPC mailbox, at the bottom of the PWRM MMIO space. Command 0xa9
 * sub-command 1 asks the PMC to disable the MEI (HECI) devices, 0 to enable
 */
#define PMC_IPC_CMD		0x0
#define PMC_IPC_STS		0x4
#define PMC_IPC_STS_BUSY	BIT(0)
#define PMC_IPC_STS_ERR		BIT(1)
#define PMC_IPC_MEI_DISABLE	0xa9
#define PMC_IPC_SUBCMD_SHIFT	12

/**
 * pcr_reg() - Get the address of a PCR register, reached through the P2SB
 *
 * @pid: Sideband port ID
 * @offset: Register offset within the port
 * Return: pointer to the register
 */
static void *pcr_reg(uint pid, uint offset)
{
	return (void *)(CONFIG_PCR_BASE_ADDRESS + (pid << 16) + offset);
}

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
	return pcr_reg(PID_PSF3, PSF3_PMC_REG_BASE + offset);
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

	/*
	 * Note: coreboot also programs PCIEXBAR (ECAM) and clears TSEG here,
	 * but doing either hangs the very next HECI MMIO access on this
	 * board, and the FSP evidently sets up ECAM itself (its PCI accesses
	 * work without it), so neither is done
	 */

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
 * setup_smbus_tco() - Set up the SMBus controller and its TCO watchdog
 *
 * The FSP uses the watchdog during memory init, so its I/O range must
 * decode; the TCO timer is halted, since nothing services it. coreboot does
 * the same in romstage just before FSP-M.
 */
static void setup_smbus_tco(void)
{
	/* SMBus I/O base and enable */
	pci_x86_write_config(PCH_DEV_SMBUS, PCI_BASE_ADDRESS_4, SMBUS_IO_BASE,
			     PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_SMBUS, SMBUS_HOSTC, SMBUS_HOSTC_HST_EN,
			     PCI_SIZE_8);
	pci_x86_write_config(PCH_DEV_SMBUS, PCI_COMMAND, PCI_COMMAND_IO,
			     PCI_SIZE_16);

	/* Disable interrupts and clear errors */
	outb(0, SMBUS_IO_BASE + SMBHSTCTL);
	outb(0xff, SMBUS_IO_BASE + SMBHSTSTAT);

	/* Program the TCO base, disabled while changing the address */
	pci_x86_write_config(PCH_DEV_SMBUS, SMBUS_TCOCTL, 0, PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_SMBUS, SMBUS_TCOBASE, TCO_BASE_ADDRESS,
			     PCI_SIZE_32);
	pci_x86_write_config(PCH_DEV_SMBUS, SMBUS_TCOCTL, SMBUS_TCOCTL_EN,
			     PCI_SIZE_32);

	/* Point the DMI fabric at it too, then halt the timer */
	writel(TCO_BASE_ADDRESS | GPMR_TCOEN, pcr_reg(PID_DMI, GPMR_TCOBASE));
	outw(inw(TCO_BASE_ADDRESS + TCO1_CNT) | TCO1_TMR_HLT,
	     TCO_BASE_ADDRESS + TCO1_CNT);
}

/**
 * setup_lpc_decodes() - Set up the eSPI/LPC I/O decodes and CMOS upper bank
 *
 * Set up the decodes for the EC's ranges, mirroring each into the DMI
 * fabric, and enable the upper CMOS bank. coreboot does this in its
 * bootblock; the EC's ports must decode before anything talks to it.
 */
static void setup_lpc_decodes(void)
{
	/* brya's generic decode ranges, from its coreboot devicetree */
	/* the fourth entry deliberately clears the unused range */
	static const u32 gen_dec[4] = { 0x00fc0801, 0x000c0201, 0x00fc0901 };
	uint i;

	pci_x86_write_config(PCH_DEV_ESPI, LPC_IO_ENABLES, LPC_FIXED_IOE,
			     PCI_SIZE_16);
	writel(LPC_FIXED_IOE, pcr_reg(PID_DMI, GPMR_LPCIOE));

	for (i = 0; i < ARRAY_SIZE(gen_dec); i++) {
		pci_x86_write_config(PCH_DEV_ESPI, LPC_GEN1_DEC + 4 * i,
				     gen_dec[i], PCI_SIZE_32);
		writel(gen_dec[i], pcr_reg(PID_DMI, GPMR_LPCLGIR1 + 4 * i));
	}

	setbits_le32(pcr_reg(PID_RTC, PCR_RTC_CONF), PCR_RTC_CONF_UCMOS_EN);
}

/**
 * unlock_txt_memory() - Unlock the memory configuration on TXT-capable CPUs
 *
 * TXT-capable CPUs lock the memory configuration at reset and the MRC cannot
 * program the memory controller until it is unlocked (TXT BIOS spec section
 * 6.2.5). No TXT launch has happened, so it is safe to unlock; coreboot does
 * the same before FSP-M when TXT is not in use.
 */
static void unlock_txt_memory(void)
{
	struct cpuid_result res;

	res = cpuid(1);
	if ((res.ecx & (CPUID_ECX_VMX | CPUID_ECX_SMX)) !=
	    (CPUID_ECX_VMX | CPUID_ECX_SMX))
		return;

	wrmsr(MSR_LT_UNLOCK_MEMORY, 0, 0);
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
 * set_mei_enabled() - Enable or disable the MEI (HECI) devices via the PMC
 *
 * The PMC's MEI-disable setting is sticky across at least an EC reset and
 * upsets even memory-mapped SPI reads while it is in force, so a previous
 * boot which disabled the devices must re-enable them early.
 *
 * @enable: true to enable the MEI devices, false to disable them
 * Return: 0 if OK, -EIO on a mailbox error, -ETIMEDOUT on timeout
 */
static int set_mei_enabled(bool enable)
{
	ulong start;
	u32 sts;

	writel(PMC_IPC_MEI_DISABLE | ((enable ? 0 : 1) << PMC_IPC_SUBCMD_SHIFT),
	       PWRM_BASE + PMC_IPC_CMD);
	start = get_timer(0);
	do {
		sts = readl(PWRM_BASE + PMC_IPC_STS);
		if (!(sts & PMC_IPC_STS_BUSY)) {
			if (sts & PMC_IPC_STS_ERR)
				return -EIO;
			return 0;
		}
		udelay(100);
	} while (get_timer(start) < 1000);

	return -ETIMEDOUT;
}

static void pad_cfg_write(uint pid, uint index, u32 dw0, u32 dw1)
{
	void *ptr = (void *)(CONFIG_PCR_BASE_ADDRESS + (pid << 16) +
			     PAD_CFG_BASE + index * PAD_CFG_SIZE);

	writel(dw0, ptr);
	writel(dw1, ptr + 4);
}

/*
 * Power up the NVMe SSD with its reset held, and route its clock-request
 * pad to the clock generator. The reset is released by
 * adl_release_ssd_reset() once FSP-M has run, by which time the power is
 * stable, so that silicon init can train the link
 */
static void setup_ssd_pads(void)
{
	/* GPP_B2: M2_SSD_PLA_L (power-loss assert, inactive) */
	pad_cfg_write(PID_GPIOCOM0, GPP_B2 - GPP_B0, PAD_GPO(1), 0);
	/* GPP_B4: SSD_PERST_L, held in reset */
	pad_cfg_write(PID_GPIOCOM0, GPP_B4 - GPP_B0, PAD_GPO(0), 0);
	/* GPP_D6: SRCCLKREQ1#, native function */
	pad_cfg_write(PID_GPIOCOM1, GPP_D6 - GPP_S0,
		      PAD_RESET_DEEP | PAD_MODE_NF1, 0);
	/* GPP_D11: EN_PP3300_SSD, power on */
	pad_cfg_write(PID_GPIOCOM1, GPP_D11 - GPP_S0, PAD_GPO(1), 0);
}

void adl_release_ssd_reset(void)
{
	/* GPP_B4: SSD_PERST_L, released */
	pad_cfg_write(PID_GPIOCOM0, GPP_B4 - GPP_B0, PAD_GPO(1), 0);
}

/*
 * Route the GSC's bus and interrupt: the Cr50 TPM sits on I2C1, whose
 * pads carry other functions after reset, and signals readiness on
 * GPP_A13 (GSC_PCH_INT_ODL)
 */
static void setup_gsc_pads(void)
{
	/* GPP_H6: PCH_I2C1_SDA, native function */
	pad_cfg_write(PID_GPIOCOM1, GPP_H6 - GPP_S0,
		      PAD_RESET_DEEP | PAD_MODE_NF1, 0);
	/* GPP_H7: PCH_I2C1_SCL, native function */
	pad_cfg_write(PID_GPIOCOM1, GPP_H7 - GPP_S0,
		      PAD_RESET_DEEP | PAD_MODE_NF1, 0);
	/* GPP_A13: GSC_PCH_INT_ODL, input */
	pad_cfg_write(PID_GPIOCOM0, GPP_A13 - GPP_B0, PAD_GPI, 0);
}

void adl_log_pm_state(const char *when)
{
	log_debug("PM(%s): gblrst_cause %x %x, hpr_cause0 %x, tco_sts %x/%x, smi_en %x, smi_sts %x\n",
		  when, readl(PWRM_BASE + GBLRST_CAUSE0),
		  readl(PWRM_BASE + GBLRST_CAUSE1),
		  readl(PWRM_BASE + HPR_CAUSE0),
		  inw(TCO_BASE_ADDRESS + TCO1_STS),
		  inw(TCO_BASE_ADDRESS + TCO2_STS),
		  inl(ACPI_BASE_ADDRESS + SMI_EN),
		  inl(ACPI_BASE_ADDRESS + SMI_STS));
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
	setup_smbus_tco();
	setup_lpc_decodes();
	setup_ssd_pads();
	setup_gsc_pads();
	unlock_txt_memory();

	/*
	 * Log the reset-cause and watchdog state which decides FSP-M's
	 * memory-init flow; helpful when a boot hangs in memory init
	 */
	adl_log_pm_state("pre-fspm");

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

	if (xpl_phase() == PHASE_TPL) {
		/*
		 * A previous boot may have left the MEI devices disabled (the
		 * PMC setting is sticky) which upsets even mapped-SPI reads,
		 * so re-enable them before anything else. PWRM must decode
		 * for the PMC IPC mailbox to be reachable
		 */
		setup_pwrmbase();
		/*
		 * Ignore any failure here, since the console is not up yet;
		 * if the MEI devices stay disabled, SPL fails to locate FSP-M
		 * and reports an error there
		 */
		set_mei_enabled(true);
		ret = arch_cpu_init_tpl();
	} else if (xpl_phase() == PHASE_SPL) {
		ret = arch_cpu_init_spl();
	}

	return ret;
}
