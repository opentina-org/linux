// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner DE352 DETOP clock/reset controller (A733).
 *
 * Unlike DE2/DE3 (gates at DE+0x0), DE352 module clocks live in DETOP:
 *   +0x00 RESET  — displayN gate at bit (N * 4), writes need BIT(16) key
 *   +0x04 CLK    — same bit layout / key
 *   +0x08 MBUS   — bit0 clk_en, bit4 reset deassert (no key; never set bit8)
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

#include "ccu_common.h"
#include "ccu_gate.h"
#include "ccu_reset.h"

#include "ccu-sun60i-de352.h"

#define DE352_RESET_REG		0x00
#define DE352_CLK_REG		0x04
#define DE352_MBUS_REG		0x08

#define DE352_KEY		BIT(16)
#define DE352_DISP_SHIFT	4
#define DE352_MBUS_CLK_EN	BIT(0)
#define DE352_MBUS_RST_N	BIT(4)
#define DE352_MOD_RATE		600000000UL

static SUNXI_CCU_GATE_WITH_KEY(mixer0_clk, "mixer0", "de0",
			       DE352_CLK_REG, DE352_KEY, BIT(0),
			       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED);
static SUNXI_CCU_GATE_WITH_KEY(mixer1_clk, "mixer1", "de0",
			       DE352_CLK_REG, DE352_KEY,
			       BIT(1 * DE352_DISP_SHIFT),
			       CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED);

/*
 * MBUS is shared by all mixers. Export as bus-mixer0; enabling sets clk_en and
 * reset-deassert together (BSP __de_mbus_*). Auto-gate bit8 must stay clear.
 */
static SUNXI_CCU_GATE(bus_mixer0_clk, "bus-mixer0", "de0-gate",
		      DE352_MBUS_REG, DE352_MBUS_CLK_EN | DE352_MBUS_RST_N,
		      CLK_IGNORE_UNUSED);

static struct ccu_common *sun60i_de352_ccu_clks[] = {
	&mixer0_clk.common,
	&mixer1_clk.common,
	&bus_mixer0_clk.common,
};

static struct clk_hw_onecell_data sun60i_de352_hw_clks = {
	.hws = {
		[CLK_MIXER0]		= &mixer0_clk.common.hw,
		[CLK_BUS_MIXER0]	= &bus_mixer0_clk.common.hw,
		[CLK_MIXER1]		= &mixer1_clk.common.hw,
	},
	.num = CLK_NUMBER,
};

static const struct ccu_reset_map sun60i_de352_resets[] = {
	[RST_MIXER0] = { DE352_RESET_REG, BIT(0) },
	[RST_MIXER1] = { DE352_RESET_REG, BIT(1 * DE352_DISP_SHIFT) },
};

struct sun60i_de352_clk {
	struct device		*dev;
	void __iomem		*base;
	spinlock_t		lock;
	struct ccu_reset	reset;
	struct clk_bulk_data	parents[4];
	struct reset_control	*rst_sys;
	struct reset_control	*rst_bus;
	int			num_registered;
};

static int sun60i_de352_reset_assert(struct reset_controller_dev *rcdev,
				     unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(ccu->lock, flags);
	reg = readl(ccu->base + map->reg);
	writel((reg & ~map->bit) | DE352_KEY, ccu->base + map->reg);
	spin_unlock_irqrestore(ccu->lock, flags);

	return 0;
}

static int sun60i_de352_reset_deassert(struct reset_controller_dev *rcdev,
				       unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(ccu->lock, flags);
	reg = readl(ccu->base + map->reg);
	writel(reg | map->bit | DE352_KEY, ccu->base + map->reg);
	spin_unlock_irqrestore(ccu->lock, flags);

	return 0;
}

static int sun60i_de352_reset_status(struct reset_controller_dev *rcdev,
				     unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];

	/* 0 = deasserted (bit set), matching ccu_reset_status polarity */
	return !(map->bit & readl(ccu->base + map->reg));
}

static const struct reset_control_ops sun60i_de352_reset_ops = {
	.assert		= sun60i_de352_reset_assert,
	.deassert	= sun60i_de352_reset_deassert,
	.status		= sun60i_de352_reset_status,
};

static void sun60i_de352_clk_remove_action(void *data)
{
	struct sun60i_de352_clk *de352 = data;
	int i;

	reset_controller_unregister(&de352->reset.rcdev);
	of_clk_del_provider(de352->dev->of_node);

	for (i = 0; i < de352->num_registered; i++) {
		struct clk_hw *hw = sun60i_de352_hw_clks.hws[i];

		if (hw)
			clk_hw_unregister(hw);
	}

	reset_control_assert(de352->rst_bus);
	reset_control_assert(de352->rst_sys);
	clk_bulk_disable_unprepare(ARRAY_SIZE(de352->parents), de352->parents);
}

static int sun60i_de352_clk_probe(struct platform_device *pdev)
{
	struct sun60i_de352_clk *de352;
	void __iomem *reg;
	int i, ret;

	de352 = devm_kzalloc(&pdev->dev, sizeof(*de352), GFP_KERNEL);
	if (!de352)
		return -ENOMEM;

	de352->dev = &pdev->dev;
	de352->parents[0].id = "ahb";
	de352->parents[1].id = "mbus";
	de352->parents[2].id = "bus";
	de352->parents[3].id = "mod";

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(de352->parents),
				de352->parents);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Couldn't get DE parent clocks\n");

	de352->rst_sys = devm_reset_control_get_shared(&pdev->dev, "sys");
	if (IS_ERR(de352->rst_sys))
		return dev_err_probe(&pdev->dev, PTR_ERR(de352->rst_sys),
				     "Couldn't get DE system reset\n");

	de352->rst_bus = devm_reset_control_get_shared(&pdev->dev, "bus");
	if (IS_ERR(de352->rst_bus))
		return dev_err_probe(&pdev->dev, PTR_ERR(de352->rst_bus),
				     "Couldn't get DE0 bus reset\n");

	/*
	 * The DETOP register interface stalls when DE0 has no functional clock.
	 * Keep the complete parent chain running before changing its rate or
	 * releasing either DE reset.
	 */
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(de352->parents), de352->parents);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Couldn't enable DE parent clocks\n");

	ret = clk_set_rate(de352->parents[3].clk, DE352_MOD_RATE);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't set DE module rate: %d\n", ret);
		goto err_disable_clks;
	}

	/* A733 BSP order: release the DE system first, then the DE0 instance. */
	ret = reset_control_deassert(de352->rst_sys);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't deassert DE system reset: %d\n",
			ret);
		goto err_disable_clks;
	}

	ret = reset_control_deassert(de352->rst_bus);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't deassert DE0 bus reset: %d\n",
			ret);
		goto err_assert_sys_reset;
	}

	spin_lock_init(&de352->lock);
	de352->base = reg;

	for (i = 0; i < ARRAY_SIZE(sun60i_de352_ccu_clks); i++) {
		sun60i_de352_ccu_clks[i]->base = reg;
		sun60i_de352_ccu_clks[i]->lock = &de352->lock;
	}

	for (i = 0; i < sun60i_de352_hw_clks.num; i++) {
		struct clk_hw *hw = sun60i_de352_hw_clks.hws[i];

		if (!hw)
			continue;

		ret = clk_hw_register(&pdev->dev, hw);
		if (ret) {
			dev_err(&pdev->dev, "Couldn't register clock %s: %d\n",
				clk_hw_get_name(hw), ret);
			de352->num_registered = i;
			goto err_unreg_clks;
		}
	}
	de352->num_registered = sun60i_de352_hw_clks.num;

	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get,
				     &sun60i_de352_hw_clks);
	if (ret)
		goto err_unreg_clks;

	de352->reset.base = reg;
	de352->reset.lock = &de352->lock;
	de352->reset.reset_map = sun60i_de352_resets;
	de352->reset.rcdev.of_node = pdev->dev.of_node;
	de352->reset.rcdev.ops = &sun60i_de352_reset_ops;
	de352->reset.rcdev.owner = THIS_MODULE;
	de352->reset.rcdev.nr_resets = ARRAY_SIZE(sun60i_de352_resets);

	ret = reset_controller_register(&de352->reset.rcdev);
	if (ret) {
		of_clk_del_provider(pdev->dev.of_node);
		goto err_unreg_clks;
	}

	ret = devm_add_action_or_reset(&pdev->dev,
				       sun60i_de352_clk_remove_action, de352);
	if (ret)
		return ret;

	return 0;

err_unreg_clks:
	for (i = de352->num_registered - 1; i >= 0; i--) {
		struct clk_hw *hw = sun60i_de352_hw_clks.hws[i];

		if (hw)
			clk_hw_unregister(hw);
	}
	reset_control_assert(de352->rst_bus);
err_assert_sys_reset:
	reset_control_assert(de352->rst_sys);
err_disable_clks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(de352->parents), de352->parents);
	return ret;
}

static const struct of_device_id sun60i_de352_clk_ids[] = {
	{ .compatible = "allwinner,sun60i-a733-de352-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun60i_de352_clk_ids);

static struct platform_driver sun60i_de352_clk_driver = {
	.probe	= sun60i_de352_clk_probe,
	.driver	= {
		.name			= "sun60i-de352-clks",
		.of_match_table		= sun60i_de352_clk_ids,
	},
};
module_platform_driver(sun60i_de352_clk_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Allwinner DE352 DETOP CCU");
MODULE_LICENSE("GPL");
