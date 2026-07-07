// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2022 Rockchip Electronics Co., Ltd
 */

#include <dm.h>
#include <spl.h>
#include <asm/io.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;

#define PERI_GRF_BASE			0xff000000
#define PERI_GRF_PERI_CON1		0x0004

#define PERI_SGRF_BASE			0xff070000
#define PERI_SGRF_FIREWALL_CON0		0x0020
#define PERI_SGRF_FIREWALL_CON1		0x0024
#define PERI_SGRF_FIREWALL_CON2		0x0028
#define PERI_SGRF_FIREWALL_CON3		0x002c
#define PERI_SGRF_FIREWALL_CON4		0x0030
#define PERI_SGRF_SOC_CON3		0x00bc

#define CORE_SGRF_BASE			0xff076000
#define CORE_SGRF_FIREWALL_CON0		0x0020
#define CORE_SGRF_FIREWALL_CON1		0x0024
#define CORE_SGRF_FIREWALL_CON2		0x0028
#define CORE_SGRF_FIREWALL_CON3		0x002c
#define CORE_SGRF_FIREWALL_CON4		0x0030
#define CORE_SGRF_CPU_CTRL_CON		0x0040

#define PMU_SGRF_BASE			0xff080000

#define FW_DDR_BASE			0xff900000
#define FW_DDR_MST3_REG			0x4c
#define FW_SHRM_BASE			0xff910000
#define FW_SHRM_MST1_REG		0x44

#define CRU_BASE			0xff3b0000
#define CRU_GLB_RST_CON			0x0c10
#define CRU_PVTPLL0_CON0_L		0x1000
#define CRU_PVTPLL0_CON1_L		0x1008
#define CRU_PVTPLL1_CON0_L		0x1030
#define CRU_PVTPLL1_CON1_L		0x1038

#define VICRU_BASE			0xff3b4000
#define VICRU_VISOFTRST_CON01		0x0a04

#define CHIP_VER_REG			0xff020204
#define CHIP_VER_MSK			0x7
#define V(x)				((x) - 1)
#define ROM_VER_REG			0xffff4ffc
#define ROM_V2				0x30303256

void board_debug_uart_init(void)
{
	/* The BootROM leaves the debug UART configured */
}

static void rv1106_xpl_init(void)
{
	/* Save the chip version to OS_REG1[2:0] */
	if (readl(ROM_VER_REG) == ROM_V2)
		writel((readl(CHIP_VER_REG) & ~CHIP_VER_MSK) | V(2),
		       CHIP_VER_REG);
	else
		writel((readl(CHIP_VER_REG) & ~CHIP_VER_MSK) | V(1),
		       CHIP_VER_REG);

	/* Set all devices to non-secure */
	writel(0xffff0000, PERI_SGRF_BASE + PERI_SGRF_FIREWALL_CON0);
	writel(0xffff0000, PERI_SGRF_BASE + PERI_SGRF_FIREWALL_CON1);
	writel(0xffff0000, PERI_SGRF_BASE + PERI_SGRF_FIREWALL_CON2);
	writel(0xffff0000, PERI_SGRF_BASE + PERI_SGRF_FIREWALL_CON3);
	writel(0xffff0000, PERI_SGRF_BASE + PERI_SGRF_FIREWALL_CON4);
	writel(0x000f0000, PERI_SGRF_BASE + PERI_SGRF_SOC_CON3);
	writel(0xffff0000, CORE_SGRF_BASE + CORE_SGRF_FIREWALL_CON0);
	writel(0xffff0000, CORE_SGRF_BASE + CORE_SGRF_FIREWALL_CON1);
	writel(0xffff0000, CORE_SGRF_BASE + CORE_SGRF_FIREWALL_CON2);
	writel(0xffff0000, CORE_SGRF_BASE + CORE_SGRF_FIREWALL_CON3);
	writel(0xffff0000, CORE_SGRF_BASE + CORE_SGRF_FIREWALL_CON4);
	writel(0x00030002, CORE_SGRF_BASE + CORE_SGRF_CPU_CTRL_CON);
	writel(0x20000000, PMU_SGRF_BASE);

	/* Allow the eMMC and FSPI to access the secure area */
	writel(0x00000000, FW_DDR_BASE + FW_DDR_MST3_REG);
	writel(0xff00ffff, FW_SHRM_BASE + FW_SHRM_MST1_REG);

	/* Release the watchdog */
	writel(0x2000200, PERI_GRF_BASE + PERI_GRF_PERI_CON1);
	writel(0x400040, CRU_BASE + CRU_GLB_RST_CON);

	/*
	 * When the venc/npu use the pvtpll, a reboot fails because the
	 * pvtpll is reset before the venc/npu reset, so the venc/npu is
	 * not completely reset and the system blocks when accessing the
	 * NoC in SPL. Enable the pvtpll so that the venc/npu reset can
	 * complete.
	 */
	writel(0xffff0018, CRU_BASE + CRU_PVTPLL0_CON1_L);
	writel(0x00030003, CRU_BASE + CRU_PVTPLL0_CON0_L);
	writel(0xffff0018, CRU_BASE + CRU_PVTPLL1_CON1_L);
	writel(0x00030003, CRU_BASE + CRU_PVTPLL1_CON0_L);
	udelay(2);
}

int arch_cpu_init(void)
{
	if (is_xpl())
		rv1106_xpl_init();

	/* Reset the sdmmc0 to prevent a power leak */
	writel(0x30003000, VICRU_BASE + VICRU_VISOFTRST_CON01);
	udelay(1);
	writel(0x30000000, VICRU_BASE + VICRU_VISOFTRST_CON01);

	return 0;
}
