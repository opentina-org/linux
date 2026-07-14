// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/sun60i-a733-cpupll-ccu.h>

#include "ccu_common.h"
#include "ccu_nm.h"
#include "ccu_nkmp.h"

#define SUN60IW2_PLL_CPU_BACK_REG	0x0000
#define SUN60IW2_PLL_CPU_L_REG		0x1000
#define SUN60IW2_PLL_CPU_B_REG		0x2000
#define SUN60IW2_PLL_CPU_DSU_REG	0x3000

#define SUN60IW2_PLL_CPU_BACK_PAT0_REG	0x0008
#define SUN60IW2_PLL_CPU_L_PAT0_REG	0x1004
#define SUN60IW2_PLL_CPU_B_PAT0_REG	0x2004
#define SUN60IW2_PLL_CPU_DSU_PAT0_REG	0x3004

#define SUN60IW2_PLL_CPU_BACK_PAT1_REG	0x000c
#define SUN60IW2_PLL_CPU_L_PAT1_REG	0x1008
#define SUN60IW2_PLL_CPU_B_PAT1_REG	0x2008
#define SUN60IW2_PLL_CPU_DSU_PAT1_REG	0x3008

#define SUN60IW2_PLL_CPU_L_LFM_REG	0x1018
#define SUN60IW2_PLL_CPU_B_LFM_REG	0x2018
#define SUN60IW2_PLL_CPU_DSU_LFM_REG	0x3018

#define SUN60IW2_CPU_L_REG	0x101c
#define SUN60IW2_CPU_B_REG	0x201c
#define SUN60IW2_CPU_DSU_REG	0x301c

static void sun60i_set_reg(void __iomem *addr, u32 val, u8 width, u8 shift)
{
	u32 reg = readl(addr);
	u32 mask = GENMASK(shift + width - 1, shift);

	writel((reg & ~mask) | ((val << shift) & mask), addr);
}

static struct ccu_nm pll_cpu_back_clk = {
	.output		= BIT(27),
	.lock		= BIT(28),
	.lock_enable	= BIT(29),
	.ldo_en		= BIT(30),
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 53, 105),
	.m		= _SUNXI_CCU_DIV(0, 4),
	.min_rate	= 159000000,
	.max_rate	= 2520000000,
	.common		= {
		.reg		= 0x0000,
		.hw.init	= CLK_HW_INIT("pll-cpu-back", "dcxo",
					      &ccu_nm_ops,
					      CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL |
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_cpu_l_clk = {
	.output		= BIT(27),
	.lock		= BIT(28),
	.lock_enable	= BIT(29),
	.ldo_en		= BIT(30),
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 28, 94),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.p_reg		= 0x101c,
	.max_rate	= 2256000000,
	.common		= {
		.reg		= 0x1000,
		.clear		= BIT(26),
		.features	= CCU_FEATURE_CLEAR_MOD,
		.hw.init	= CLK_HW_INIT("pll-cpu-l", "dcxo",
					      &ccu_nkmp_ops,
					      CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL |
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_cpu_b_clk = {
	.output		= BIT(27),
	.lock		= BIT(28),
	.lock_enable	= BIT(29),
	.ldo_en		= BIT(30),
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 28, 94),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.p_reg		= 0x201c,
	.max_rate	= 2256000000,
	.common		= {
		.reg		= 0x2000,
		.clear		= BIT(26),
		.features	= CCU_FEATURE_CLEAR_MOD,
		.hw.init	= CLK_HW_INIT("pll-cpu-b", "dcxo",
					      &ccu_nkmp_ops,
					      CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL |
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_cpu_dsu_clk = {
	.output		= BIT(27),
	.lock		= BIT(28),
	.lock_enable	= BIT(29),
	.ldo_en		= BIT(30),
	.enable		= BIT(31),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 28, 94),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.p_reg		= 0x301c,
	.max_rate	= 2256000000,
	.common		= {
		.reg		= 0x3000,
		.clear		= BIT(26),
		.features	= CCU_FEATURE_CLEAR_MOD,
		.hw.init	= CLK_HW_INIT("pll-cpu-dsu", "dcxo",
					      &ccu_nkmp_ops,
					      CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL |
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const cpu_l_parents[] = {
	"dcxo", "dcxo", "dcxo", "pll-cpu-l", "pll-peri0-2x", "pll-cpu-back"
};

static SUNXI_CCU_MUX(cpu_l_clk, "cpu_l", cpu_l_parents,
		     0x101c, 24, 3, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static const char * const cpu_b_parents[] = {
	"dcxo", "dcxo", "dcxo", "pll-cpu-b", "pll-peri0-2x", "pll-cpu-back"
};

static SUNXI_CCU_MUX(cpu_b_clk, "cpu_b", cpu_b_parents,
		     0x201c, 24, 3, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static const char * const cpu_dsu_parents[] = {
	"dcxo", "dcxo", "dcxo", "pll-cpu-dsu", "pll-peri0-2x", "pll-cpu-back"
};

static SUNXI_CCU_MUX(cpu_dsu_clk, "cpu_dsu", cpu_dsu_parents,
		     0x301c, 24, 3, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static struct ccu_common *sun60i_pll_cpu_clks[] = {
	&pll_cpu_back_clk.common,
	&pll_cpu_l_clk.common,
	&pll_cpu_b_clk.common,
	&pll_cpu_dsu_clk.common,
	&cpu_l_clk.common,
	&cpu_b_clk.common,
	&cpu_dsu_clk.common,
};

static struct clk_hw_onecell_data sun60i_cpupll_hw_clks = {
	.hws	= {
		[CLK_PLL_CPU_BACK]	= &pll_cpu_back_clk.common.hw,
		[CLK_PLL_CPU_L]		= &pll_cpu_l_clk.common.hw,
		[CLK_PLL_CPU_B]		= &pll_cpu_b_clk.common.hw,
		[CLK_PLL_CPU_DSU]	= &pll_cpu_dsu_clk.common.hw,
		[CLK_CPU_L]		= &cpu_l_clk.common.hw,
		[CLK_CPU_B]		= &cpu_b_clk.common.hw,
		[CLK_CPU_DSU]		= &cpu_dsu_clk.common.hw,
	},
	.num	= CLK_CPUPLL_MAX_NO,
};

static const struct sunxi_ccu_desc sun60i_cpupll_desc = {
	.ccu_clks	= sun60i_pll_cpu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun60i_pll_cpu_clks),
	.hw_clks	= &sun60i_cpupll_hw_clks,
};

static const u32 sun60i_pll_cpu_regs[] = {
	SUN60IW2_PLL_CPU_BACK_REG,
	SUN60IW2_PLL_CPU_L_REG,
	SUN60IW2_PLL_CPU_B_REG,
	SUN60IW2_PLL_CPU_DSU_REG,
};

static const u32 sun60i_pll_cpu_pat0_regs[] = {
	SUN60IW2_PLL_CPU_BACK_PAT0_REG,
	SUN60IW2_PLL_CPU_L_PAT0_REG,
	SUN60IW2_PLL_CPU_B_PAT0_REG,
	SUN60IW2_PLL_CPU_DSU_PAT0_REG,
};

static const u32 sun60i_pll_cpu_pat1_regs[] = {
	SUN60IW2_PLL_CPU_BACK_PAT1_REG,
	SUN60IW2_PLL_CPU_L_PAT1_REG,
	SUN60IW2_PLL_CPU_B_PAT1_REG,
	SUN60IW2_PLL_CPU_DSU_PAT1_REG,
};

static const u32 sun60i_pll_lfm_regs[] = {
	SUN60IW2_PLL_CPU_L_LFM_REG,
	SUN60IW2_PLL_CPU_B_LFM_REG,
	SUN60IW2_PLL_CPU_DSU_LFM_REG,
};

static void sun60i_cpupll_wait_for_lock(void __iomem *addr, u32 lock)
{
	u32 reg;

	WARN_ON(readl_relaxed_poll_timeout(addr, reg, reg & lock, 100, 70000));
}

static void sun60i_cpupll_wait_for_clear(void __iomem *addr, u32 clear)
{
	u32 reg;

	reg = readl(addr);
	writel(reg | clear, addr);
	WARN_ON(readl_relaxed_poll_timeout_atomic(addr, reg, !(reg & clear),
						  100, 10000));
}

static void sun60i_cpupll_adapt_vco(struct device *dev, unsigned long dcxo_rate)
{
	if (dcxo_rate == 19200000) {
		pll_cpu_back_clk.n.min = 66;
		pll_cpu_back_clk.n.max = 131;
		pll_cpu_l_clk.n.min = 35;
		pll_cpu_l_clk.n.max = 117;
		pll_cpu_b_clk.n.min = 35;
		pll_cpu_b_clk.n.max = 117;
		pll_cpu_dsu_clk.n.min = 35;
		pll_cpu_dsu_clk.n.max = 117;
	} else if (dcxo_rate == 26000000) {
		pll_cpu_back_clk.n.min = 49;
		pll_cpu_back_clk.n.max = 96;
		pll_cpu_l_clk.n.min = 26;
		pll_cpu_l_clk.n.max = 84;
		pll_cpu_b_clk.n.min = 26;
		pll_cpu_b_clk.n.max = 86;
		pll_cpu_dsu_clk.n.min = 26;
		pll_cpu_dsu_clk.n.max = 86;
	} else if (dcxo_rate != 24000000) {
		dev_warn(dev, "unexpected dcxo rate %lu, using defaults\n",
			 dcxo_rate);
	}
}

static int sun60i_a733_cpupll_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *reg;
	struct clk *dcxo;
	unsigned long dcxo_rate;
	u32 val;
	int i, ret;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	dcxo = devm_clk_get(dev, "dcxo");
	if (IS_ERR(dcxo))
		return dev_err_probe(dev, PTR_ERR(dcxo), "failed to get dcxo\n");

	dcxo_rate = clk_get_rate(dcxo);
	sun60i_cpupll_adapt_vco(dev, dcxo_rate);

	/*
	 * Enable LFM so PLL rate changes can ramp while CPUs keep running
	 * on the same PLL (BSP behaviour). Pair with proper cpu-supply.
	 */
	for (i = 0; i < ARRAY_SIZE(sun60i_pll_cpu_regs); i++) {
		val = readl(reg + sun60i_pll_cpu_pat0_regs[i]);
		val |= BIT(29) | BIT(30);
		writel(val, reg + sun60i_pll_cpu_pat0_regs[i]);

		val = readl(reg + sun60i_pll_cpu_pat1_regs[i]);
		val |= BIT(31);
		writel(val, reg + sun60i_pll_cpu_pat1_regs[i]);

		if (i) {
			val = readl(reg + sun60i_pll_lfm_regs[i - 1]);
			val &= ~BIT(0);
			val |= BIT(31) | BIT(8);
			writel(val, reg + sun60i_pll_lfm_regs[i - 1]);
		}

		sun60i_cpupll_wait_for_clear(reg + sun60i_pll_cpu_regs[i], BIT(26));

		val = readl(reg + sun60i_pll_cpu_regs[i]);
		val |= BIT(27) | BIT(29) | BIT(30) | BIT(31);
		writel(val, reg + sun60i_pll_cpu_regs[i]);
		sun60i_cpupll_wait_for_clear(reg + sun60i_pll_cpu_regs[i], BIT(26));
		sun60i_cpupll_wait_for_lock(reg + sun60i_pll_cpu_regs[i], BIT(28));
	}

	sun60i_set_reg(reg + SUN60IW2_CPU_L_REG, 0x3, 3, 24);
	sun60i_set_reg(reg + SUN60IW2_CPU_B_REG, 0x3, 3, 24);
	sun60i_set_reg(reg + SUN60IW2_CPU_DSU_REG, 0x3, 3, 24);

	ret = devm_sunxi_ccu_probe(dev, reg, &sun60i_cpupll_desc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register cpupll CCU\n");

	return 0;
}

static const struct of_device_id sun60i_a733_cpupll_ids[] = {
	{ .compatible = "allwinner,sun60iw2-cpupll" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun60i_a733_cpupll_ids);

static struct platform_driver sun60i_a733_cpupll_driver = {
	.probe	= sun60i_a733_cpupll_probe,
	.driver	= {
		.name		= "sun60i-a733-cpupll",
		.of_match_table	= sun60i_a733_cpupll_ids,
	},
};
module_platform_driver(sun60i_a733_cpupll_driver);

MODULE_DESCRIPTION("Allwinner A733 CPU PLL clock driver");
MODULE_AUTHOR("Allwinner Technology Co.,Ltd.");
MODULE_LICENSE("GPL");
