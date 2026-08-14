// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/****************************************************************************
*
*    Copyright (c) 2005 - 2023 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License version 2 as
*    published by the Free Software Foundation.
*

*****************************************************************************/

#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_platform.h"
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#include <linux/reset.h>
#include <linux/nvmem-consumer.h>
#include "gc_hal_kernel_platform_allwinner.h"

aw_driver_t aw_driver = {
	.rst = NULL,
	.mclk = NULL,
	.pclk = NULL,
	.aclk = NULL,
	.hclk = NULL,
	.arst = NULL,
	.hrst = NULL,
	.regulator = NULL,
	.vol = 0,

	.mbus_gate = NULL,
	.ahb_gate = NULL,
	.bus = NULL,
	.mbus = NULL,
	.vf_index = 0,
	.enable_pm = false,
};

/*
 * Tina U-Boot board_sun60iw2.c :: sunxi_set_sramc_mode()
 * SYSCTRL SRAM_CTRL_REG2 bit1 = 0 muxes on-chip SRAM to NPU.
 */
#define SUNXI_SYSCTRL_BASE		0x03000000UL
#define SUNXI_SRAM_CTRL_REG2_OFF	0x8
#define SUNXI_SRAM_TO_NPU_BIT		1

static gceSTATUS npu_clk_init(void);

static void npu_set_sramc_mode(void)
{
	void __iomem *reg;
	u32 val;

	reg = ioremap(SUNXI_SYSCTRL_BASE + SUNXI_SRAM_CTRL_REG2_OFF, 4);
	if (!reg) {
		pr_err("galcore: fail ioremap SYSCTRL SRAM_CTRL_REG2\n");
		return;
	}

	val = readl(reg);
	if (val & BIT(SUNXI_SRAM_TO_NPU_BIT)) {
		val &= ~BIT(SUNXI_SRAM_TO_NPU_BIT);
		writel(val, reg);
		val = readl(reg);
	}
	if (val & BIT(SUNXI_SRAM_TO_NPU_BIT))
		pr_err("galcore: set sram to npu FAIL, SRAM_CTRL_REG2=0x%08x\n", val);
	else
		pr_info("galcore: set sram to npu OK, SRAM_CTRL_REG2=0x%08x\n", val);

	iounmap(reg);
}

/* Get SID DVFS via existing nvmem sunxi_sid (same as cpufreq / vipcore). */
static int npu_read_dvfs_code(struct device *dev, u32 *code)
{
	u32 word;
	int ret;

	ret = nvmem_cell_read_u32(dev, "dvfs", &word);
	if (ret)
		return ret;

	*code = (word >> 16) & 0xff;
	if (!*code)
		*code = (word >> 8) & 0xff;

	return 0;
}

static int match_vf_table(u32 combi, u32 *index)
{
	struct device_node *np = NULL;
	int nsels, ret, i;
	u32 tmp;

	np = of_find_node_by_name(NULL, "vf_mapping_table");
	if (!np) {
		pr_err("Unable to find node\n");
		return -EINVAL;
	}

	if (!of_get_property(np, "table", &nsels)) {
		of_node_put(np);
		return -EINVAL;
	}

	nsels /= sizeof(u32);
	if (!nsels) {
		pr_err("invalid table property size\n");
		of_node_put(np);
		return -EINVAL;
	}

	for (i = 0; i < nsels / 2; i++) {
		ret = of_property_read_u32_index(np, "table", i * 2, &tmp);
		if (ret) {
			pr_err("could not retrieve table property: %d\n", ret);
			of_node_put(np);
			return ret;
		}

		if (tmp == combi) {
			ret = of_property_read_u32_index(np, "table", i * 2 + 1, &tmp);
			if (ret) {
				pr_err("could not retrieve table property: %d\n", ret);
				of_node_put(np);
				return ret;
			}
			*index = tmp;
			break;
		}
	}
	if (i == nsels / 2)
		pr_notice("%s %d, could not match vf table, i:%d", __func__, __LINE__, i);

	of_node_put(np);
	return 0;
}

static u32 get_dcxo_clk_source(void)
{
	static const char * const names[] = { "dcxo24M", "dcxo26M", "dcxo", NULL };
	struct clk *dcxo_rate;
	u32 clock_rate;
	int i;

	for (i = 0; names[i]; i++) {
		dcxo_rate = clk_get(NULL, names[i]);
		if (IS_ERR(dcxo_rate))
			continue;
		clock_rate = clk_get_rate(dcxo_rate);
		clk_put(dcxo_rate);
		if (clock_rate)
			return clock_rate;
	}

	printk("failed to get dcxo clock source, assume 24MHz\n");
	return 24000000;
}

static void get_vf_index(struct device *dev)
{
	u32 dvfs = 0;

	aw_driver.vf_index = 0x0100;

	if (npu_read_dvfs_code(dev, &dvfs))
		printk("failed to get soc dvfs from nvmem, use default vf table\n");

	match_vf_table(dvfs, &aw_driver.vf_index);

	printk("current dvfs: %x, vf index: %x\n", dvfs, aw_driver.vf_index);
}

#define DCXO_CLK_26M    (26000000)
#define MAX_NAME_LEN	64
/* Get NPU CLK and Vol */
static int get_npu_clk_vol(unsigned int vf_index, int npu_vf, int *npu_vol, uint64_t *npu_clk)
{
	struct device_node *npu_table_node;
	struct device_node *opp_node;
	int opp_vol = 0;
	uint64_t opp_hz = 0;
	int err;
	char temp[40], opp_temp[40];

	char microvolt[MAX_NAME_LEN] =  {0};
	char opp_freq[MAX_NAME_LEN] = {0};
	u32 clock_rate = 24000000;

	if (vf_index > 0xFFFF)
		vf_index = 0;

	if (vf_index == 0) {
		vf_index = 0x0100;
		pr_notice("DVFS Get Fail, use default VF0100!");
	}

	pr_notice("NPU Use VF%04x, use freq %d\n", vf_index, npu_vf);

	npu_table_node = of_find_node_by_name(NULL, "npu-opp-table");

	clock_rate = get_dcxo_clk_source();
	printk("clock_rate: %d\n", clock_rate);
	if (clock_rate == DCXO_CLK_26M)
		snprintf(microvolt, MAX_NAME_LEN, "opp-microvolt-26m-vf%04x", vf_index);
	else
		snprintf(microvolt, MAX_NAME_LEN, "opp-microvolt-vf%04x", vf_index);

	snprintf(opp_freq, MAX_NAME_LEN, "opp-%d", npu_vf);

	opp_node = of_find_node_by_name(npu_table_node, opp_freq);

	err = of_property_read_u64_index(opp_node, "opp-hz", 0, &opp_hz);
	if (err != 0) {
		printk("Get NPU CLK again!\n");
		err = of_property_read_u32_index(opp_node, "opp-hz", 0, (u32 *)&opp_hz);
		if (err != 0)
			printk("Failed to get NPU CLK\n");
	}

	err = of_property_read_u32_index(opp_node, microvolt, 0, &opp_vol);
	if (err != 0)
		pr_notice("Get NPU VOL FAIL!\n");
	*npu_clk = opp_hz;
	*npu_vol = opp_vol;

	return 0;
}

/*
 * @brief regulator the VIP vol.
 */
static gceSTATUS npu_regulator_enable(struct device_node *node)
{
	int err, npu_regulator = 0;

	if (!aw_driver.regulator)
		return 0;

	/*
	 * npu-setvol: 1 = driver programs voltage; 0/absent = keep boot/DT rail.
	 * A733 npu-supply is shared vdd-sys — do not default to 1.
	 */
	err = of_property_read_u32_index(node, "npu-setvol", 0, &npu_regulator);
	if (err)
		npu_regulator = 0;

	if (aw_driver.vol && npu_regulator)
		regulator_set_voltage(aw_driver.regulator, aw_driver.vol, aw_driver.vol);

	if (regulator_enable(aw_driver.regulator)) {
		printk("enable regulator failed!\n");
		return -1;
	}

	return 0;
}

static gceSTATUS npu_regulator_disable(void)
{
	if (!aw_driver.regulator)
		return 0;

	if (regulator_is_enabled(aw_driver.regulator))
		regulator_disable(aw_driver.regulator);

	return 0;
}

static gceSTATUS _AdjustParam(IN gcsPLATFORM *Platform, OUT gcsMODULE_PARAMETERS *Args)
{
	struct platform_device *pdev = Platform->device;
	int irqLine = platform_get_irq(pdev, 0);
	int ret, err, vol, npu_vf = 0, npu_vol;
	uint64_t npu_clk;
	unsigned long rate, real_rate;
	unsigned int mod_clk = 0;
	struct device *dev = &(pdev->dev);
	struct resource *res;

	printk("galcore: irq line = %d\n", irqLine);
	printk("galcore: ####################enter _AdjustParam "
		   "######################\n");
	printk("galcore: galcore irq number is %d.\n", irqLine);
	if (irqLine < 0) {
		printk("galcore: get galcore irq resource error\n");
		irqLine = platform_get_irq_byname(pdev, "galcore");
		printk("galcore: galcore irq number is %d\n", irqLine);
	}
	if (irqLine < 0)
		return gcvSTATUS_OUT_OF_RESOURCES;

	Args->irqs[gcvCORE_MAJOR] = irqLine;
	/* Args->irqs[gcvCORE_MAJOR] = 65; */
	printk("galcore: xp galcore irq number is %d.\n", Args->irqs[gcvCORE_MAJOR]);
	/*
	 * Args->contiguousBase = 0x41000000;
	 * Args->contiguousSize = 0x1000000;
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		printk("galcore: no resource for registers\n");
		ret = -ENOENT;
		return -1;
	}

	aw_driver.regulator = regulator_get(dev, "npu");
	if (IS_ERR(aw_driver.regulator)) {
		pr_notice("Don`t Set NPU regulator!\n");
		aw_driver.regulator = NULL;
	}

	/* Get NPU VF */
	ret = of_property_read_u32(pdev->dev.of_node, "npu-vf", &npu_vf);
	if (!ret) {
		get_vf_index(dev);

		err = get_npu_clk_vol(aw_driver.vf_index, npu_vf, &npu_vol, &npu_clk);
		if (!err) {
			aw_driver.vol = npu_vol;
			mod_clk = npu_clk;
		} else {
			pr_notice("Get NPU CLK and Vol Failed!\n");
		}
	}

	/*
	 * If the shared rail cannot sustain the requested OPP voltage, drop to
	 * an OPP that fits (800 mV -> 852 MHz on vf0200).
	 */
	if (aw_driver.regulator && aw_driver.vol) {
		int rail_uv = regulator_get_voltage(aw_driver.regulator);
		int setvol = 0;

		of_property_read_u32_index(pdev->dev.of_node, "npu-setvol", 0, &setvol);
		if (!setvol && rail_uv > 0 && aw_driver.vol > rail_uv) {
			uint64_t safe_clk = 0;
			int safe_vol = 0;

			if (!get_npu_clk_vol(aw_driver.vf_index, 852, &safe_vol, &safe_clk) &&
			    safe_vol > 0 && safe_vol <= rail_uv) {
				pr_notice("galcore: clamp npu %d MHz/%duV -> 852 MHz/%duV (rail %duV)\n",
					  npu_vf, aw_driver.vol, safe_vol, rail_uv);
				aw_driver.vol = safe_vol;
				mod_clk = safe_clk;
				npu_vf = 852;
			}
		}
	}

	/* Set NPU Vol */
	if (aw_driver.vol && aw_driver.regulator) {
		err = npu_regulator_enable(pdev->dev.of_node);
		if (err) {
			pr_notice("enable regulator failed!\n");
			return -1;
		}
		vol = regulator_get_voltage(aw_driver.regulator);
		pr_notice("galcore: Want set npu vol(%d) now vol(%d)\n", aw_driver.vol, vol);
	}

	/* Args->extSRAMBases[0] = 0xFF000000; */
	Args->extSRAMSizes[0] =  0x80000;

	Args->registerBases[0] = res->start;
	/* printk("Args->registerBases:%u\n",res->start); */
	Args->registerSizes[0] = resource_size(res);
	/* printk("Args->registerSize:%u\n",Args->registerSizes[0]); */

	printk("galcore: %s %d SUCCESS\n", __func__, __LINE__);
	aw_driver.mclk = of_clk_get_by_name(pdev->dev.of_node, "clk_npu");
	if (IS_ERR_OR_NULL(aw_driver.mclk)) {
		pr_err("failed to get NPU model clk\n");
		return -1;
	}
	aw_driver.pclk = of_clk_get_by_name(pdev->dev.of_node, "clk_parent");
	if (IS_ERR_OR_NULL(aw_driver.pclk)) {
		pr_err("failed to get NPU parent clk\n");
		return -1;
	}

	aw_driver.mbus_gate = of_clk_get_by_name(pdev->dev.of_node, "clk_mbus_gate");
	if (IS_ERR_OR_NULL(aw_driver.mbus_gate))
		pr_notice("NPU MBUS GATE NULL\n");

	aw_driver.ahb_gate = of_clk_get_by_name(pdev->dev.of_node, "clk_ahb_gate");
	if (IS_ERR_OR_NULL(aw_driver.ahb_gate))
		pr_notice("NPU AHB GATE NULL\n");

	aw_driver.aclk = of_clk_get_by_name(pdev->dev.of_node, "npu-aclk");
	if (IS_ERR(aw_driver.aclk))
		aw_driver.aclk = NULL;
	aw_driver.hclk = of_clk_get_by_name(pdev->dev.of_node, "npu-hclk");
	if (IS_ERR(aw_driver.hclk))
		aw_driver.hclk = NULL;

	aw_driver.bus = of_clk_get_by_name(pdev->dev.of_node, "clk_bus");
	if (IS_ERR_OR_NULL(aw_driver.bus))
		pr_notice("NPU BUS CLK NULL\n");

	aw_driver.arst = devm_reset_control_get(&pdev->dev, "npu_axi_rst");
	if (IS_ERR_OR_NULL(aw_driver.arst))
		pr_notice("NPU AXI RST NULL\n");
	aw_driver.hrst = devm_reset_control_get(&pdev->dev, "npu_ahb_rst");
	if (IS_ERR_OR_NULL(aw_driver.hrst))
		pr_notice("NPU AHB RST NULL\n");
	aw_driver.rst = devm_reset_control_get(&pdev->dev, "npu_rst");
	if (IS_ERR_OR_NULL(aw_driver.rst)) {
		pr_err("failed to get NPU reset handle\n");
		return -1;
	}
	if (reset_control_deassert(aw_driver.rst)) {
		pr_err("vipcore: Couldn't deassert NPU RST\n");
		return -EBUSY;
	}

	/* Set NPU CLK */
	if (mod_clk) {
		if (!IS_ERR_OR_NULL(aw_driver.pclk)) {
			rate = clk_round_rate(aw_driver.pclk, mod_clk);
			if (clk_set_rate(aw_driver.pclk, rate)) {
				pr_err("clk_set_rate:%ld  mod_clk:%d failed\n", rate, mod_clk);
				return -1;
			}
			real_rate = clk_get_rate(aw_driver.pclk);
			printk("galcore: Want set pclk rate(%d) support(%ld) real(%ld)\n", mod_clk, rate, real_rate);
			ret  = clk_set_parent(aw_driver.mclk, aw_driver.pclk);
			if (ret != 0) {
				pr_err("clk_set_parent() failed! return\n");
				return -1;
			}
		}
		rate = clk_round_rate(aw_driver.mclk, mod_clk);
		if (clk_set_rate(aw_driver.mclk, rate)) {
			pr_err("clk_set_rate:%ld  mod_clk:%d failed\n", rate, mod_clk);
			return -1;
		}
		real_rate = clk_get_rate(aw_driver.mclk);
		printk("galcore: Want set mclk rate(%d) support(%ld) real(%ld)\n", mod_clk, rate, real_rate);
	} else {
		pr_notice("CLK Frequency Get Failed! Use parent clk!\n");
		if (!IS_ERR_OR_NULL(aw_driver.pclk)) {
			printk("galcore: %s rate:%d\n", __func__, mod_clk);
			ret  = clk_set_parent(aw_driver.mclk, aw_driver.pclk);
			if (ret != 0) {
				pr_err("clk_set_parent() failed! return\n");
				return -1;
			}
		}
	}
	aw_driver.enable_pm = of_property_present(pdev->dev.of_node, "power-domains");
	if (aw_driver.enable_pm) {
		pm_runtime_enable(dev);
	} else if (gcvSTATUS_OK != npu_clk_init()) {
		pr_err("galcore: Couldn't enable module clock\n");
		return -EBUSY;
	}
	return gcvSTATUS_OK;
}

/*
 * @brief turn on the VIP clock.
 */
static gceSTATUS npu_clk_init(void)
{
	/* Match Tina U-Boot / vipcore: SRAM muxed to NPU before clocks/run */
	npu_set_sramc_mode();

	if (!IS_ERR_OR_NULL(aw_driver.hrst)) {
		if (reset_control_deassert(aw_driver.hrst)) {
			pr_err("galcore: Couldn't deassert AHB RST\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.arst)) {
		if (reset_control_deassert(aw_driver.arst)) {
			pr_err("galcore: Couldn't deassert AXI RST\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.rst)) {
		if (reset_control_deassert(aw_driver.rst)) {
			pr_err("galcore: Couldn't deassert NPU RST\n");
			return -EBUSY;
		}
	}

	/*sun60iw2*/
	if (!IS_ERR_OR_NULL(aw_driver.ahb_gate)) {
		if (clk_prepare_enable(aw_driver.ahb_gate)) {
			pr_err("galcore: Couldn't enable AHB GATE clock\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.mbus_gate)) {
		if (clk_prepare_enable(aw_driver.mbus_gate)) {
			pr_err("galcore: Couldn't enable MBUS GATE clock\n");
			return -EBUSY;
		}
	}

	/*V85X bus set*/
	if (!IS_ERR_OR_NULL(aw_driver.bus)) {
		if (clk_prepare_enable(aw_driver.bus)) {
			printk("galcore: Couldn't enable AHB clock\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.mbus)) {
		if (clk_prepare_enable(aw_driver.mbus)) {
			printk("galcore: Couldn't enable AXI clock\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.hclk)) {
		if (clk_prepare_enable(aw_driver.hclk)) {
			pr_err("galcore: Couldn't enable AHB clock\n");
			return -EBUSY;
		}
	}
	if (!IS_ERR_OR_NULL(aw_driver.aclk)) {
		if (clk_prepare_enable(aw_driver.aclk)) {
			pr_err("galcore: Couldn't enable AXI clock\n");
			return -EBUSY;
		}
	}

	if (aw_driver.mclk) {
		if (clk_prepare_enable(aw_driver.mclk)) {
			pr_err("Couldn't enable module clock\n");
			return -EBUSY;
		}
	}

	return gcvSTATUS_OK;
}

/*
 * @brief turn off the VIP clock.
 */
static gceSTATUS npu_clk_uninit(void)
{
	if (aw_driver.mclk)
		clk_disable_unprepare(aw_driver.mclk);

	/*V85X bus set*/
	if (!IS_ERR_OR_NULL(aw_driver.bus))
		clk_disable_unprepare(aw_driver.bus);
	if (!IS_ERR_OR_NULL(aw_driver.mbus))
		clk_disable_unprepare(aw_driver.mbus);

	if (!IS_ERR_OR_NULL(aw_driver.hclk))
		clk_disable_unprepare(aw_driver.hclk);
	if (!IS_ERR_OR_NULL(aw_driver.aclk))
		clk_disable_unprepare(aw_driver.aclk);

	/* sun60iw2 */
	if (!IS_ERR_OR_NULL(aw_driver.mbus_gate)) {
		clk_disable_unprepare(aw_driver.mbus_gate);
	}

	if (!IS_ERR_OR_NULL(aw_driver.ahb_gate)) {
		clk_disable_unprepare(aw_driver.ahb_gate);
	}

	if (!IS_ERR_OR_NULL(aw_driver.hrst)) {
		if (reset_control_assert(aw_driver.hrst)) {
			pr_err("galcore: Couldn't assert AHB RST\n");
			return -EBUSY;
		}
	}
	if (!IS_ERR_OR_NULL(aw_driver.arst)) {
		if (reset_control_assert(aw_driver.arst)) {
			pr_err("galcore: Couldn't assert AXI RST\n");
			return -EBUSY;
		}
	}

	if (!IS_ERR_OR_NULL(aw_driver.rst)) {
		if (reset_control_assert(aw_driver.rst)) {
			pr_err("galcore: Couldn't assert NPU RST\n");
			return -EBUSY;
		}
	}

	return gcvSTATUS_OK;
}

static gceSTATUS _SetPower(IN gcsPLATFORM *Platform, IN gctUINT32 DevIndex, IN gceCORE GPU, IN gctBOOL Enable)
{
	struct platform_device *pdev = Platform->device;
	struct device *dev = &(pdev->dev);
	switch (Enable) {
	case gcvTRUE:
		printk("galcore: %s %d ON\n", __func__, GPU);
		if (aw_driver.enable_pm) {
			if (gcvSTATUS_OK != npu_clk_init())
				pr_err("galcore: Couldn't enable module clock\n");

			if (pm_runtime_get_sync(dev) < 0)
				pr_err("failed to get pm runtime\n");
		}
		break;
	case gcvFALSE:
		printk("galcore: %s %d OFF\n", __func__, GPU);
		if (aw_driver.enable_pm) {
			pm_runtime_put(dev);
			npu_clk_uninit();
		}
		break;
	default:
		printk("galcore: Unsupport clk status");
		break;
	}

	return gcvSTATUS_OK;
}


static gceSTATUS _SetClock(IN gcsPLATFORM *Platform, IN gctUINT32 DevIndex, IN gceCORE GPU, IN gctBOOL Enable)
{
	/*
	 * void *ccmu_vaddr = ioremap(0x02001000, 0xff0);
	 * printk("#enter _SetClock#\n");
	 * writel(0x80000000, ccmu_vaddr + 0x6e0);
	 * writel(0x10001, ccmu_vaddr + 0x6ec);
	 * iounmap(ccmu_vaddr);
	 */
	return gcvSTATUS_OK;
}

static gceSTATUS _GetPower(IN gcsPLATFORM *Platform)
{
	printk("galcore: enter _GetPower \n");
	return gcvSTATUS_OK;
}

static gceSTATUS _PutPower(IN gcsPLATFORM *Platform)
{
	printk("galcore: enter _PutPower \n");
	return gcvSTATUS_OK;
}

static struct _gcsPLATFORM_OPERATIONS default_ops = {
	.adjustParam = _AdjustParam,
	.getPower = _GetPower,
	.putPower = _PutPower,
	.setPower = _SetPower,
	.setClock = _SetClock,
};

static struct _gcsPLATFORM default_platform = {
	.name = __FILE__, .ops = &default_ops,
};

static struct platform_device *default_dev;

static const struct of_device_id galcore_dev_match[] = {
	{.compatible = "allwinner,npu"}, {},
};

int gckPLATFORM_Init(struct platform_driver *pdrv,
			 struct _gcsPLATFORM **platform)
{
	printk("galcore: enter gckPLATFORM_Init from allwinenertech\n");
	pdrv->driver.of_match_table = galcore_dev_match;

	*platform = &default_platform;

	return 0;
}

int gckPLATFORM_Terminate(struct _gcsPLATFORM *platform)
{
	struct platform_device *pdev = platform->device;
	struct device *dev = &(pdev->dev);
	if (dev != NULL) {
		if (aw_driver.vol) {
			npu_regulator_disable();
			if (aw_driver.regulator) {
				regulator_put(aw_driver.regulator);
				aw_driver.regulator = NULL;
			}
		}
		printk("%s %d SUCCESS\n", __func__, __LINE__);
		if (aw_driver.enable_pm)
			pm_runtime_disable(dev);
	} else {
		printk("%s %d ERR\n", __func__, __LINE__);
	}

	if (default_dev) {
		platform_device_unregister(default_dev);
		default_dev = NULL;
	}

	return 0;
}

MODULE_VERSION("6.4.15");
MODULE_AUTHOR("Miujiu <zhangjunjie@allwinnertech.com>");
