// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * sun60i top-PHY helper aligned with BSP phy_top.c init/config sequence.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/types.h>

#include "sun60i_hdmi.h"

/* DW-HDMI main controller register (byte); not in public dw_hdmi.h UAPI */
#define SUN60I_DW_HDMI_MC_CLKDIS		0x4001
#define SUN60I_DW_HDMI_FC_INVIDCONF		0x1000
#define SUN60I_DW_HDMI_FC_AVICONF0		0x1005
#define SUN60I_DW_HDMI_FC_GCP			0x1018
#define SUN60I_DW_HDMI_MC_FLOWCTRL		0x4004

#define SUN60I_TOP_PHY_REG0000		0x0000
#define SUN60I_TOP_PHY_REG0004		0x0004
#define SUN60I_TOP_PHY_REG0010		0x0010
#define SUN60I_TOP_PHY_REG0020		0x0020
#define SUN60I_TOP_PHY_REG0024		0x0024
#define SUN60I_TOP_PHY_REG0028		0x0028
#define SUN60I_TOP_PHY_REG002c		0x002c
#define SUN60I_TOP_PHY_REG0030		0x0030
#define SUN60I_TOP_PHY_REG0040		0x0040

#define SUN60I_RESCAL_RES0_OFFSET		0x4

/* BSP top_phy_power(ON): reset, pddq, txpwron, enhpdrxsense */
#define SUN60I_TOP_PHY_PWR_ON		(BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define SUN60I_TOP_PHY_TXPWRON		BIT(2)
#define SUN60I_TOP_PHY_TXREADY		BIT(0)
#define SUN60I_TOP_PHY_PLL_OUT_GATE	BIT(27)
#define SUN60I_TOP_PHY_LOCK_ENABLE	BIT(29)
#define SUN60I_TOP_PHY_PLL_LDO_EN	BIT(30)
#define SUN60I_TOP_PHY_PLL_EN		BIT(31)
#define SUN60I_TOP_PHY_LEVEL_SHIFTER	BIT(7)
#define SUN60I_TOP_PHY_HDMI_OUTCLK_SEL	BIT(8)
#define SUN60I_TOP_PHY_PLL_LOCKED	BIT(0)

#define SUN60I_RESCAL_HDMI_RES_SEL	BIT(12)
#define SUN60I_RESCAL_MODE		BIT(2)
#define SUN60I_RESCAL_ANA_EN		BIT(1)
#define SUN60I_RESCAL_EN		BIT(0)
#define SUN60I_RESCAL_RES0_MASK		GENMASK(31, 24)

#define SUN60I_PLLVCO_MIN_KHZ		1260000
#define SUN60I_PLLVCO_MAX_KHZ		2250000
#define SUN60I_PLL_AUTO_MAX_RESULT	5

struct sun60i_top_phy_pll {
	unsigned long pixel_clock;
	u32 pll_value;
	u32 ldo_value;
	u32 pll_pattern0;
	u32 pll_pattern1;
};

struct sun60i_top_phy_pll_cal {
	u32 pll_n;
	u32 pll_m;
	u32 pll_p;
	u64 diff;
};

static const struct sun60i_top_phy_pll sun60i_top_phy_24m[] = {
	{ 13500,  0xe85f3500, 0x00035000, 0x00000000, 0x30000000 },
	{ 27000,  0xe8576200, 0x00035000, 0x80000000, 0x30000000 },
	{ 54000,  0xe82b6200, 0x00035000, 0x80000000, 0x30000000 },
	{ 65000,  0xe8246200, 0x00035000, 0x80000000, 0x30000000 },
	{ 74250,  0xe81f6200, 0x00035000, 0x80000000, 0x30000000 },
	{ 148500, 0xe80f6200, 0x00035000, 0x80000000, 0x30000000 },
	{ 185625, 0xe80f6200, 0x00035000, 0x80000000, 0x30000000 },
	{ 297000, 0xe8076200, 0x00035000, 0x00000000, 0x30000000 },
	{ 371250, 0xe8076200, 0x00035000, 0x00000000, 0x30000000 },
	{ 594000, 0xe8036200, 0x00035000, 0x00000000, 0x30000000 },
};

static const struct sun60i_top_phy_pll sun60i_top_phy_26m[] = {
	{ 13500,  0xe8673500, 0x00035000, 0x00000000, 0x30000000 },
	{ 27000,  0xe8595c00, 0x00035000, 0x80000000, 0x30000000 },
	{ 54000,  0xe80c1a00, 0x00035000, 0x80000000, 0x30000000 },
	{ 65000,  0xe8235a00, 0x00035000, 0x80000000, 0x30000000 },
	{ 74250,  0xe81f5a00, 0x00035000, 0x80000000, 0x30000000 },
	{ 108000, 0xe80c3500, 0x00035000, 0x00000000, 0x30000000 },
	{ 148500, 0xe80f5a00, 0x00035000, 0x80000000, 0x30000000 },
	{ 185625, 0xe80f5a00, 0x00035000, 0x80000000, 0x30000000 },
	{ 297000, 0xe807b602, 0x00035000, 0x00000000, 0x30000000 },
	{ 371250, 0xe807b602, 0x00035000, 0x00000000, 0x30000000 },
	{ 594000, 0xe803b602, 0x00035000, 0x00000000, 0x30000000 },
};

static inline void sun60i_top_phy_write(struct sun60i_hdmi *hdmi,
					u32 reg, u32 value)
{
	writel(value, hdmi->regs + SUN60I_HDMI_TOP_PHY_OFFSET + reg);
}

static inline u32 sun60i_top_phy_read(struct sun60i_hdmi *hdmi, u32 reg)
{
	return readl(hdmi->regs + SUN60I_HDMI_TOP_PHY_OFFSET + reg);
}

static unsigned long sun60i_top_phy_diff(unsigned long a, unsigned long b)
{
	return a > b ? a - b : b - a;
}

static void sun60i_top_phy_pll_set_output(struct sun60i_hdmi *hdmi, bool on)
{
	u32 value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);

	if (on)
		value |= SUN60I_TOP_PHY_PLL_OUT_GATE;
	else
		value &= ~SUN60I_TOP_PHY_PLL_OUT_GATE;
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);
}

static void sun60i_top_phy_set_clock_select(struct sun60i_hdmi *hdmi, u8 sel)
{
	u32 value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0024);

	if (sel)
		value |= SUN60I_TOP_PHY_HDMI_OUTCLK_SEL;
	else
		value &= ~SUN60I_TOP_PHY_HDMI_OUTCLK_SEL;
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0024, value);
}

static void sun60i_top_phy_apply_pll_fields(struct sun60i_hdmi *hdmi,
					    u32 pll_value)
{
	u32 value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);

	value &= ~(BIT(1) | GENMASK(7, 5) | GENMASK(15, 8) | GENMASK(22, 16));
	value |= (pll_value & 0x00000002);
	value |= (pll_value & 0x00000020);
	value |= (pll_value & 0x000000c0);
	value |= (pll_value & 0x0000ff00);
	value |= (pll_value & 0x007f0000);
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);
}

static int sun60i_top_phy_pll_auto_cal(unsigned long dcxo_khz,
				       unsigned long pixel_clock,
				       struct sun60i_top_phy_pll *cfg)
{
	struct sun60i_top_phy_pll_cal temp[SUN60I_PLL_AUTO_MAX_RESULT];
	struct sun60i_top_phy_pll_cal result = {};
	unsigned long min_diff = ULONG_MAX;
	unsigned long dcxo_hz = dcxo_khz;
	unsigned long target_clk = pixel_clock;
	unsigned int temp_count = 0;
	bool has_m1 = false, has_result = false;
	unsigned long temp_vco_max = 0;
	u16 temp_m, temp_n, temp_p;
	unsigned int i;

	for (temp_m = 1; temp_m <= 2; temp_m++) {
		for (temp_n = 1; temp_n <= 255; temp_n++) {
			unsigned long temp_vco = (dcxo_hz * temp_n) / temp_m;

			if (temp_vco < SUN60I_PLLVCO_MIN_KHZ ||
			    temp_vco > SUN60I_PLLVCO_MAX_KHZ)
				continue;

			for (temp_p = 1; temp_p <= 128; temp_p++) {
				unsigned long temp_clk =
					(dcxo_hz * temp_n) / (temp_m * temp_p);
				unsigned long temp_diff =
					sun60i_top_phy_diff(temp_clk, target_clk);

				if (temp_diff < min_diff)
					min_diff = temp_diff;
			}
		}
	}

	for (temp_m = 1; temp_m <= 2; temp_m++) {
		for (temp_n = 1; temp_n <= 255; temp_n++) {
			unsigned long temp_vco = (dcxo_hz * temp_n) / temp_m;

			if (temp_vco < SUN60I_PLLVCO_MIN_KHZ ||
			    temp_vco > SUN60I_PLLVCO_MAX_KHZ)
				continue;

			for (temp_p = 1; temp_p <= 128; temp_p++) {
				unsigned long temp_clk =
					(dcxo_hz * temp_n) / (temp_m * temp_p);
				unsigned long temp_diff =
					sun60i_top_phy_diff(temp_clk, target_clk);

				if (temp_diff == min_diff &&
				    temp_count < SUN60I_PLL_AUTO_MAX_RESULT) {
					temp[temp_count].pll_n = temp_n;
					temp[temp_count].pll_m = temp_m;
					temp[temp_count].pll_p = temp_p;
					temp[temp_count].diff = temp_diff;
					temp_count++;
				}
			}
		}
	}

	if (!temp_count)
		return -EINVAL;

	for (i = 0; i < temp_count; i++) {
		if (temp[i].pll_m == 1) {
			has_m1 = true;
			break;
		}
	}

	for (i = 0; i < temp_count; i++) {
		unsigned long temp_vco;

		if (has_m1 && temp[i].pll_m != 1)
			continue;

		temp_vco = dcxo_hz * temp[i].pll_n;
		if (temp_vco > temp_vco_max) {
			temp_vco_max = temp_vco;
			result = temp[i];
			has_result = true;
		}
	}

	if (!has_result)
		return -EINVAL;

	cfg->pixel_clock = pixel_clock;
	cfg->pll_value = 0xe8000000;
	cfg->pll_value |= (result.pll_m - 1) << 1;
	cfg->pll_value |= (result.pll_n - 1) << 8;
	cfg->pll_value |= (result.pll_p - 1) << 16;
	cfg->ldo_value = 0x00035000;
	cfg->pll_pattern0 = 0x00000000;
	cfg->pll_pattern1 = 0x30000000;
	return 0;
}

static int sun60i_top_phy_lookup_pll(const struct sun60i_top_phy_pll *table,
				     size_t count, unsigned long dcxo_khz,
				     unsigned long pixel_clock,
				     struct sun60i_top_phy_pll *cfg)
{
	unsigned long tolerance;
	size_t i;

	for (i = 0; i < count; i++) {
		tolerance = pixel_clock / 1000;
		if (!tolerance)
			tolerance = 1;
		if (sun60i_top_phy_diff(pixel_clock, table[i].pixel_clock) <=
		    tolerance) {
			*cfg = table[i];
			return 0;
		}
	}

	if (!sun60i_top_phy_pll_auto_cal(dcxo_khz, pixel_clock, cfg))
		return 0;

	for (i = 0; i + 1 < count; i++) {
		if (pixel_clock > table[i].pixel_clock &&
		    pixel_clock < table[i + 1].pixel_clock) {
			if ((table[i + 1].pixel_clock - pixel_clock) >
			    (pixel_clock - table[i].pixel_clock))
				*cfg = table[i];
			else
				*cfg = table[i + 1];
			return 0;
		}
	}

	*cfg = table[0];
	for (i = 1; i < count; i++) {
		if (sun60i_top_phy_diff(pixel_clock, table[i].pixel_clock) <
		    sun60i_top_phy_diff(pixel_clock, cfg->pixel_clock))
			*cfg = table[i];
	}

	return 0;
}

bool sun60i_hdmi_top_phy_wait_txready(struct sun60i_hdmi *hdmi,
				      unsigned int ms)
{
	unsigned int i;

	if (!hdmi->regs)
		return false;

	for (i = 0; i < ms; i++) {
		u32 stat = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0010);

		if (stat & SUN60I_TOP_PHY_TXREADY)
			return true;
		udelay(1000);
	}

	return false;
}

int sun60i_hdmi_rescal_board_init(struct sun60i_hdmi *hdmi)
{
	u32 value;

	if (!hdmi->rescal_regs)
		return 0;

	if (hdmi->resistor_src_onboard) {
		value = readl(hdmi->rescal_regs);
		value |= SUN60I_RESCAL_HDMI_RES_SEL;
		writel(value, hdmi->rescal_regs);

		value = readl(hdmi->rescal_regs + SUN60I_RESCAL_RES0_OFFSET);
		value &= ~SUN60I_RESCAL_RES0_MASK;
		writel(value, hdmi->rescal_regs + SUN60I_RESCAL_RES0_OFFSET);
		return 0;
	}

	value = readl(hdmi->rescal_regs);
	value &= ~SUN60I_RESCAL_EN;
	writel(value, hdmi->rescal_regs);

	value &= ~SUN60I_RESCAL_HDMI_RES_SEL;
	writel(value, hdmi->rescal_regs);

	value |= SUN60I_RESCAL_ANA_EN;
	writel(value, hdmi->rescal_regs);

	value |= SUN60I_RESCAL_EN;
	writel(value, hdmi->rescal_regs);

	udelay(200);

	value &= ~SUN60I_RESCAL_ANA_EN;
	writel(value, hdmi->rescal_regs);

	value &= ~SUN60I_RESCAL_EN;
	writel(value, hdmi->rescal_regs);

	return 0;
}

int sun60i_hdmi_top_phy_init(struct sun60i_hdmi *hdmi)
{
	u32 value;

	if (!hdmi->regs)
		return -ENODEV;

	if (hdmi->top_phy_inited)
		return 0;

	/* BSP: top_phy_power(ON) */
	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0000);
	value |= SUN60I_TOP_PHY_PWR_ON;
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0000, value);

	/* Route TMDS to HDMI pads (not GPIO). */
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0004, 0);

	/* BSP init: outgate off, then clock source select. */
	sun60i_top_phy_pll_set_output(hdmi, false);
	sun60i_top_phy_set_clock_select(hdmi, hdmi->clock_src);

	hdmi->top_phy_inited = true;
	return 0;
}

int sun60i_hdmi_top_phy_config(struct sun60i_hdmi *hdmi,
			       const struct drm_display_mode *mode)
{
	const struct sun60i_top_phy_pll *table;
	struct sun60i_top_phy_pll cfg = {};
	size_t table_count;
	unsigned long dcxo_rate, dcxo_khz, pixel_clock;
	u32 value;
	int i, ret;

	if (!hdmi->clk_dcxo || !hdmi->regs)
		return 0;

	/* BSP top_phy_config(): disable output gate first. */
	sun60i_top_phy_pll_set_output(hdmi, false);

	if (hdmi->clock_src) {
		dev_dbg(hdmi->dev, "top-phy: CCMU clock source, skip PLL config\n");
		return 0;
	}

	dcxo_rate = clk_get_rate(hdmi->clk_dcxo);
	switch (dcxo_rate) {
	case 24000000:
		table = sun60i_top_phy_24m;
		table_count = ARRAY_SIZE(sun60i_top_phy_24m);
		break;
	case 26000000:
		table = sun60i_top_phy_26m;
		table_count = ARRAY_SIZE(sun60i_top_phy_26m);
		break;
	default:
		dev_warn(hdmi->dev, "unsupported dcxo clock %luHz for top-phy\n",
			 dcxo_rate);
		return -EINVAL;
	}

	dcxo_khz = dcxo_rate / 1000;
	pixel_clock = mode->clock * (hdmi->pixel_repeat + 1);

	ret = sun60i_top_phy_lookup_pll(table, table_count, dcxo_khz,
					pixel_clock, &cfg);
	if (ret)
		return ret;

	sun60i_top_phy_apply_pll_fields(hdmi, cfg.pll_value);
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0028, cfg.ldo_value);
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG002c, cfg.pll_pattern0);
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0030, cfg.pll_pattern1);

	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);
	if (!(value & SUN60I_TOP_PHY_PLL_EN)) {
		value |= SUN60I_TOP_PHY_PLL_EN;
		sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);
	}

	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);
	if (!(value & SUN60I_TOP_PHY_PLL_LDO_EN)) {
		value |= SUN60I_TOP_PHY_PLL_LDO_EN;
		sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);
	}

	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0024);
	value |= SUN60I_TOP_PHY_LEVEL_SHIFTER;
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0024, value);

	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);
	value |= SUN60I_TOP_PHY_LOCK_ENABLE;
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);

	for (i = 0; i < 200; i++) {
		if (sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0040) &
		    SUN60I_TOP_PHY_PLL_LOCKED)
			break;
		udelay(100);
	}

	if (i == 200) {
		dev_warn(hdmi->dev, "top-phy lock timeout for %lu kHz mode\n",
			 pixel_clock);
		return -ETIMEDOUT;
	}

	udelay(20);
	sun60i_top_phy_pll_set_output(hdmi, true);

	return 0;
}

void sun60i_hdmi_top_phy_disable(struct sun60i_hdmi *hdmi)
{
	u32 value;

	if (!hdmi->regs)
		return;

	sun60i_top_phy_pll_set_output(hdmi, false);

	value = sun60i_top_phy_read(hdmi, SUN60I_TOP_PHY_REG0020);
	value &= ~(SUN60I_TOP_PHY_PLL_EN | SUN60I_TOP_PHY_PLL_LDO_EN |
		   SUN60I_TOP_PHY_LOCK_ENABLE);
	sun60i_top_phy_write(hdmi, SUN60I_TOP_PHY_REG0020, value);
}
