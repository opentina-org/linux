// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "sun8i_dw_hdmi.h"
#include "sun8i_tcon_top.h"
#include "sun60i_hdmi.h"

static void sun8i_dw_hdmi_encoder_mode_set(struct drm_encoder *encoder,
					   struct drm_display_mode *mode,
					   struct drm_display_mode *adj_mode)
{
	struct sun8i_dw_hdmi *hdmi = encoder_to_sun8i_dw_hdmi(encoder);

	if (hdmi->quirks->integrated_phy) {
		if (hdmi->sun60i.clk_tcon_tv)
			clk_set_rate(hdmi->sun60i.clk_tcon_tv,
				     adj_mode->crtc_clock * 1000);
		if (hdmi->sun60i.clk_hdmi)
			clk_set_rate(hdmi->sun60i.clk_hdmi,
				     adj_mode->crtc_clock * 1000);
	} else {
		clk_set_rate(hdmi->clk_tmds, mode->crtc_clock * 1000);
	}
}

static const struct drm_encoder_helper_funcs
sun8i_dw_hdmi_encoder_helper_funcs = {
	.mode_set = sun8i_dw_hdmi_encoder_mode_set,
};

static enum drm_mode_status
sun8i_dw_hdmi_mode_valid_a83t(struct dw_hdmi *hdmi, void *data,
			      const struct drm_display_info *info,
			      const struct drm_display_mode *mode)
{
	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_mode_status
sun8i_dw_hdmi_mode_valid_a733(struct dw_hdmi *hdmi, void *data,
			      const struct drm_display_info *info,
			      const struct drm_display_mode *mode)
{
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int sun8i_dw_hdmi_tcon_top_gate(struct device *dev, bool enable)
{
	struct device_node *remote;
	struct platform_device *top_pdev;
	int ret = -ENODEV;

	remote = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!remote)
		return ret;

	top_pdev = of_find_device_by_node(remote);
	of_node_put(remote);
	if (!top_pdev)
		return ret;

	ret = sun8i_tcon_top_hdmi_gate_enable(&top_pdev->dev, enable);
	put_device(&top_pdev->dev);

	return ret;
}

static int sun8i_dw_hdmi_bind_sun60i(struct sun8i_dw_hdmi *hdmi,
				     struct platform_device *pdev,
				     struct drm_device *drm)
{
	struct device *dev = &pdev->dev;
	struct sun60i_hdmi *s60 = &hdmi->sun60i;
	struct resource *res;
	struct dw_hdmi_plat_data *plat_data = &hdmi->plat_data;
	int ret;

	s60->dev = dev;

	/*
	 * Share the HDMI MMIO window with dw_hdmi_bind(), which claims the
	 * platform resource via devm_ioremap_resource(). Use a non-exclusive
	 * map here so Top-PHY/SNPS helpers can reach +0xe0000 without -EBUSY.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	s60->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!s60->regs)
		return -ENOMEM;

	s60->rescal_regs = devm_ioremap(dev, SUN60I_HDMI_RESCAL_PHYS,
					SUN60I_HDMI_RESCAL_SIZE);

	s60->clk_cec = devm_clk_get_optional(dev, "clk_cec");
	if (IS_ERR(s60->clk_cec))
		return dev_err_probe(dev, PTR_ERR(s60->clk_cec),
				     "Couldn't get cec clock\n");

	s60->clk_tcon_tv = devm_clk_get_optional(dev, "clk_tcon_tv");
	if (IS_ERR(s60->clk_tcon_tv))
		return dev_err_probe(dev, PTR_ERR(s60->clk_tcon_tv),
				     "Couldn't get tcon_tv clock\n");

	s60->clk_hdmi = devm_clk_get(dev, "clk_hdmi");
	if (IS_ERR(s60->clk_hdmi))
		return dev_err_probe(dev, PTR_ERR(s60->clk_hdmi),
				     "Couldn't get hdmi clock\n");

	s60->clk_hdmi_24m = devm_clk_get_optional(dev, "clk_hdmi_24M");
	if (IS_ERR(s60->clk_hdmi_24m))
		return dev_err_probe(dev, PTR_ERR(s60->clk_hdmi_24m),
				     "Couldn't get hdmi 24M clock\n");

	s60->clk_hdcp = devm_clk_get_optional(dev, "clk_hdcp");
	if (IS_ERR(s60->clk_hdcp))
		return dev_err_probe(dev, PTR_ERR(s60->clk_hdcp),
				     "Couldn't get hdcp clock\n");

	s60->clk_dcxo = devm_clk_get_optional(dev, "clk_dcxo");
	if (IS_ERR(s60->clk_dcxo))
		return dev_err_probe(dev, PTR_ERR(s60->clk_dcxo),
				     "Couldn't get dcxo clock\n");

	s60->clk_isfr = devm_clk_get(dev, "isfr");
	if (IS_ERR(s60->clk_isfr))
		return dev_err_probe(dev, PTR_ERR(s60->clk_isfr),
				     "Couldn't get HDMI SFR clock\n");

	s60->clk_iahb = devm_clk_get(dev, "iahb");
	if (IS_ERR(s60->clk_iahb))
		return dev_err_probe(dev, PTR_ERR(s60->clk_iahb),
				     "Couldn't get HDMI AHB clock\n");

	s60->rst_bus_sub = devm_reset_control_get_optional(dev, "rst_bus_sub");
	if (IS_ERR(s60->rst_bus_sub))
		return dev_err_probe(dev, PTR_ERR(s60->rst_bus_sub),
				     "Couldn't get sub reset\n");

	s60->rst_bus_main = devm_reset_control_get(dev, "rst_bus_main");
	if (IS_ERR(s60->rst_bus_main))
		return dev_err_probe(dev, PTR_ERR(s60->rst_bus_main),
				     "Couldn't get main reset\n");

	s60->rst_bus_hdcp = devm_reset_control_get_optional(dev, "rst_bus_hdcp");
	if (IS_ERR(s60->rst_bus_hdcp))
		return dev_err_probe(dev, PTR_ERR(s60->rst_bus_hdcp),
				     "Couldn't get hdcp reset\n");

	hdmi->rst_ctrl = s60->rst_bus_main;

	/* BSP opens the TCON HDMI path before the HDMI clocks and resets. */
	ret = sun8i_dw_hdmi_tcon_top_gate(dev, true);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Couldn't enable TCON-top HDMI gate\n");

	/* Match BSP/opentina: dcxo + clk_tcon_tv before MAIN/SUB reset. */
	ret = clk_prepare_enable(s60->clk_dcxo);
	if (ret)
		goto err_disable_tcon_gate;

	ret = clk_prepare_enable(s60->clk_tcon_tv);
	if (ret)
		goto err_disable_dcxo;

	ret = reset_control_deassert(s60->rst_bus_main);
	if (ret)
		goto err_disable_tcon_tv;

	ret = reset_control_deassert(s60->rst_bus_sub);
	if (ret)
		goto err_assert_main;

	ret = clk_prepare_enable(s60->clk_hdmi_24m);
	if (ret)
		goto err_assert_sub;

	ret = clk_prepare_enable(s60->clk_hdmi);
	if (ret)
		goto err_disable_hdmi24;

	ret = clk_prepare_enable(s60->clk_cec);
	if (ret)
		goto err_disable_hdmi;

	s60->clock_src = s60->clk_dcxo &&
			 clk_get_rate(s60->clk_dcxo) > 25000000 ? 1 : 0;

	s60->irq = platform_get_irq(pdev, 0);
	if (s60->irq < 0) {
		ret = s60->irq;
		goto err_disable_cec;
	}

	/* Prefer the DT-provided DCXO as the HDMI SFR parent. */
	if (s60->clk_dcxo && clk_set_parent(s60->clk_isfr, s60->clk_dcxo))
		dev_warn(dev, "failed to set HDMI SFR clock parent\n");

	ret = clk_prepare_enable(s60->clk_isfr);
	if (ret)
		goto err_disable_cec;

	ret = clk_prepare_enable(s60->clk_iahb);
	if (ret)
		goto err_disable_isfr;

	ret = sun60i_hdmi_rescal_board_init(s60);
	if (ret)
		goto err_disable_iahb;

	ret = sun60i_hdmi_top_phy_init(s60);
	if (ret)
		goto err_disable_iahb;

	plat_data->mode_valid = hdmi->quirks->mode_valid;
	plat_data->use_drm_infoframe = hdmi->quirks->use_drm_infoframe;
	plat_data->hpd_debounce_ms = 100;
	plat_data->edid_retry_count = 5;
	plat_data->edid_retry_delay_ms = 100;
	plat_data->phy_ops = sun60i_hdmi_phy_ops_get();
	plat_data->phy_name = "sun60i_top_snps";
	plat_data->phy_data = s60;
	/* CONFIG2_ID reads as vendor PHY on A733; force glue-layer ops. */
	plat_data->phy_force_vendor = true;

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, &hdmi->encoder, plat_data);
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		goto err_disable_iahb;
	}

	/* dw-hdmi now owns its runtime isfr/iahb clock references. */
	clk_disable_unprepare(s60->clk_iahb);
	clk_disable_unprepare(s60->clk_isfr);

	sun60i_hdmi_snps_phy_probe_init(hdmi->hdmi, s60);

	return 0;

err_disable_iahb:
	clk_disable_unprepare(s60->clk_iahb);
err_disable_isfr:
	clk_disable_unprepare(s60->clk_isfr);
err_disable_cec:
	clk_disable_unprepare(s60->clk_cec);
err_disable_hdmi:
	clk_disable_unprepare(s60->clk_hdmi);
err_disable_hdmi24:
	clk_disable_unprepare(s60->clk_hdmi_24m);
err_assert_sub:
	reset_control_assert(s60->rst_bus_sub);
err_assert_main:
	reset_control_assert(s60->rst_bus_main);
err_disable_tcon_tv:
	clk_disable_unprepare(s60->clk_tcon_tv);
err_disable_dcxo:
	clk_disable_unprepare(s60->clk_dcxo);
err_disable_tcon_gate:
	sun8i_dw_hdmi_tcon_top_gate(dev, false);

	return ret;
}

static enum drm_mode_status
sun8i_dw_hdmi_mode_valid_h6(struct dw_hdmi *hdmi, void *data,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	/*
	 * Controller support maximum of 594 MHz, which correlates to
	 * 4K@60Hz 4:4:4 or RGB.
	 */
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool sun8i_dw_hdmi_node_is_tcon_top(struct device_node *node)
{
	return IS_ENABLED(CONFIG_DRM_SUN8I_TCON_TOP) &&
		!!of_match_node(sun8i_tcon_top_of_table, node);
}

static u32 sun8i_dw_hdmi_find_possible_crtcs(struct drm_device *drm,
					     struct device_node *node)
{
	struct device_node *port, *ep, *remote, *remote_port;
	u32 crtcs = 0;

	remote = of_graph_get_remote_node(node, 0, -1);
	if (!remote)
		return 0;

	if (sun8i_dw_hdmi_node_is_tcon_top(remote)) {
		port = of_graph_get_port_by_id(remote, 4);
		if (!port)
			goto crtcs_exit;

		for_each_child_of_node(port, ep) {
			remote_port = of_graph_get_remote_port(ep);
			if (remote_port) {
				crtcs |= drm_of_crtc_port_mask(drm, remote_port);
				of_node_put(remote_port);
			}
		}
	} else {
		crtcs = drm_of_find_possible_crtcs(drm, node);
	}

crtcs_exit:
	of_node_put(remote);

	return crtcs;
}

static int sun8i_dw_hdmi_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	struct drm_device *drm = data;
	struct device_node *phy_node;
	struct drm_encoder *encoder;
	struct sun8i_dw_hdmi *hdmi;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = &hdmi->plat_data;
	hdmi->dev = &pdev->dev;
	encoder = &hdmi->encoder;

	hdmi->quirks = of_device_get_match_data(dev);

	encoder->possible_crtcs =
		sun8i_dw_hdmi_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &sun8i_dw_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	if (hdmi->quirks->integrated_phy) {
		ret = sun8i_dw_hdmi_bind_sun60i(hdmi, pdev, drm);
		if (ret)
			drm_encoder_cleanup(encoder);
		return ret;
	}

	hdmi->rst_ctrl = devm_reset_control_get(dev, "ctrl");
	if (IS_ERR(hdmi->rst_ctrl))
		return dev_err_probe(dev, PTR_ERR(hdmi->rst_ctrl),
				     "Could not get ctrl reset control\n");

	hdmi->clk_tmds = devm_clk_get(dev, "tmds");
	if (IS_ERR(hdmi->clk_tmds))
		return dev_err_probe(dev, PTR_ERR(hdmi->clk_tmds),
				     "Couldn't get the tmds clock\n");

	hdmi->regulator = devm_regulator_get(dev, "hvcc");
	if (IS_ERR(hdmi->regulator))
		return dev_err_probe(dev, PTR_ERR(hdmi->regulator),
				     "Couldn't get regulator\n");

	ret = regulator_enable(hdmi->regulator);
	if (ret) {
		dev_err(dev, "Failed to enable regulator\n");
		return ret;
	}

	ret = reset_control_deassert(hdmi->rst_ctrl);
	if (ret) {
		dev_err(dev, "Could not deassert ctrl reset control\n");
		goto err_disable_regulator;
	}

	ret = clk_prepare_enable(hdmi->clk_tmds);
	if (ret) {
		dev_err(dev, "Could not enable tmds clock\n");
		goto err_assert_ctrl_reset;
	}

	phy_node = of_parse_phandle(dev->of_node, "phys", 0);
	if (!phy_node) {
		dev_err(dev, "Can't found PHY phandle\n");
		ret = -EINVAL;
		goto err_disable_clk_tmds;
	}

	ret = sun8i_hdmi_phy_get(hdmi, phy_node);
	of_node_put(phy_node);
	if (ret) {
		dev_err(dev, "Couldn't get the HDMI PHY\n");
		goto err_disable_clk_tmds;
	}

	ret = sun8i_hdmi_phy_init(hdmi->phy);
	if (ret)
		goto err_disable_clk_tmds;

	plat_data->mode_valid = hdmi->quirks->mode_valid;
	plat_data->use_drm_infoframe = hdmi->quirks->use_drm_infoframe;
	sun8i_hdmi_phy_set_ops(hdmi->phy, plat_data);

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		goto cleanup_encoder;
	}

	return 0;

cleanup_encoder:
	drm_encoder_cleanup(encoder);
err_disable_clk_tmds:
	clk_disable_unprepare(hdmi->clk_tmds);
err_assert_ctrl_reset:
	reset_control_assert(hdmi->rst_ctrl);
err_disable_regulator:
	regulator_disable(hdmi->regulator);

	return ret;
}

static void sun8i_dw_hdmi_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct sun8i_dw_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(hdmi->hdmi);

	if (hdmi->quirks->integrated_phy) {
		struct sun60i_hdmi *s60 = &hdmi->sun60i;

		/* HDCP left off intentionally (see bind_sun60i). */
		clk_disable_unprepare(s60->clk_cec);
		clk_disable_unprepare(s60->clk_hdmi);
		clk_disable_unprepare(s60->clk_hdmi_24m);
		reset_control_assert(s60->rst_bus_sub);
		reset_control_assert(s60->rst_bus_main);
		clk_disable_unprepare(s60->clk_tcon_tv);
		clk_disable_unprepare(s60->clk_dcxo);
		sun8i_dw_hdmi_tcon_top_gate(dev, false);
		return;
	}

	sun8i_hdmi_phy_deinit(hdmi->phy);
	clk_disable_unprepare(hdmi->clk_tmds);
	reset_control_assert(hdmi->rst_ctrl);
	regulator_disable(hdmi->regulator);
}

static const struct component_ops sun8i_dw_hdmi_ops = {
	.bind	= sun8i_dw_hdmi_bind,
	.unbind	= sun8i_dw_hdmi_unbind,
};

static int sun8i_dw_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_dw_hdmi_ops);
}

static void sun8i_dw_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_dw_hdmi_ops);
}

static const struct sun8i_dw_hdmi_quirks sun8i_a83t_quirks = {
	.mode_valid = sun8i_dw_hdmi_mode_valid_a83t,
};

static const struct sun8i_dw_hdmi_quirks sun50i_h6_quirks = {
	.mode_valid = sun8i_dw_hdmi_mode_valid_h6,
	.use_drm_infoframe = true,
};

static const struct sun8i_dw_hdmi_quirks sun60i_a733_quirks = {
	.mode_valid = sun8i_dw_hdmi_mode_valid_a733,
	.use_drm_infoframe = true,
	.integrated_phy = true,
};

static const struct of_device_id sun8i_dw_hdmi_dt_ids[] = {
	{
		.compatible = "allwinner,sun8i-a83t-dw-hdmi",
		.data = &sun8i_a83t_quirks,
	},
	{
		.compatible = "allwinner,sun50i-h6-dw-hdmi",
		.data = &sun50i_h6_quirks,
	},
	{
		.compatible = "allwinner,sun60i-a733-dw-hdmi",
		.data = &sun60i_a733_quirks,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun8i_dw_hdmi_dt_ids);

static struct platform_driver sun8i_dw_hdmi_pltfm_driver = {
	.probe  = sun8i_dw_hdmi_probe,
	.remove = sun8i_dw_hdmi_remove,
	.driver = {
		.name = "sun8i-dw-hdmi",
		.of_match_table = sun8i_dw_hdmi_dt_ids,
	},
};

static int __init sun8i_dw_hdmi_init(void)
{
	int ret;

	ret = platform_driver_register(&sun8i_dw_hdmi_pltfm_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&sun8i_hdmi_phy_driver);
	if (ret) {
		platform_driver_unregister(&sun8i_dw_hdmi_pltfm_driver);
		return ret;
	}

	return ret;
}

static void __exit sun8i_dw_hdmi_exit(void)
{
	platform_driver_unregister(&sun8i_dw_hdmi_pltfm_driver);
	platform_driver_unregister(&sun8i_hdmi_phy_driver);
}

module_init(sun8i_dw_hdmi_init);
module_exit(sun8i_dw_hdmi_exit);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@siol.net>");
MODULE_DESCRIPTION("Allwinner DW HDMI bridge");
MODULE_LICENSE("GPL");
