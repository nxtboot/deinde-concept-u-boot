// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 The coreboot project
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Pinctrl driver for Alder Lake (PCH-P / PCH-M). The community and pad-group
 * tables are taken from the coreboot Alder Lake support.
 */

#define LOG_CATEGORY UCLASS_GPIO

#include <dm.h>
#include <log.h>
#include <p2sb.h>
#include <asm/intel_pinctrl.h>
#include <asm-generic/gpio.h>
#include <asm/intel_pinctrl_defs.h>

/* The number of GPI status/enable registers in each community */
#define NUM_COM0_GPI_REGS	3
#define NUM_COM1_GPI_REGS	4
#define NUM_COM2_GPI_REGS	1
#define NUM_COM3_GPI_REGS	5
#define NUM_COM4_GPI_REGS	4
#define NUM_COM5_GPI_REGS	1

static const struct reset_mapping rst_map[] = {
	{ .logical = PAD_CFG0_LOGICAL_RESET_PWROK, .chipset = 0U << 30 },
	{ .logical = PAD_CFG0_LOGICAL_RESET_DEEP, .chipset = 1U << 30 },
	{ .logical = PAD_CFG0_LOGICAL_RESET_PLTRST, .chipset = 2U << 30 },
};

static const struct reset_mapping rst_map_com2[] = {
	{ .logical = PAD_CFG0_LOGICAL_RESET_PWROK, .chipset = 0U << 30 },
	{ .logical = PAD_CFG0_LOGICAL_RESET_DEEP, .chipset = 1U << 30 },
	{ .logical = PAD_CFG0_LOGICAL_RESET_PLTRST, .chipset = 2U << 30 },
	{ .logical = PAD_CFG0_LOGICAL_RESET_RSMRST, .chipset = 3U << 30 },
};

/* Groups within each community */
static const struct pad_group adl_community0_groups[] = {
	INTEL_GPP_BASE(GPP_B0, GPP_B0, GPP_B25, 0),			/* GPP_B */
	INTEL_GPP_BASE(GPP_B0, GPP_T0, GPP_T15, 32),			/* GPP_T */
	INTEL_GPP_BASE(GPP_B0, GPP_A0, GPP_ESPI_CLK_LOOPBK, 64),	/* GPP_A */
};

static const struct pad_group adl_community1_groups[] = {
	INTEL_GPP_BASE(GPP_S0, GPP_S0, GPP_S7, 96),			/* GPP_S */
	INTEL_GPP_BASE(GPP_S0, GPP_H0, GPP_H23, 128),			/* GPP_H */
	INTEL_GPP_BASE(GPP_S0, GPP_D0, GPP_GSPI2_CLK_LOOPBK, 160),	/* GPP_D */
	INTEL_GPP(GPP_S0, GPP_CPU_RSVD_1, GPP_CPU_RSVD_24),		/* CPU */
	INTEL_GPP(GPP_S0, GPP_VGPIO_0, GPP_VGPIO_37),			/* vGPIO */
};

static const struct pad_group adl_community2_groups[] = {
	INTEL_GPP_BASE(GPD0, GPD0, GPD_DRAM_RESETB, 192),		/* GPD */
};

static const struct pad_group adl_community3_groups[] = {
	/* one group, with no ACPI numbering, as coreboot has it */
	INTEL_GPP(GPP_CPU_RSVD_25, GPP_CPU_RSVD_25, GPP_vGPIO_PCIE_83),
};

static const struct pad_group adl_community4_groups[] = {
	INTEL_GPP_BASE(GPP_C0, GPP_C0, GPP_C23, 256),			/* GPP_C */
	INTEL_GPP_BASE(GPP_C0, GPP_F0, GPP_F_CLK_LOOPBK, 288),		/* GPP_F */
	INTEL_GPP(GPP_C0, GPP_L_BKLTEN, GPP_MLK_RSTB),			/* HVMOS */
	INTEL_GPP_BASE(GPP_C0, GPP_E0, GPP_E_CLK_LOOPBK, 320),		/* GPP_E */
};

static const struct pad_group adl_community5_groups[] = {
	INTEL_GPP_BASE(GPP_R0, GPP_R0, GPP_R7, 352),			/* GPP_R */
	INTEL_GPP_BASE(GPP_R0, GPP_SPI0_IO_2, GPP_SPI0_CLK, 384),	/* SPI */
};

static const struct pad_community adl_gpio_communities[] = {
	{
		.port = PID_GPIOCOM0,
		.first_pad = GPIO_COM0_START,
		.last_pad = GPIO_COM0_END,
		.num_gpi_regs = NUM_COM0_GPI_REGS,
		.gpi_status_offset = NUM_COM5_GPI_REGS + NUM_COM4_GPI_REGS +
			NUM_COM3_GPI_REGS + NUM_COM2_GPI_REGS +
			NUM_COM1_GPI_REGS,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM0",
		.reset_map = rst_map,
		.num_reset_vals = ARRAY_SIZE(rst_map),
		.groups = adl_community0_groups,
		.num_groups = ARRAY_SIZE(adl_community0_groups),
	}, {
		.port = PID_GPIOCOM1,
		.first_pad = GPIO_COM1_START,
		.last_pad = GPIO_COM1_END,
		.num_gpi_regs = NUM_COM1_GPI_REGS,
		.gpi_status_offset = NUM_COM5_GPI_REGS + NUM_COM4_GPI_REGS +
			NUM_COM3_GPI_REGS + NUM_COM2_GPI_REGS,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM1",
		.reset_map = rst_map,
		.num_reset_vals = ARRAY_SIZE(rst_map),
		.groups = adl_community1_groups,
		.num_groups = ARRAY_SIZE(adl_community1_groups),
	}, {
		.port = PID_GPIOCOM2,
		.first_pad = GPIO_COM2_START,
		.last_pad = GPIO_COM2_END,
		.num_gpi_regs = NUM_COM2_GPI_REGS,
		.gpi_status_offset = NUM_COM5_GPI_REGS + NUM_COM4_GPI_REGS +
			NUM_COM3_GPI_REGS,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM2",
		.reset_map = rst_map_com2,
		.num_reset_vals = ARRAY_SIZE(rst_map_com2),
		.groups = adl_community2_groups,
		.num_groups = ARRAY_SIZE(adl_community2_groups),
	}, {
		.port = PID_GPIOCOM3,
		.first_pad = GPIO_COM3_START,
		.last_pad = GPIO_COM3_END,
		.num_gpi_regs = NUM_COM3_GPI_REGS,
		.gpi_status_offset = NUM_COM5_GPI_REGS + NUM_COM4_GPI_REGS,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM3",
		.reset_map = rst_map,
		.num_reset_vals = ARRAY_SIZE(rst_map),
		.groups = adl_community3_groups,
		.num_groups = ARRAY_SIZE(adl_community3_groups),
	}, {
		.port = PID_GPIOCOM4,
		.first_pad = GPIO_COM4_START,
		.last_pad = GPIO_COM4_END,
		.num_gpi_regs = NUM_COM4_GPI_REGS,
		.gpi_status_offset = NUM_COM5_GPI_REGS,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM4",
		.reset_map = rst_map,
		.num_reset_vals = ARRAY_SIZE(rst_map),
		.groups = adl_community4_groups,
		.num_groups = ARRAY_SIZE(adl_community4_groups),
	}, {
		.port = PID_GPIOCOM5,
		.first_pad = GPIO_COM5_START,
		.last_pad = GPIO_COM5_END,
		.num_gpi_regs = NUM_COM5_GPI_REGS,
		.gpi_status_offset = 0,
		.pad_cfg_base = PAD_CFG_BASE,
		.host_own_reg_0 = HOSTSW_OWN_REG_0,
		.gpi_int_sts_reg_0 = GPI_INT_STS_0,
		.gpi_int_en_reg_0 = GPI_INT_EN_0,
		.gpi_smi_sts_reg_0 = GPI_SMI_STS_0,
		.gpi_smi_en_reg_0 = GPI_SMI_EN_0,
		.max_pads_per_group = GPIO_MAX_NUM_PER_GROUP,
		.name = "GPIO_COM5",
		.reset_map = rst_map,
		.num_reset_vals = ARRAY_SIZE(rst_map),
		.groups = adl_community5_groups,
		.num_groups = ARRAY_SIZE(adl_community5_groups),
	},
};

static int adl_pinctrl_of_to_plat(struct udevice *dev)
{
	const struct pad_community *comm = NULL;
	struct p2sb_child_plat *pplat;
	int i;

	/* Attach this device to its community structure */
	pplat = dev_get_parent_plat(dev);
	for (i = 0; i < ARRAY_SIZE(adl_gpio_communities); i++) {
		if (adl_gpio_communities[i].port == pplat->pid)
			comm = &adl_gpio_communities[i];
	}
	if (!comm)
		return log_msg_ret("Cannot find community", -ENOENT);

	return intel_pinctrl_of_to_plat(dev, comm, GPIO_NUM_PAD_CFG_REGS);
}

#if CONFIG_IS_ENABLED(OF_REAL)
static const struct udevice_id adl_gpio_ids[] = {
	{ .compatible = "intel,alderlake-pinctrl" },
	{ }
};
#endif

U_BOOT_DRIVER(intel_alderlake_pinctrl) = {
	.name		= "intel_alderlake_pinctrl",
	.id		= UCLASS_PINCTRL,
	.of_match	= of_match_ptr(adl_gpio_ids),
	.probe		= intel_pinctrl_probe,
	.ops		= &intel_pinctrl_ops,
#if CONFIG_IS_ENABLED(OF_REAL)
	.bind		= dm_scan_fdt_dev,
#endif
	.of_to_plat	= adl_pinctrl_of_to_plat,
	.priv_auto	= sizeof(struct intel_pinctrl_priv),
};
