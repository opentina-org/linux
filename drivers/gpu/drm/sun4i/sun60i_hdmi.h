/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */

#ifndef _SUN60I_HDMI_H_
#define _SUN60I_HDMI_H_

#include <linux/clk.h>
#include <linux/reset.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>

#define SUN60I_HDMI_TOP_PHY_OFFSET	0x000e0000
#define SUN60I_HDMI_RESCAL_PHYS		0x03000160
#define SUN60I_HDMI_RESCAL_SIZE		0x8

struct sun60i_hdmi {
	struct device *dev;
	void __iomem *regs;
	void __iomem *rescal_regs;
	struct clk *clk_cec;
	struct clk *clk_tcon_tv;
	struct clk *clk_hdmi;
	struct clk *clk_hdmi_24m;
	struct clk *clk_hdcp;
	struct clk *clk_dcxo;
	/* Glue-layer handles used to clock register accesses before dw-hdmi binds. */
	struct clk *clk_isfr;
	struct clk *clk_iahb;
	struct reset_control *rst_bus_sub;
	struct reset_control *rst_bus_main;
	struct reset_control *rst_bus_hdcp;
	bool top_phy_inited;
	bool resistor_src_onboard;
	u8 clock_src;
	u8 pixel_repeat;
	int irq;
	bool irq_snps_masked;
};

int sun60i_hdmi_rescal_board_init(struct sun60i_hdmi *hdmi);
int sun60i_hdmi_top_phy_init(struct sun60i_hdmi *hdmi);
int sun60i_hdmi_top_phy_config(struct sun60i_hdmi *hdmi,
			       const struct drm_display_mode *mode);
void sun60i_hdmi_top_phy_disable(struct sun60i_hdmi *hdmi);
bool sun60i_hdmi_top_phy_wait_txready(struct sun60i_hdmi *hdmi,
				      unsigned int ms);

int sun60i_hdmi_snps_phy_configure(struct dw_hdmi *dw_hdmi,
				   struct sun60i_hdmi *hdmi,
				   const struct drm_display_mode *mode);
void sun60i_hdmi_snps_phy_disable(struct dw_hdmi *dw_hdmi,
				  struct sun60i_hdmi *hdmi);
void sun60i_hdmi_snps_phy_enable_tmds(struct sun60i_hdmi *hdmi);
void sun60i_hdmi_snps_phy_probe_init(struct dw_hdmi *dw_hdmi,
				     struct sun60i_hdmi *hdmi);

void sun60i_hdmi_apply_video_fixup(struct sun60i_hdmi *hdmi);
void sun60i_hdmi_clear_avmute(struct sun60i_hdmi *hdmi);

const struct dw_hdmi_phy_ops *sun60i_hdmi_phy_ops_get(void);

#endif /* _SUN60I_HDMI_H_ */
