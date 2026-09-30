// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net> */


#include <linux/bitfield.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/sun8i-tcon-top.h>

#include "sun8i_tcon_top.h"

struct sun8i_tcon_top_quirks {
	bool has_tcon_tv0;
	bool has_tcon_tv1;
	bool has_dsi;
	bool has_legacy_mux;
};

static bool sun8i_tcon_top_node_is_tcon_top(struct device_node *node)
{
	return !!of_match_node(sun8i_tcon_top_of_table, node);
}

int sun8i_tcon_top_set_hdmi_src(struct device *dev, int tcon)
{
	struct sun8i_tcon_top *tcon_top = dev_get_drvdata(dev);
	unsigned long flags;
	u32 val;

	if (!sun8i_tcon_top_node_is_tcon_top(dev->of_node)) {
		dev_err(dev, "Device is not TCON TOP!\n");
		return -EINVAL;
	}

	/* A733 uses bit 28 as the TV0 HDMI gate, not as a source mux. */
	if (of_device_is_compatible(dev->of_node,
				    "allwinner,sun60i-a733-tcon-top"))
		return sun8i_tcon_top_hdmi_gate_enable(dev, true);

	if (tcon < 2 || tcon > 3) {
		dev_err(dev, "TCON index must be 2 or 3!\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&tcon_top->reg_lock, flags);

	val = readl(tcon_top->regs + TCON_TOP_GATE_SRC_REG);
	val &= ~TCON_TOP_HDMI_SRC_MSK;
	val |= FIELD_PREP(TCON_TOP_HDMI_SRC_MSK, tcon - 1);
	writel(val, tcon_top->regs + TCON_TOP_GATE_SRC_REG);

	spin_unlock_irqrestore(&tcon_top->reg_lock, flags);

	return 0;
}
EXPORT_SYMBOL(sun8i_tcon_top_set_hdmi_src);

int sun8i_tcon_top_hdmi_gate_enable(struct device *dev, bool enable)
{
	struct sun8i_tcon_top *tcon_top = dev_get_drvdata(dev);
	unsigned long flags;
	u32 val, setup;

	if (!tcon_top || !sun8i_tcon_top_node_is_tcon_top(dev->of_node))
		return -EINVAL;

	spin_lock_irqsave(&tcon_top->reg_lock, flags);

	/* Match BSP tcon_top_hdmi_set_gate() for sun60iw2. */
	val = readl(tcon_top->regs + TCON_TOP_GATE_SRC_REG);
	if (enable) {
		val |= BIT(TCON_TOP_TCON_TV0_GATE);
		val |= BIT(TCON_TOP_TV0_HDMI_GATE);
	} else {
		val &= ~BIT(TCON_TOP_TV0_HDMI_GATE);
	}
	writel(val, tcon_top->regs + TCON_TOP_GATE_SRC_REG);

	/*
	 * HDMI path (15.2.9.1): TV0 from HDMI (bit1=0), pixel clk from
	 * HDMI PHY (bit3=0), CCMU as TV0_CLK_SRC (bit0=0). Matches REF SETUP=0.
	 */
	setup = readl(tcon_top->regs + TCON_TOP_TCON_TV_SETUP_REG);
	if (enable)
		setup &= ~(TCON_TOP_TV0_CLK_SRC_CVBS |
			   TCON_TOP_TV0_EDP_HDMI_CK_SEL |
			   TCON_TOP_TV0_HDMIPHY_CCU_CK_SEL);
	writel(setup, tcon_top->regs + TCON_TOP_TCON_TV_SETUP_REG);

	spin_unlock_irqrestore(&tcon_top->reg_lock, flags);

	return 0;
}
EXPORT_SYMBOL(sun8i_tcon_top_hdmi_gate_enable);

int sun8i_tcon_top_de_config(struct device *dev, int mixer, int tcon)
{
	struct sun8i_tcon_top *tcon_top = dev_get_drvdata(dev);
	const struct sun8i_tcon_top_quirks *quirks;
	unsigned long flags;
	u32 reg;

	if (!sun8i_tcon_top_node_is_tcon_top(dev->of_node)) {
		dev_err(dev, "Device is not TCON TOP!\n");
		return -EINVAL;
	}

	quirks = of_device_get_match_data(dev);
	if (!quirks || !quirks->has_legacy_mux)
		return 0;

	if (mixer > 1) {
		dev_err(dev, "Mixer index is too high!\n");
		return -EINVAL;
	}

	if (tcon > 3) {
		dev_err(dev, "TCON index is too high!\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&tcon_top->reg_lock, flags);

	reg = readl(tcon_top->regs + TCON_TOP_PORT_SEL_REG);
	if (mixer == 0) {
		reg &= ~TCON_TOP_PORT_DE0_MSK;
		reg |= FIELD_PREP(TCON_TOP_PORT_DE0_MSK, tcon);
	} else {
		reg &= ~TCON_TOP_PORT_DE1_MSK;
		reg |= FIELD_PREP(TCON_TOP_PORT_DE1_MSK, tcon);
	}
	writel(reg, tcon_top->regs + TCON_TOP_PORT_SEL_REG);

	spin_unlock_irqrestore(&tcon_top->reg_lock, flags);

	return 0;
}
EXPORT_SYMBOL(sun8i_tcon_top_de_config);


static struct clk_hw *sun8i_tcon_top_register_gate(struct device *dev,
						   const char *parent,
						   void __iomem *regs,
						   spinlock_t *lock,
						   u8 bit, int name_index)
{
	const char *clk_name, *parent_name;
	int ret, index;

	index = of_property_match_string(dev->of_node, "clock-names", parent);
	if (index < 0)
		return ERR_PTR(index);

	parent_name = of_clk_get_parent_name(dev->of_node, index);

	ret = of_property_read_string_index(dev->of_node,
					    "clock-output-names", name_index,
					    &clk_name);
	if (ret)
		return ERR_PTR(ret);

	return clk_hw_register_gate(dev, clk_name, parent_name,
				    CLK_SET_RATE_PARENT,
				    regs + TCON_TOP_GATE_SRC_REG,
				    bit, 0, lock);
};

static int sun8i_tcon_top_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct clk_hw_onecell_data *clk_data;
	struct sun8i_tcon_top *tcon_top;
	const struct sun8i_tcon_top_quirks *quirks;
	void __iomem *regs;
	int ret, i;

	quirks = of_device_get_match_data(&pdev->dev);

	tcon_top = devm_kzalloc(dev, sizeof(*tcon_top), GFP_KERNEL);
	if (!tcon_top)
		return -ENOMEM;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, CLK_NUM),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->num = CLK_NUM;
	tcon_top->clk_data = clk_data;

	spin_lock_init(&tcon_top->reg_lock);

	/*
	 * H6/R40 use a single unnamed reset. A733 VO1 also needs
	 * RST_BUS_VIDEO_OUT1 ("rst_bus_reg") for the HDMI APB fabric.
	 */
	tcon_top->rst = devm_reset_control_get_optional(dev, "bus");
	if (IS_ERR(tcon_top->rst))
		return dev_err_probe(dev, PTR_ERR(tcon_top->rst),
				     "Couldn't get bus reset\n");
	if (!tcon_top->rst) {
		tcon_top->rst = devm_reset_control_get(dev, NULL);
		if (IS_ERR(tcon_top->rst)) {
			dev_err(dev, "Couldn't get our reset line\n");
			return PTR_ERR(tcon_top->rst);
		}
	}

	tcon_top->rst_reg = devm_reset_control_get_optional(dev, "rst_bus_reg");
	if (IS_ERR(tcon_top->rst_reg))
		return PTR_ERR(tcon_top->rst_reg);

	tcon_top->bus = devm_clk_get(dev, "bus");
	if (IS_ERR(tcon_top->bus)) {
		dev_err(dev, "Couldn't get the bus clock\n");
		return PTR_ERR(tcon_top->bus);
	}

	tcon_top->ahb_gate = devm_clk_get_optional(dev, "ahb-gate");
	if (IS_ERR(tcon_top->ahb_gate))
		return PTR_ERR(tcon_top->ahb_gate);

	regs = devm_platform_ioremap_resource(pdev, 0);
	tcon_top->regs = regs;
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	if (tcon_top->rst_reg) {
		ret = reset_control_deassert(tcon_top->rst_reg);
		if (ret) {
			dev_err(dev, "Could not deassert rst_bus_reg\n");
			return ret;
		}
	}

	ret = reset_control_deassert(tcon_top->rst);
	if (ret) {
		dev_err(dev, "Could not deassert ctrl reset control\n");
		goto err_assert_rst_reg;
	}

	ret = clk_prepare_enable(tcon_top->ahb_gate);
	if (ret) {
		dev_err(dev, "Could not enable ahb-gate clock\n");
		goto err_assert_reset;
	}

	ret = clk_prepare_enable(tcon_top->bus);
	if (ret) {
		dev_err(dev, "Could not enable bus clock\n");
		goto err_disable_ahb_gate;
	}

	/*
	 * At least on H6, some registers have some bits set by default
	 * which may cause issues. Clear them here. A733 VO0/VO1 uses a
	 * different register layout, so preserve the firmware state there.
	 */
	if (quirks->has_legacy_mux) {
		writel(0, regs + TCON_TOP_PORT_SEL_REG);
		writel(0, regs + TCON_TOP_GATE_SRC_REG);
	}

	/*
	 * TCON TOP has two muxes, which select parent clock for each TCON TV
	 * channel clock. Parent could be either TCON TV or TVE clock. For now
	 * we leave this fixed to TCON TV, since TVE driver for R40 is not yet
	 * implemented. Once it is, graph needs to be traversed to determine
	 * if TVE is active on each TCON TV. If it is, mux should be switched
	 * to TVE clock parent.
	 */
	i = 0;
	if (quirks->has_tcon_tv0)
		clk_data->hws[CLK_TCON_TOP_TV0] =
			sun8i_tcon_top_register_gate(dev, "tcon-tv0", regs,
						     &tcon_top->reg_lock,
						     TCON_TOP_TCON_TV0_GATE, i++);

	if (quirks->has_tcon_tv1)
		clk_data->hws[CLK_TCON_TOP_TV1] =
			sun8i_tcon_top_register_gate(dev, "tcon-tv1", regs,
						     &tcon_top->reg_lock,
						     TCON_TOP_TCON_TV1_GATE, i++);

	if (quirks->has_dsi)
		clk_data->hws[CLK_TCON_TOP_DSI] =
			sun8i_tcon_top_register_gate(dev, "dsi", regs,
						     &tcon_top->reg_lock,
						     TCON_TOP_TCON_DSI_GATE, i++);

	for (i = 0; i < CLK_NUM; i++)
		if (IS_ERR(clk_data->hws[i])) {
			ret = PTR_ERR(clk_data->hws[i]);
			goto err_unregister_gates;
		}

	if (of_property_present(dev->of_node, "#clock-cells")) {
		ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
					     clk_data);
		if (ret)
			goto err_unregister_gates;
	}

	dev_set_drvdata(dev, tcon_top);

	return 0;

err_unregister_gates:
	for (i = 0; i < CLK_NUM; i++)
		if (!IS_ERR_OR_NULL(clk_data->hws[i]))
			clk_hw_unregister_gate(clk_data->hws[i]);
	clk_disable_unprepare(tcon_top->bus);
err_disable_ahb_gate:
	clk_disable_unprepare(tcon_top->ahb_gate);
err_assert_reset:
	reset_control_assert(tcon_top->rst);
err_assert_rst_reg:
	if (tcon_top->rst_reg)
		reset_control_assert(tcon_top->rst_reg);

	return ret;
}

static void sun8i_tcon_top_unbind(struct device *dev, struct device *master,
				  void *data)
{
	struct sun8i_tcon_top *tcon_top = dev_get_drvdata(dev);
	struct clk_hw_onecell_data *clk_data = tcon_top->clk_data;
	int i;

	if (of_property_present(dev->of_node, "#clock-cells"))
		of_clk_del_provider(dev->of_node);
	for (i = 0; i < CLK_NUM; i++)
		if (clk_data->hws[i])
			clk_hw_unregister_gate(clk_data->hws[i]);

	clk_disable_unprepare(tcon_top->bus);
	clk_disable_unprepare(tcon_top->ahb_gate);
	reset_control_assert(tcon_top->rst);
	if (tcon_top->rst_reg)
		reset_control_assert(tcon_top->rst_reg);
}

static const struct component_ops sun8i_tcon_top_ops = {
	.bind	= sun8i_tcon_top_bind,
	.unbind	= sun8i_tcon_top_unbind,
};

static int sun8i_tcon_top_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_tcon_top_ops);
}

static void sun8i_tcon_top_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_tcon_top_ops);
}

static const struct sun8i_tcon_top_quirks sun8i_r40_tcon_top_quirks = {
	.has_tcon_tv0	= true,
	.has_tcon_tv1	= true,
	.has_dsi	= true,
	.has_legacy_mux = true,
};

static const struct sun8i_tcon_top_quirks sun20i_d1_tcon_top_quirks = {
	.has_tcon_tv0	= true,
	.has_dsi	= true,
	.has_legacy_mux = true,
};

static const struct sun8i_tcon_top_quirks sun50i_h6_tcon_top_quirks = {
	.has_tcon_tv0	= true,
	.has_legacy_mux = true,
};

static const struct sun8i_tcon_top_quirks sun60i_a733_tv_top_quirks = {
	.has_tcon_tv0	= true,
};

/* sun4i_drv uses this list to check if a device node is a TCON TOP */
const struct of_device_id sun8i_tcon_top_of_table[] = {
	{
		.compatible = "allwinner,sun8i-r40-tcon-top",
		.data = &sun8i_r40_tcon_top_quirks
	},
	{
		.compatible = "allwinner,sun20i-d1-tcon-top",
		.data = &sun20i_d1_tcon_top_quirks
	},
	{
		.compatible = "allwinner,sun50i-h6-tcon-top",
		.data = &sun50i_h6_tcon_top_quirks
	},
	{
		.compatible = "allwinner,sun60i-a733-tcon-top",
		.data = &sun60i_a733_tv_top_quirks
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun8i_tcon_top_of_table);
EXPORT_SYMBOL(sun8i_tcon_top_of_table);

static struct platform_driver sun8i_tcon_top_platform_driver = {
	.probe		= sun8i_tcon_top_probe,
	.remove		= sun8i_tcon_top_remove,
	.driver		= {
		.name		= "sun8i-tcon-top",
		.of_match_table	= sun8i_tcon_top_of_table,
	},
};
module_platform_driver(sun8i_tcon_top_platform_driver);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner R40 TCON TOP driver");
MODULE_LICENSE("GPL");
