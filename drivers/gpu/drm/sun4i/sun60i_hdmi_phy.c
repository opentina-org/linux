// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * sun60i / A733 DW HDMI PHY glue (Top-PHY V2 + SNPS internal).
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>

#include "sun60i_hdmi.h"

#define SUN60I_DW_HDMI_FC_GCP			0x1018
#define SUN60I_DW_HDMI_FC_GCP_CLEAR_AVMUTE	BIT(0)
#define SUN60I_DW_HDMI_PHY_CONF0		0x3000
#define SUN60I_DW_HDMI_PHY_CONF0_ENTMDS		BIT(6)

void sun60i_hdmi_clear_avmute(struct sun60i_hdmi *hdmi)
{
	u8 gcp;

	if (!hdmi->regs)
		return;

	gcp = readb(hdmi->regs + SUN60I_DW_HDMI_FC_GCP);
	gcp &= ~BIT(1);
	gcp |= SUN60I_DW_HDMI_FC_GCP_CLEAR_AVMUTE;
	writeb(gcp, hdmi->regs + SUN60I_DW_HDMI_FC_GCP);
}

static int sun60i_hdmi_phy_init(struct dw_hdmi *dw_hdmi, void *data,
				const struct drm_display_info *display,
				const struct drm_display_mode *mode)
{
	struct sun60i_hdmi *hdmi = data;
	int ret;

	ret = sun60i_hdmi_top_phy_config(hdmi, mode);
	if (ret)
		return ret;

	ret = sun60i_hdmi_snps_phy_configure(dw_hdmi, hdmi, mode);
	if (ret)
		return ret;

	mdelay(50);
	sun60i_hdmi_clear_avmute(hdmi);
	sun60i_hdmi_snps_phy_enable_tmds(hdmi);
	sun60i_hdmi_apply_video_fixup(hdmi);

	return 0;
}

static void sun60i_hdmi_phy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct sun60i_hdmi *hdmi = data;

	sun60i_hdmi_snps_phy_disable(dw_hdmi, hdmi);
	sun60i_hdmi_top_phy_disable(hdmi);
}

static const struct dw_hdmi_phy_ops sun60i_hdmi_phy_ops = {
	.init = sun60i_hdmi_phy_init,
	.disable = sun60i_hdmi_phy_disable,
	.setup_hpd = dw_hdmi_phy_setup_hpd,
	.read_hpd = dw_hdmi_phy_read_hpd,
	.update_hpd = dw_hdmi_phy_update_hpd,
};

const struct dw_hdmi_phy_ops *sun60i_hdmi_phy_ops_get(void)
{
	return &sun60i_hdmi_phy_ops;
}

void sun60i_hdmi_apply_video_fixup(struct sun60i_hdmi *hdmi)
{
	u8 invid0, invconf, phy;

	if (!hdmi->regs)
		return;

	invid0 = readb(hdmi->regs + 0x0200);
	invid0 &= ~BIT(7);
	writeb(invid0, hdmi->regs + 0x0200);

	invconf = readb(hdmi->regs + 0x1000);
	invconf |= BIT(3);
	writeb(invconf, hdmi->regs + 0x1000);

	phy = readb(hdmi->regs + SUN60I_DW_HDMI_PHY_CONF0);
	phy |= SUN60I_DW_HDMI_PHY_CONF0_ENTMDS;
	writeb(phy, hdmi->regs + SUN60I_DW_HDMI_PHY_CONF0);

	sun60i_hdmi_clear_avmute(hdmi);
}
