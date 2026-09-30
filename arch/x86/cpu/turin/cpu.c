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

#include <cpu_func.h>
#include <init.h>
#include <log.h>
#include <spl.h>
#include <time.h>
#include <asm/cpu.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/mtrr.h>
#include <asm/arch/cpu.h>
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
#define PM_LPC_GATING		0xec
#define PM_LPC_ENABLE		BIT(0)

/* LPC bridge at 00:14.3; its SPI/eSPI register block */
#define LPC_BDF_REG(reg)	((0x14 << 11) | (3 << 8) | (reg))
#define LPC_SPI_BASE_ADDRESS	0xa0
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
#define SMU_NUM_ARGS		6
#define SMU_MSG_USB_INIT	0xa
#define SMU_RESULT_OK		1

/* Aspeed BMC SuperIO behind eSPI */
#define SIO_INDEX		0x2e
#define SIO_DATA		0x2f
#define SIO_ENTRY_KEY		0xa5
#define SIO_EXIT_KEY		0xaa
#define SIO_LDN_SUART1		2

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
}

/**
 * turin_smu_request() - Send a message to the SMU and wait for its reply
 *
 * @msg: Message ID
 * @args: Six argument words, updated with the SMU's reply
 * Return: SMU result code (SMU_RESULT_OK on success), or -ETIMEDOUT
 */
static int turin_smu_request(u32 msg, u32 *args)
{
	ulong start;
	u32 resp;
	int i;

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

	return resp;
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
	/* ECAM, so that PCI config access works */
	wrmsrl(MSR_MMIO_CONF_BASE, ECAM_BASE | MMIO_CONF_EN |
	       (__fls(ECAM_BUSES) << MMIO_CONF_BUS_RANGE_SHIFT));

	/* ACPIMMIO */
	pm_io_setbits8(0x04, PM_04_ACPIMMIO_DECODE_EN);

	/* LPC controller and the SPI/eSPI register block */
	setbits_8(ACPIMMIO_PMIO + PM_LPC_GATING, PM_LPC_ENABLE);
	pci_cf8_write32(LPC_BDF_REG(LPC_SPI_BASE_ADDRESS), SPI_BASE);

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

int arch_early_init_r(void)
{
	int ret;

	turin_mem_restore_signoff();
	turin_ecam_init();
	turin_smu_usb_init();
	ret = turin_mpio_init();
	if (ret)
		log_err("MPIO link setup failed (err=%d)\n", ret);

	return 0;
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
