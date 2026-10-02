// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * AMD EPYC Turin: the PSP's ABL has trained the memory and set up the
 * memory map (TOP_MEM MSRs) before the x86 cores start. This does the few
 * things needed to reach a console and describe the memory, following what
 * the Dasharo coreboot bootblock does on the Gigabyte MZ33-AR1.
 */

#define LOG_CATEGORY LOGC_ARCH

#include <binman.h>
#include <cpu_func.h>
#include <dm/ofnode.h>
#include <init.h>
#include <log.h>
#include <mapmem.h>
#include <spl.h>
#include <time.h>
#include <vsprintf.h>
#include <asm/cpu.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/lapic.h>
#include <asm/arch/ap.h>
#include <asm/msr.h>
#include <asm/mtrr.h>
#include <asm/arch/trace.h>
#include <asm/arch/cpu.h>
#include <asm/arch/fch.h>
#include <asm/arch/opensil.h>
#include <asm/post.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

/* MSRs */
#define MSR_MMIO_CONF_BASE	0xc0010058
#define MMIO_CONF_EN		BIT(0)
#define MMIO_CONF_BUS_RANGE_SHIFT 2
#define MSR_TOP_MEM		0xc001001a
#define SYSCFG_MTRR_VAR_DRAM_EN	BIT(20)
#define SYSCFG_TOM2_EN		BIT(21)
#define SYSCFG_TOM2_WB		BIT(22)
#define MSR_TOP_MEM2		0xc001001d
#define MSR_PS_LIM		0xc0010061
#define MSR_PSTATE0		0xc0010064

/* PCIe ECAM as programmed by the ABL, 256 buses */
#define ECAM_BASE		CONFIG_PCIE_ECAM_BASE
#define ECAM_BUSES		256

/* FCH PM index/data port and the ACPIMMIO block it can enable */
#define PM_INDEX		0xcd6
#define PM_DATA			0xcd7
#define PM_04_ACPIMMIO_DECODE_EN BIT(1)
#define ACPIMMIO_BASE		0xfed80000
#define ACPIMMIO_PMIO		(ACPIMMIO_BASE + 0x300)
#define PM_DECODE_EN		0x00
#define PM_LEGACY_IO_EN		BIT(0)
#define PM_CF9_IO_EN		BIT(1)
#define PM_LEGACY_DMA_IO_EN	BIT(2)
#define PM_IOAPIC_EN		BIT(5)		/* decode it at 0xfec00000 */
#define PM_HPET_EN		BIT(6)		/* decode it at 0xfed00000 */
#define PM_HPET_MSI_EN		BIT(29)
#define PM_EVT_BLK		0x60
#define PM1_CNT_BLK		0x62
#define PM_TMR_BLK		0x64
#define PM_CPU_CNT_BLK		0x66
#define PM_GPE0_BLK		0x68
#define PM_ACPI_SMI_CMD		0x6a
#define PM_ACPI_CONF		0x74
#define PM_ACPI_DECODE_STD	BIT(0)
#define PM_ACPI_GLOBAL_EN	BIT(1)
#define PM_ACPI_RTC_EN_EN	BIT(2)
#define PM_ACPI_TIMER_EN_EN	BIT(4)
#define PM_PCI_CTRL		0x08
#define PM_MSG_INTR_EN		BIT(4)	/* send PIC interrupts as messages */
#define PM_PIC_MSG_SEL		BIT(5)
#define PM_NMI_MSG_SEL		BIT(6)
#define PM_PCI_INT_VW		0xa8
#define PM_PCI_INT_VW_MODE	0x80ffcef8	/* as coreboot and the ABL use */
#define PM_LPC_GATING		0xec
#define PM_LPC_ENABLE		BIT(0)

/* LPC bridge at 00:14.3; its SPI/eSPI register block */
#define LPC_BDF_REG(reg)	((0x14 << 11) | (3 << 8) | (reg))
#define LPC_SPI_BASE_ADDRESS	0xa0
#define  LPC_SPI_ENABLES	GENMASK(7, 0)	/* ROM decode and others */
#define SPI_BASE		0xfec10000
#define ESPI_BASE		(SPI_BASE + 0x10000)
#define ESPI_DECODE		0x40
#define ESPI_DECODE_IO_2E_2F	BIT(0)
#define ESPI_DECODE_IO_80	BIT(2)

/*
 * CMOS bank 1 (index port 0x72) bytes shared with the PSP's ABL, as used by
 * AGESA's MemRestoreLib and coreboot's memctx_cmos.c. The ABL clears the
 * memory-restore byte when it restores the saved context and expects the
 * BIOS to sign off a successful boot by setting APOB_SAVED again; without
 * that it retrains on the next boot
 */
#define CMOS_INDEX1		0x72
#define CMOS_DATA1		0x73
#define CMOS_APCB_RECOVERY_LO	0x06
#define CMOS_APCB_RECOVERY_HI	0x07
#define CMOS_APCB_RECOVERY_DISABLED 0x5555
#define CMOS_MEM_RESTORE	0x0d
#define CMOS_MEM_RESTORE_BOOT_FAIL BIT(0)
#define CMOS_APOB_SAVED		BIT(2)

/*
 * System Management Unit mailbox (MP1 C2P messages), reached through the
 * SMN index/data pair in the root complex's config space, as openSIL's
 * SmuServiceRequest does. The SMU firmware powers up and configures the
 * FCH's USB controllers on request; until then their MMIO hangs the CPU
 */
#define SMN_INDEX		0xb8	/* in PCI 00:00.0 config space */
#define SMN_DATA		0xbc
#define SMU_MSG			0x3b10930
#define SMU_RESP		0x3b1097c
#define SMU_ARG0		0x3b109c4
#define SMU_MSG_USB_INIT	0xa
#define SMU_MSG_GET_NAME	0xd
#define SMU_MSG_SET_FEATURES	0x3

/*
 * SMU features which openSIL enables by default (mSmuClassDflts), in its
 * three feature words; bit 22 of the first is CPPC, for example
 */
#define SMU_FEATURES		0x7adb4fff
#define SMU_FEATURES_EXT	0x00000001
#define SMU_FEATURES_64		0x00000000

/* Aspeed BMC SuperIO behind eSPI */
#define SIO_INDEX		0x2e
#define SIO_DATA		0x2f
#define SIO_ENTRY_KEY		0xa5
#define SIO_EXIT_KEY		0xaa
#define SIO_LDN_SUART1		2

/* FCH interrupt routing, indexed by source, with bit 7 for I/O APIC mode */
#define PCI_INTR_INDEX		0xc00
#define PCI_INTR_DATA		0xc01
#define PCI_INTR_APIC		BIT(7)
#define FCH_IRQ_MAX_ROUTES	64

static u64 msr_read64(u32 msr)
{
	u64 val;

	rdmsrl(msr, val);

	return val;
}

static void pci_cf8_write32(u32 bdf_reg, u32 val)
{
	outl(0x80000000 | bdf_reg, 0xcf8);
	outl(val, 0xcfc);
}

static u32 pci_cf8_read32(u32 bdf_reg)
{
	outl(0x80000000 | bdf_reg, 0xcf8);

	return inl(0xcfc);
}

static u32 smn_read32(u32 reg)
{
	pci_cf8_write32(SMN_INDEX, reg);

	return pci_cf8_read32(SMN_DATA);
}

static void smn_write32(u32 reg, u32 val)
{
	pci_cf8_write32(SMN_INDEX, reg);
	pci_cf8_write32(SMN_DATA, val);
	turin_trace_smn(reg, val);
}

int turin_smu_request(u32 msg, u32 *args)
{
	ulong start;
	u32 resp;
	int i;

	turin_trace_msg('U', msg, args, SMU_NUM_ARGS);
	smn_write32(SMU_RESP, 0);
	for (i = 0; i < SMU_NUM_ARGS; i++)
		smn_write32(SMU_ARG0 + 4 * i, args[i]);
	smn_write32(SMU_MSG, msg);
	start = get_timer(0);
	do {
		resp = smn_read32(SMU_RESP);
		if (resp)
			break;
	} while (get_timer(start) < 2000);
	if (!resp)
		return -ETIMEDOUT;
	for (i = 0; i < SMU_NUM_ARGS; i++)
		args[i] = smn_read32(SMU_ARG0 + 4 * i);
	turin_trace_resp('u', resp, args, SMU_NUM_ARGS);

	return resp;
}

/**
 * turin_smu_features_init() - Ask the SMU to enable its power features
 *
 * openSIL does this in InitializeSmuBrh(). Without it the SMU leaves CPPC,
 * among others, disabled, so the CPPC capability MSR reads as zero and
 * Linux cannot manage the CPU frequency
 */
static void turin_smu_features_init(void)
{
	u32 args[SMU_NUM_ARGS] = { SMU_FEATURES, SMU_FEATURES_EXT,
				   SMU_FEATURES_64 };
	int ret;

	ret = turin_smu_request(SMU_MSG_SET_FEATURES, args);
	if (ret != SMU_RESULT_OK)
		log_warning("SMU feature setup failed: %d\n", ret);
}

/**
 * turin_set_name_string() - Set the processor name string from the SMU
 *
 * The name CPUID 0x80000002-4 reports comes from six MSRs, which reset to
 * zero, so the OS would otherwise show the processor as a bare family and
 * model. The SMU holds the name; openSIL fetches it four bytes at a time
 * and writes the MSRs on every thread (the APs get them from mp.c)
 */
static void turin_set_name_string(void)
{
	u64 name[CPUID_NAME_STRING_MSRS];
	u32 *words = (u32 *)name;
	int i, ret;

	for (i = 0; i < CPUID_NAME_STRING_MSRS * 2; i++) {
		u32 args[SMU_NUM_ARGS] = { i };

		ret = turin_smu_request(SMU_MSG_GET_NAME, args);
		if (ret != SMU_RESULT_OK) {
			log_warning("SMU name string failed: %d\n", ret);
			return;
		}
		words[i] = args[0];
	}
	for (i = 0; i < CPUID_NAME_STRING_MSRS; i++)
		native_write_msr(MSR_CPUID_NAME_STRING0 + i, name[i],
				 name[i] >> 32);
	log_debug("processor: %.48s\n", (char *)name);
}

/**
 * turin_smu_usb_init() - Ask the SMU to bring up the FCH's USB controllers
 *
 * openSIL does this in its FCH USB init (FchKLXhciSmuServiceUsbInit) with a
 * bit per xHCI controller; it also sends PHY configuration afterwards,
 * which is not done here yet
 */
static void turin_smu_usb_init(void)
{
	u32 args[SMU_NUM_ARGS] = { BIT(0) | BIT(1) };
	int ret;

	ret = turin_smu_request(SMU_MSG_USB_INIT, args);
	if (ret != SMU_RESULT_OK)
		log_warning("SMU USB init failed: %d\n", ret);
}

/*
 * With SPL, SPL sets up the console path and U-Boot proper finds it ready;
 * see x86_64/cpu.c
 */
#if !IS_ENABLED(CONFIG_SPL) || IS_ENABLED(CONFIG_XPL_BUILD)
static void pm_io_setbits8(u8 reg, u8 bits)
{
	outb(reg, PM_INDEX);
	outb(inb(PM_DATA) | bits, PM_DATA);
}

static void sio_write(u8 reg, u8 val)
{
	outb(reg, SIO_INDEX);
	outb(val, SIO_DATA);
}

/**
 * turin_console_path_init() - Open the path to the SuperIO UART
 *
 * The console is the BMC's SuperIO UART at I/O 0x3f8, reached over eSPI.
 * The ABL has already opened the 0x3f8 window from the APCB and the UART
 * is usable as it stands (the ABL prints on it), so the normal console
 * needs nothing here. For the debug UART this enables the LPC/eSPI block
 * and the SuperIO configuration port and sets the UART up in the SuperIO,
 * as the coreboot bootblock does.
 */
static void turin_console_path_init(void)
{
	u32 val;

	/* ECAM, so that PCI config access works */
	wrmsrl(MSR_MMIO_CONF_BASE, ECAM_BASE | MMIO_CONF_EN |
	       (__fls(ECAM_BUSES) << MMIO_CONF_BUS_RANGE_SHIFT));

	/* ACPIMMIO */
	pm_io_setbits8(0x04, PM_04_ACPIMMIO_DECODE_EN);

	/*
	 * LPC controller and the SPI/eSPI register block. Keep the enables in
	 * the low bits, since without ROM decode the flash reads as 0xff
	 */
	setbits_8(ACPIMMIO_PMIO + PM_LPC_GATING, PM_LPC_ENABLE);
	val = pci_cf8_read32(LPC_BDF_REG(LPC_SPI_BASE_ADDRESS));
	pci_cf8_write32(LPC_BDF_REG(LPC_SPI_BASE_ADDRESS),
			SPI_BASE | (val & LPC_SPI_ENABLES));

	/* eSPI decode for the SuperIO config port and port 80 */
	setbits_le32(ESPI_BASE + ESPI_DECODE,
		     ESPI_DECODE_IO_2E_2F | ESPI_DECODE_IO_80);

	/* SuperIO: SUART1 at 0x3f8 */
	outb(SIO_ENTRY_KEY, SIO_INDEX);
	outb(SIO_ENTRY_KEY, SIO_INDEX);
	sio_write(0x07, SIO_LDN_SUART1);
	sio_write(0x30, 0x00);
	sio_write(0x60, CONFIG_DEBUG_UART_BASE >> 8);
	sio_write(0x61, CONFIG_DEBUG_UART_BASE & 0xff);
	sio_write(0x30, 0x01);
	outb(SIO_EXIT_KEY, SIO_INDEX);
}

void board_debug_uart_init(void)
{
	turin_console_path_init();
}
#endif

/**
 * turin_fch_acpi_init() - Set up the FCH's legacy decoding and ACPI hardware
 *
 * The ABL sets these up on some boots but not others (not after a cold
 * mains-on, for example), so program them rather than rely on it: the FCH's
 * I/O APIC, the legacy I/O and reset ports, the HPET, and the ACPI blocks
 * which the FADT describes. There is no SMM handler, so there is no SMI
 * command port and U-Boot switches to ACPI mode itself
 */
static void turin_fch_acpi_init(void)
{
	void *pmio = (void *)ACPIMMIO_PMIO;

	setbits_le32(pmio + PM_DECODE_EN, PM_LEGACY_IO_EN | PM_CF9_IO_EN |
		     PM_LEGACY_DMA_IO_EN | PM_IOAPIC_EN | PM_HPET_EN |
		     PM_HPET_MSI_EN);
	writew(ACPI_PM1_EVT, pmio + PM_EVT_BLK);
	writew(ACPI_PM1_CNT, pmio + PM1_CNT_BLK);
	writew(ACPI_PM_TMR, pmio + PM_TMR_BLK);
	writew(ACPI_CPU_CNT, pmio + PM_CPU_CNT_BLK);
	writew(ACPI_GPE0, pmio + PM_GPE0_BLK);
	writew(0, pmio + PM_ACPI_SMI_CMD);
	setbits_le32(pmio + PM_ACPI_CONF, PM_ACPI_DECODE_STD |
		     PM_ACPI_GLOBAL_EN | PM_ACPI_RTC_EN_EN |
		     PM_ACPI_TIMER_EN_EN);
	outw(inw(ACPI_PM1_CNT) | PM1_CNT_SCI_EN, ACPI_PM1_CNT);
}

/**
 * turin_irq_routing_init() - Set up the FCH's interrupt routing
 *
 * This writes the board's /fch amd,irq-routing table, then sets how the FCH
 * delivers interrupts, as coreboot does: the PIC's as ExtInt messages and PCI
 * interrupts as virtual wires. Without these no ISA interrupt (the timer, the
 * serial port, the SCI) reaches the CPUs, so the OS cannot use the I/O APIC
 *
 * Return: 0 if OK, -ve on error
 */
static int turin_irq_routing_init(void)
{
	void *pmio = (void *)ACPIMMIO_PMIO;
	u32 cells[FCH_IRQ_MAX_ROUTES * 3];
	ofnode node;
	int size, i;

	node = ofnode_path("/fch");
	size = ofnode_read_size(node, "amd,irq-routing");
	if (size == -EINVAL)
		return 0;
	if (size <= 0 || size % 12 || size > sizeof(cells))
		return log_msg_ret("irq", -EINVAL);
	if (ofnode_read_u32_array(node, "amd,irq-routing", cells, size / 4))
		return log_msg_ret("rd", -EINVAL);
	for (i = 0; i < size / 4; i += 3) {
		outb(cells[i], PCI_INTR_INDEX);
		outb(cells[i + 1], PCI_INTR_DATA);
		outb(cells[i] | PCI_INTR_APIC, PCI_INTR_INDEX);
		outb(cells[i + 2], PCI_INTR_DATA);
	}
	clrsetbits_le32(pmio + PM_PCI_CTRL, PM_PIC_MSG_SEL | PM_NMI_MSG_SEL,
			PM_MSG_INTR_EN);
	writel(PM_PCI_INT_VW_MODE, pmio + PM_PCI_INT_VW);

	return 0;
}

/**
 * turin_tsc_rate() - Work out the TSC rate from the P-state MSRs
 *
 * The TSC runs at the P0 core frequency, which on family 1Ah is 5MHz times
 * the frequency ID of the highest-performance P-state.
 *
 * Return: TSC rate in Hz, or 0 if it cannot be determined
 */
static ulong turin_tsc_rate(void)
{
	u64 pstate;
	uint high;

	high = msr_read64(MSR_PS_LIM) & 7;
	pstate = msr_read64(MSR_PSTATE0 + high);
	if (!(pstate & BIT_ULL(63)))
		return 0;

	return 5000000UL * (pstate & 0xfff);
}

static u64 top_mem_low(void)
{
	return msr_read64(MSR_TOP_MEM) & ~(SZ_8M - 1ULL);
}

static u64 top_mem_high(void)
{
	return msr_read64(MSR_TOP_MEM2);
}

int arch_cpu_init(void)
{
	int ret;

	post_code(POST_CPU_INIT);

	ret = IS_ENABLED(CONFIG_X86_64) ? x86_cpu_reinit_f() :
		x86_cpu_init_f();
	if (ret)
		return ret;

	gd->arch.clock_rate = turin_tsc_rate();

	/*
	 * The ABL leaves the local APIC disabled; enable it in virtual-wire
	 * mode, as the OS expects to find it
	 */
	if (xpl_phase() == PHASE_BOARD_F)
		lapic_setup();

	return 0;
}

static u8 cmos1_read(u8 index)
{
	outb(index, CMOS_INDEX1);

	return inb(CMOS_DATA1);
}

static void cmos1_write(u8 index, u8 val)
{
	outb(index, CMOS_INDEX1);
	outb(val, CMOS_DATA1);
}

/**
 * turin_mem_restore_signoff() - Tell the ABL that the memory context is good
 *
 * The saved memory context (the APOB in the flash's RW_MRC_CACHE region) is
 * only restored on the next boot if the BIOS signs off the current one, by
 * clearing the boot-failure bit and setting APOB_SAVED, and disables the
 * APCB recovery mechanism with the 0x5555 signature. U-Boot has no way to
 * write the flash here, so it does not save a new context itself: the
 * image must already carry one which matches the DIMMs, and if it does not
 * the ABL simply trains again, as it did before.
 */
static void turin_mem_restore_signoff(void)
{
	u8 val;

	cmos1_write(CMOS_APCB_RECOVERY_LO, CMOS_APCB_RECOVERY_DISABLED & 0xff);
	cmos1_write(CMOS_APCB_RECOVERY_HI, CMOS_APCB_RECOVERY_DISABLED >> 8);
	val = cmos1_read(CMOS_MEM_RESTORE);
	val = (val & ~CMOS_MEM_RESTORE_BOOT_FAIL) | CMOS_APOB_SAVED;
	cmos1_write(CMOS_MEM_RESTORE, val);
}

/*
 * AMD microcode patch header, as used by coreboot's update_microcode.c; the
 * patch-loader MSR takes the address of the header
 */
struct amd_ucode_header {
	u32 date_code;
	u32 patch_id;
	u16 mc_patch_data_id;
	u8 reserved1[6];
	u32 chipset1_dev_id;
	u32 chipset2_dev_id;
	u16 processor_rev_id;
	u8 chipset1_rev_id;
	u8 chipset2_rev_id;
	u8 reserved2[4];
} __packed;

#define MSR_PATCH_LOADER	0xc0010020
#define MSR_PATCH_LEVEL		0x8b

/**
 * turin_microcode_update() - Apply the microcode patch for this processor
 *
 * The image carries a patch for each revision of the processor, as a binman
 * entry named by the processor's equivalent revision ID. The PSP copies the
 * whole image into DRAM, so the patch is already there.
 *
 * @ucodep: Returns the patch, for the APs
 * Return: 0 if OK, -ve on error
 */
static int turin_microcode_update(const void **ucodep)
{
	const struct amd_ucode_header *hdr;
	struct binman_entry entry;
	u32 eax = cpuid_eax(1);
	u16 rev_id = (eax & 0xff0000) >> 8 | (eax & 0xff);
	char name[30];
	u64 old;
	int ret;

	snprintf(name, sizeof(name), "amd-ucode-%04x", rev_id);
	ret = binman_entry_find(name, &entry);
	if (ret)
		return log_msg_ret("fnd", ret);
	hdr = map_sysmem(CONFIG_TURIN_IMAGE_ADDR + entry.image_pos,
			 entry.size);
	if (entry.size < sizeof(*hdr) || hdr->processor_rev_id != rev_id)
		return log_msg_ret("rev", -EINVAL);
	*ucodep = hdr;

	rdmsrl(MSR_PATCH_LEVEL, old);
	if ((u32)old >= hdr->patch_id)
		return 0;
	wrmsrl(MSR_PATCH_LOADER, (ulong)hdr);
	turin_trace_msr(MSR_PATCH_LOADER, (ulong)hdr);
	rdmsrl(MSR_PATCH_LEVEL, old);
	if ((u32)old != hdr->patch_id)
		return log_msg_ret("upd", -EIO);

	/* rewrite P-state 0 so that the TSC is recalculated, as openSIL does */
	wrmsrl(MSR_PSTATE0, msr_read64(MSR_PSTATE0));
	turin_trace_msr(MSR_PSTATE0, msr_read64(MSR_PSTATE0));
	log_debug("microcode updated to %x\n", hdr->patch_id);

	return 0;
}

int arch_early_init_r(void)
{
	const void *ucode = NULL;
	int ret;

	ret = turin_irq_routing_init();
	if (ret)
		log_err("IRQ routing failed (err=%d)\n", ret);

	turin_fch_acpi_init();

	ret = turin_microcode_update(&ucode);
	if (ret)
		log_err("Microcode update failed (err=%d)\n", ret);
	/* the APs copy the name string from the boot CPU, so set it first */
	turin_set_name_string();

	/*
	 * openSIL starts the APs and sets up the SMU's features, the USB
	 * controllers and the links itself
	 */
	if (IS_ENABLED(CONFIG_TURIN_OPENSIL)) {
		turin_mem_restore_signoff();
		turin_ecam_init();
		ret = turin_opensil_init(ucode);
		if (ret)
			log_err("openSIL set-up failed (err=%d)\n", ret);
		ret = turin_find_aps();
		if (ret)
			log_err("AP discovery failed (err=%d)\n", ret);
		return 0;
	}

	ret = turin_start_aps(ucode);
	if (ret)
		log_err("AP start-up failed (err=%d)\n", ret);
	turin_mem_restore_signoff();
	turin_ecam_init();
	turin_smu_features_init();
	turin_smu_usb_init();
	ret = turin_mpio_init();
	if (ret)
		log_err("MPIO link setup failed (err=%d)\n", ret);

	return 0;
}

/* openSIL's second timepoint follows PCI enumeration, before the tables */
void board_final_init(void)
{
	if (IS_ENABLED(CONFIG_TURIN_OPENSIL))
		turin_opensil_tp2();
}

/* and its third comes once U-Boot is otherwise done */
void board_final_cleanup(void)
{
	if (IS_ENABLED(CONFIG_TURIN_OPENSIL))
		turin_opensil_tp3();
}

int dram_init(void)
{
	u64 low = top_mem_low();
	u64 high = top_mem_high();
	int ret;

	gd->ram_size = low;
	if (high > SZ_4G)
		gd->ram_size += high - SZ_4G;
	post_code(POST_DRAM);

	if (xpl_phase() == PHASE_BOARD_F) {
		/*
		 * Memory from 4GB to TOP_MEM2 is write-back through SYSCFG
		 * rather than an MTRR; the ABL leaves that off, as it does the
		 * use of variable MTRRs for DRAM. Without it Linux finds the
		 * MTRRs cover only low memory and drops the rest
		 */
		msr_setbits_64(MSR_K8_SYSCFG, SYSCFG_MTRR_VAR_DRAM_EN |
			       SYSCFG_TOM2_EN | SYSCFG_TOM2_WB);
		turin_trace_msr(MSR_K8_SYSCFG, msr_read64(MSR_K8_SYSCFG));
		ret = mtrr_add_request(MTRR_TYPE_WRBACK, 0, low);
		if (ret != -ENOSYS) {
			if (ret)
				return log_msg_ret("mta", ret);
			ret = mtrr_commit(false);
			if (ret)
				return log_msg_ret("mtc", ret);
		}
	}

	return 0;
}

int dram_init_banksize(void)
{
	u64 high = top_mem_high();

	gd->dram[0].start = 0;
	gd->dram[0].size = top_mem_low();
	if (high > SZ_4G) {
		gd->dram[1].start = SZ_4G;
		gd->dram[1].size = high - SZ_4G;
	}

	return 0;
}

phys_addr_t board_get_usable_ram_top(phys_size_t total_size)
{
	return top_mem_low();
}
