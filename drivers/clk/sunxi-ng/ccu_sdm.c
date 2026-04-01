// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Chen-Yu Tsai <wens@csie.org>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "ccu_sdm.h"

#if 0
u32 ccu_get_sdmval(unsigned long rate, struct ccu_common *common, u32 n)
{
	u32 sdm_val, sdm_freq, step_value;
	u64 x2, wave_step;
	struct clk_sdm_info *sdm_info = common->sdm_info;

	sdm_freq = 315 + sdm_info->sdm_freq * 5;

	x2 = sdm_info->sdm_factor * n;
	if (x2 >= 1000) {
		sunxi_err(NULL, "clk: invalid sdm_factor: %d\n", sdm_info->sdm_factor);
		return -1;
	}
	/*
	 *      SDM_CLK_SEL->24M
	 *      fix coefficient=2^17*2/24 = 10922.5
	 **/
	wave_step = 109225 * x2 * sdm_freq;

	do_div(wave_step, 100000000);
	step_value = (u32)wave_step;

	sdm_val = (wave_step << 20);
	/* enanle SDM */
	sdm_val = SET_BITS(31, 1, sdm_val, 1);
	/* choose freq_mode */
	sdm_val = SET_BITS(29, 2, sdm_val, sdm_info->freq_mode);

	/* choose sdm_freq */
	sdm_val = SET_BITS(17, 2, sdm_val, sdm_info->sdm_freq);
	/* Some platforms support the configuration of sdm_direction, and sdm_direction and sdm_clk share configuration bit */
	if (sdm_info->sdm_direction != SDM_DIR_NONE)
		sdm_val = SET_BITS(19, 1, sdm_val, sdm_info->sdm_direction);
	else
		sdm_val = SET_BITS(19, 1, sdm_val, 0);

	sunxi_debug(NULL, "sdm_val: 0x%x wave_step: %llu, sdm_freq: %d freq_mode: %d sdm_dir: %d\n", sdm_val, wave_step, sdm_info->sdm_freq, sdm_info->freq_mode, sdm_info->sdm_direction);

	return sdm_val;
}

void ccu_common_set_sdm_value(struct ccu_common *common, struct ccu_sdm_internal *sdm, u32 sdmval)
{
	if (sdm->enable)
		set_bits(common->base + common->reg, sdm->enable);

	set_field(common->base + sdm->tuning_reg, BITS_WIDTH(0, 32), sdmval);

	if (sdm->pattern1_reg)
		set_field(common->base + sdm->pattern1_reg, BITS_WIDTH(0, 32), sdm->pattern1_enable);
}
#endif

bool ccu_sdm_helper_is_enabled(struct ccu_common *common,
			       struct ccu_sdm_internal *sdm)
{
	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return false;

	if (sdm->enable && !(readl(common->base + common->reg) & sdm->enable))
		return false;

	if (sdm->pattern1_reg) {
		if (((readl(common->base + sdm->pattern1_reg) & sdm->pattern1_enable)) != sdm->pattern1_enable)
			return false;
	}

	return !!(readl(common->base + sdm->tuning_reg) & sdm->tuning_enable);
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_is_enabled, "SUNXI_CCU");

void ccu_sdm_helper_enable(struct ccu_common *common,
			   struct ccu_sdm_internal *sdm,
			   unsigned long rate)
{
	unsigned long flags;
	unsigned int i;
	u32 reg;

	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return;

	/* Set the pattern */
	for (i = 0; i < sdm->table_size; i++)
		if (sdm->table[i].rate == rate)
			writel(sdm->table[i].pattern,
			       common->base + sdm->tuning_reg);

	/* Make sure SDM is enabled */
	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + sdm->tuning_reg);
	writel(reg | sdm->tuning_enable, common->base + sdm->tuning_reg);
	if (sdm->pattern1_reg) {
		reg = readl(common->base + sdm->pattern1_reg);
		writel(reg | sdm->pattern1_enable, common->base + sdm->pattern1_reg);
	}
	spin_unlock_irqrestore(common->lock, flags);

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + common->reg);
	writel(reg | sdm->enable, common->base + common->reg);
	spin_unlock_irqrestore(common->lock, flags);
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_enable, "SUNXI_CCU");

void ccu_sdm_helper_disable(struct ccu_common *common,
			    struct ccu_sdm_internal *sdm)
{
	unsigned long flags;
	u32 reg;

	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return;

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + common->reg);
	writel(reg & ~sdm->enable, common->base + common->reg);
	spin_unlock_irqrestore(common->lock, flags);

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + sdm->tuning_reg);
	writel(reg & ~sdm->tuning_enable, common->base + sdm->tuning_reg);
	if (sdm->pattern1_reg) {
		reg = readl(common->base + sdm->pattern1_reg);
		writel(reg & ~sdm->pattern1_enable, common->base + sdm->pattern1_reg);
	}
	spin_unlock_irqrestore(common->lock, flags);
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_disable, "SUNXI_CCU");

/*
 * Sigma delta modulation provides a way to do fractional-N frequency
 * synthesis, in essence allowing the PLL to output any frequency
 * within its operational range. On earlier SoCs such as the A10/A20,
 * some PLLs support this. On later SoCs, all PLLs support this.
 *
 * The datasheets do not explain what the "wave top" and "wave bottom"
 * parameters mean or do, nor how to calculate the effective output
 * frequency. The only examples (and real world usage) are for the audio
 * PLL to generate 24.576 and 22.5792 MHz clock rates used by the audio
 * peripherals. The author lacks the underlying domain knowledge to
 * pursue this.
 *
 * The goal and function of the following code is to support the two
 * clock rates used by the audio subsystem, allowing for proper audio
 * playback and capture without any pitch or speed changes.
 */
bool ccu_sdm_helper_has_rate(struct ccu_common *common,
			     struct ccu_sdm_internal *sdm,
			     unsigned long rate)
{
	unsigned int i;

	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return false;

	for (i = 0; i < sdm->table_size; i++)
		if (sdm->table[i].rate == rate)
			return true;

	return false;
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_has_rate, "SUNXI_CCU");

unsigned long ccu_sdm_helper_read_rate(struct ccu_common *common,
				       struct ccu_sdm_internal *sdm,
				       u32 m, u32 n)
{
	unsigned int i;
	u32 reg;

	pr_debug("%s: Read sigma-delta modulation setting\n",
		 clk_hw_get_name(&common->hw));

	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return 0;

	pr_debug("%s: clock is sigma-delta modulated\n",
		 clk_hw_get_name(&common->hw));

	reg = readl(common->base + sdm->tuning_reg);

	pr_debug("%s: pattern reg is 0x%x",
		 clk_hw_get_name(&common->hw), reg);

	for (i = 0; i < sdm->table_size; i++)
		if (sdm->table[i].pattern == reg &&
		    sdm->table[i].m == m && sdm->table[i].n == n)
			return sdm->table[i].rate;

	/* We can't calculate the effective clock rate, so just fail. */
	return 0;
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_read_rate, "SUNXI_CCU");

int ccu_sdm_helper_get_factors(struct ccu_common *common,
			       struct ccu_sdm_internal *sdm,
			       unsigned long rate,
			       unsigned long *m, unsigned long *n)
{
	unsigned int i;

	if (!(common->features & CCU_FEATURE_SIGMA_DELTA_MOD))
		return -EINVAL;

	for (i = 0; i < sdm->table_size; i++)
		if (sdm->table[i].rate == rate) {
			*m = sdm->table[i].m;
			*n = sdm->table[i].n;
			return 0;
		}

	/* nothing found */
	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(ccu_sdm_helper_get_factors, "SUNXI_CCU");
