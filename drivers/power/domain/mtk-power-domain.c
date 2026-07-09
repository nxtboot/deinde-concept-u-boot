// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include <clk.h>
#include <dm.h>
#include <regmap.h>
#include <syscon.h>
#include <asm/io.h>
#include <linux/err.h>
#include <linux/iopoll.h>

#include "mtk-power-domain.h"

/**
 * This function enables the bus protection bits for disabled power
 * domains so that the system does not hang when some unit accesses the
 * bus while in power down.
 */
static int mtk_infracfg_set_bus_protection(void __iomem *infracfg,
					   u32 mask)
{
	u32 val;

	clrsetbits_le32(infracfg + INFRA_TOPAXI_PROT_EN, mask, mask);

	return readl_poll_timeout(infracfg + INFRA_TOPAXI_PROT_STA1, val,
				  (val & mask) == mask, 100);
}

static int mtk_infracfg_clear_bus_protection(void __iomem *infracfg,
					     u32 mask)
{
	u32 val;

	clrbits_le32(infracfg + INFRA_TOPAXI_PROT_EN, mask);

	return readl_poll_timeout(infracfg + INFRA_TOPAXI_PROT_STA1, val,
				  !(val & mask), 100);
}

static int mtk_scpsys_domain_is_on(struct power_domain *power_domain)
{
	struct mtk_scp_domain *scpd = dev_get_priv(power_domain->dev);
	const struct mtk_scp_domain_data *data = &scpd->data[power_domain->id];
	u32 sta = readl(scpd->base + SPM_PWR_STATUS) &
			data->sta_mask;
	u32 sta2 = readl(scpd->base + SPM_PWR_STATUS_2ND) &
			 data->sta_mask;

	/*
	 * A domain is on when both status bits are set. If only one is set
	 * return an error. This happens while powering up a domain
	 */
	if (sta && sta2)
		return true;
	if (!sta && !sta2)
		return false;

	return -EINVAL;
}

static int mtk_scpsys_power_on(struct power_domain *power_domain)
{
	struct mtk_scp_domain *scpd = dev_get_priv(power_domain->dev);
	const struct mtk_scp_domain_data *data = &scpd->data[power_domain->id];
	void __iomem *ctl_addr = scpd->base + data->ctl_offs;
	u32 pdn_ack = data->sram_pdn_ack_bits;
	u32 val;
	int ret, tmp;

	writel(SPM_EN, scpd->base);

	val = readl(ctl_addr);
	val |= PWR_ON_BIT;
	writel(val, ctl_addr);

	val |= PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	ret = readx_poll_timeout(mtk_scpsys_domain_is_on, power_domain, tmp, tmp > 0,
				 100);
	if (ret < 0)
		return ret;

	val &= ~PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ISO_BIT;
	writel(val, ctl_addr);

	val |= PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val &= ~data->sram_pdn_bits;
	writel(val, ctl_addr);

	ret = readl_poll_timeout(ctl_addr, tmp, !(tmp & pdn_ack), 100);
	if (ret < 0)
		return ret;

	if (data->bus_prot_mask) {
		ret = mtk_infracfg_clear_bus_protection(scpd->infracfg,
							data->bus_prot_mask);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_scpsys_power_off(struct power_domain *power_domain)
{
	struct mtk_scp_domain *scpd = dev_get_priv(power_domain->dev);
	const struct mtk_scp_domain_data *data = &scpd->data[power_domain->id];
	void __iomem *ctl_addr = scpd->base + data->ctl_offs;
	u32 pdn_ack = data->sram_pdn_ack_bits;
	u32 val;
	int ret, tmp;

	if (data->bus_prot_mask) {
		ret = mtk_infracfg_set_bus_protection(scpd->infracfg,
						      data->bus_prot_mask);
		if (ret)
			return ret;
	}

	val = readl(ctl_addr);
	val |= data->sram_pdn_bits;
	writel(val, ctl_addr);

	ret = readl_poll_timeout(ctl_addr, tmp, (tmp & pdn_ack) == pdn_ack,
				 100);
	if (ret < 0)
		return ret;

	val |= PWR_ISO_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_RST_B_BIT;
	writel(val, ctl_addr);

	val |= PWR_CLK_DIS_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_BIT;
	writel(val, ctl_addr);

	val &= ~PWR_ON_2ND_BIT;
	writel(val, ctl_addr);

	ret = readx_poll_timeout(mtk_scpsys_domain_is_on, power_domain, tmp, !tmp, 100);
	if (ret < 0)
		return ret;

	return 0;
}

int mtk_power_domain_probe(struct udevice *dev)
{
	struct ofnode_phandle_args args;
	struct mtk_scp_domain *scpd = dev_get_priv(dev);
	struct regmap *regmap;
	struct clk_bulk bulk;
	int err;

	scpd->base = dev_read_addr_ptr(dev);
	if (!scpd->base)
		return -ENOENT;

	scpd->data = (const struct mtk_scp_domain_data *)dev_get_driver_data(dev);

	/* get corresponding syscon phandle */
	err = dev_read_phandle_with_args(dev, "infracfg", NULL, 0, 0, &args);
	if (err)
		return err;

	regmap = syscon_node_to_regmap(args.node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	scpd->infracfg = regmap_get_range(regmap, 0);
	if (!scpd->infracfg)
		return -ENOENT;

	/* enable Infra DCM */
	setbits_le32(scpd->infracfg + INFRA_TOPDCM_CTRL, DCM_TOP_EN);

	err = clk_get_bulk(dev, &bulk);
	if (err)
		return err;

	return clk_enable_bulk(&bulk);
}

const struct power_domain_ops mtk_power_domain_ops = {
	.off = mtk_scpsys_power_off,
	.on = mtk_scpsys_power_on,
};
