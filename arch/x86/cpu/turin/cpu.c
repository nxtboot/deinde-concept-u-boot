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
#include <asm/cpu.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/mtrr.h>
#include <asm/post.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

/* MSRs */
#define MSR_MMIO_CONF_BASE	0xc0010058
#define MMIO_CONF_EN		BIT(0)
#define MMIO_CONF_BUS_RANGE_SHIFT 2
#define MSR_TOP_MEM		0xc001001a
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

int arch_early_init_r(void)
{
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
