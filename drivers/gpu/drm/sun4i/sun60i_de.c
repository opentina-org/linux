// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner sun60i / A733 DE352 display engine for sun4i-drm.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_rect.h>

#include "sun4i_drv.h"
#include "sun60i_de.h"
#include "sun60i_de_hw.h"
#include "sunxi_engine.h"

static const u32 sun60i_de_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
};

static const u32 sun60i_de_cursor_formats[] = {
	DRM_FORMAT_ARGB8888,
};

static const u64 sun60i_de_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static u8 sun60i_de_bld_port_from_mode(u32 chn_cfg_mode, u8 ui_channel)
{
	if (ui_channel != 0)
		return 3 + ui_channel;

	switch (chn_cfg_mode) {
	case 1:
	case 2:
		return 2;
	default:
		return 3;
	}
}

static void sun60i_de_init_topology(struct sun60i_de *de)
{
	u32 chn_cfg_mode = 0;
	u32 tcon_id = 4;

	de->display_id = 0;
	/*
	 * DE2TCON nibble is the TCON *device_index*, not the OF graph
	 * endpoint reg. Reference A733 HDMI steady state is 0x0000fff4
	 * (disp0 → tcon 4). Looking up port@display_id also fails here:
	 * sun4i output is port@1. Prefer DT "allwinner,tcon-id".
	 */
	de->tcon_id = 4;
	de->ui_channel = 0;
	de->chn_cfg_mode = 0;

	if (!of_property_read_u32(de->dev->of_node, "chn_cfg_mode", &chn_cfg_mode))
		de->chn_cfg_mode = chn_cfg_mode;
	de->bld_port = sun60i_de_bld_port_from_mode(de->chn_cfg_mode, de->ui_channel);

	if (!of_property_read_u32(de->dev->of_node, "allwinner,tcon-id", &tcon_id))
		de->tcon_id = tcon_id & 0xf;
}

static int sun60i_de_hw_enable(struct sun60i_de *de)
{
	int ret;

	if (de->enabled)
		return 0;

	/*
	 * Parent clocks/resets are owned by display_clocks. The mixer only
	 * controls its exported DETOP module/MBUS gates and reset. DE352 must
	 * see the module clock before its core reset is released; reversing
	 * these two operations can wedge the DE AHB port before any RTMX MMIO.
	 */
	ret = clk_prepare_enable(de->clk_mixer);
	if (ret)
		return ret;
	ret = reset_control_deassert(de->rst_mixer);
	if (ret)
		goto err_disable_clk_mixer;
	ret = clk_prepare_enable(de->clk_bus_mixer);
	if (ret)
		goto err_assert_mixer_reset;
	ret = sun60i_de_hw_bus_init(de);
	if (ret)
		goto err_disable_clk_bus_mixer;

	ret = sun60i_de_hw_bus_verify(de);
	if (ret)
		goto err_disable_clk_bus_mixer;

	de->enabled = true;
	return 0;

err_disable_clk_bus_mixer:
	clk_disable_unprepare(de->clk_bus_mixer);
err_assert_mixer_reset:
	reset_control_assert(de->rst_mixer);
err_disable_clk_mixer:
	clk_disable_unprepare(de->clk_mixer);

	return ret;
}

static void sun60i_de_hw_disable_clocks(struct sun60i_de *de)
{
	if (!de->enabled)
		return;

	sun60i_de_hw_disable(de);
	clk_disable_unprepare(de->clk_bus_mixer);
	clk_disable_unprepare(de->clk_mixer);
	reset_control_assert(de->rst_mixer);

	de->enabled = false;
}

static struct sun60i_de_scanout_state *
sun60i_de_plane_scanout(struct sun60i_de *de, struct drm_plane *plane)
{
	return plane->type == DRM_PLANE_TYPE_CURSOR ? &de->cursor_scanout :
						       &de->scanout;
}

static void sun60i_de_update_plane_scanout(struct sun60i_de *de,
					   struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct sun60i_de_scanout_state *scanout = sun60i_de_plane_scanout(de, plane);
	struct drm_plane_state *new_state;
	struct drm_plane_state *old_state;

	new_state = drm_atomic_get_new_plane_state(state, plane);
	if (!new_state)
		return;
	old_state = drm_atomic_get_old_plane_state(state, plane);

	if (!new_state->crtc || !new_state->visible || !new_state->fb) {
		memset(scanout, 0, sizeof(*scanout));
		return;
	}

	drm_fb_dma_sync_non_coherent(plane->dev, old_state, new_state);

	scanout->enabled = true;
	scanout->addr = drm_fb_dma_get_gem_addr(new_state->fb, new_state, 0);
	scanout->pitch = new_state->fb->pitches[0];
	scanout->format = new_state->fb->format->format;
	scanout->src_x = new_state->src.x1 >> 16;
	scanout->src_y = new_state->src.y1 >> 16;
	scanout->src_w = drm_rect_width(&new_state->src) >> 16;
	scanout->src_h = drm_rect_height(&new_state->src) >> 16;
	scanout->crtc_x = new_state->dst.x1;
	scanout->crtc_y = new_state->dst.y1;
	scanout->crtc_w = drm_rect_width(&new_state->dst);
	scanout->crtc_h = drm_rect_height(&new_state->dst);
}

static void sun60i_de_commit(struct sunxi_engine *engine,
			     struct drm_crtc *crtc,
			     struct drm_atomic_state *state)
{
	struct sun60i_de *de = engine_to_sun60i_de(engine);
	int ret;

	sun60i_de_update_plane_scanout(de, &de->primary, state);
	sun60i_de_update_plane_scanout(de, &de->cursor, state);

	if (!de->enabled || !de->mode_valid) {
		dev_err_once(de->dev,
			     "de: commit skipped (enabled=%d mode_valid=%d)\n",
			     de->enabled, de->mode_valid);
		return;
	}

	ret = sun60i_de_hw_commit(de);
	if (ret)
		dev_err_ratelimited(de->dev, "Failed to commit display state: %d\n", ret);
}

static void sun60i_de_mode_set(struct sunxi_engine *engine,
			       const struct drm_display_mode *mode)
{
	struct sun60i_de *de = engine_to_sun60i_de(engine);

	drm_mode_copy(&de->mode, mode);
	de->mode_valid = true;
}

static void sun60i_de_quiesce(struct sunxi_engine *engine,
			      struct drm_crtc *crtc)
{
	struct sun60i_de *de = engine_to_sun60i_de(engine);

	memset(&de->scanout, 0, sizeof(de->scanout));
	memset(&de->cursor_scanout, 0, sizeof(de->cursor_scanout));
	/*
	 * A modeset quiesce is a real hardware disable.  Keep the clock
	 * framework state in lockstep with DETOP: sun60i_de_hw_disable() used
	 * to close the module/MBUS gates behind the framework's back while
	 * leaving de->enabled set.  The following enable then returned early,
	 * so RCQ completed against clock-gated BLD/UI blocks and the display
	 * stayed black after the first mode change.
	 */
	sun60i_de_hw_disable_clocks(de);
}

static void sun60i_de_enable(struct sunxi_engine *engine,
			     const struct drm_display_mode *mode)
{
	struct sun60i_de *de = engine_to_sun60i_de(engine);
	int ret;

	drm_mode_copy(&de->mode, mode);
	de->mode_valid = true;

	ret = sun60i_de_hw_enable(de);
	if (ret) {
		dev_err(de->dev, "de: enable failed: %d\n", ret);
		return;
	}
}

static int sun60i_de_plane_atomic_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state;
	struct drm_crtc_state *crtc_state;
	int ret;

	new_state = drm_atomic_get_new_plane_state(state, plane);
	if (!new_state->crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  plane->type == DRM_PLANE_TYPE_CURSOR,
						  true);
	if (ret)
		return ret;

	if (plane->type == DRM_PLANE_TYPE_CURSOR && new_state->visible &&
	    (drm_rect_width(&new_state->src) > 256 << 16 ||
	     drm_rect_height(&new_state->src) > 256 << 16))
		return -EINVAL;

	return 0;
}

static void sun60i_de_plane_atomic_disable(struct drm_plane *plane,
					   struct drm_atomic_state *state)
{
	struct sun60i_de *de = plane_to_sun60i_de(plane);
	struct sun60i_de_scanout_state *scanout = sun60i_de_plane_scanout(de, plane);

	memset(scanout, 0, sizeof(*scanout));
}

static void sun60i_de_plane_atomic_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	/*
	 * Scanout is latched in sun60i_de_commit() via the CRTC flush path.
	 * Provide a non-NULL atomic_update so drm_atomic_helper_commit_planes()
	 * does not call into a NULL function pointer.
	 */
}

static const struct drm_plane_helper_funcs sun60i_de_plane_helper_funcs = {
	.prepare_fb = drm_gem_plane_helper_prepare_fb,
	.atomic_check = sun60i_de_plane_atomic_check,
	.atomic_update = sun60i_de_plane_atomic_update,
	.atomic_disable = sun60i_de_plane_atomic_disable,
};

static const struct drm_plane_funcs sun60i_de_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane **sun60i_de_layers_init(struct drm_device *drm,
						struct sunxi_engine *engine)
{
	struct sun60i_de *de = engine_to_sun60i_de(engine);
	struct drm_plane **planes;
	int ret;

	planes = devm_kcalloc(drm->dev, 3, sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	ret = drm_universal_plane_init(drm, &de->primary, 0,
				       &sun60i_de_plane_funcs,
				       sun60i_de_formats,
				       ARRAY_SIZE(sun60i_de_formats),
				       sun60i_de_modifiers,
				       DRM_PLANE_TYPE_PRIMARY,
				       "sun60i-primary");
	if (ret)
		return ERR_PTR(ret);

	drm_plane_helper_add(&de->primary, &sun60i_de_plane_helper_funcs);

	ret = drm_universal_plane_init(drm, &de->cursor, 0,
				       &sun60i_de_plane_funcs,
				       sun60i_de_cursor_formats,
				       ARRAY_SIZE(sun60i_de_cursor_formats),
				       sun60i_de_modifiers,
				       DRM_PLANE_TYPE_CURSOR,
				       "sun60i-cursor");
	if (ret)
		return ERR_PTR(ret);

	drm_plane_helper_add(&de->cursor, &sun60i_de_plane_helper_funcs);

	planes[0] = &de->primary;
	planes[1] = &de->cursor;
	planes[2] = NULL;

	return planes;
}

static const struct sunxi_engine_ops sun60i_de_engine_ops = {
	.commit		= sun60i_de_commit,
	.layers_init	= sun60i_de_layers_init,
	.mode_set	= sun60i_de_mode_set,
	.quiesce	= sun60i_de_quiesce,
	.enable		= sun60i_de_enable,
};

/*
 * T527-style mixer@100000 lists engine/top/display/detop. All sun60i_de
 * offsets are from DE base (0x05000000); detop is DE+0x8000. Map the full
 * 4MiB window without exclusive claim so clock@8000 can own DETOP.
 */
static int sun60i_de_map_regs(struct platform_device *pdev, struct sun60i_de *de)
{
	struct resource *detop, *res;
	resource_size_t base;

	detop = platform_get_resource_byname(pdev, IORESOURCE_MEM, "detop");
	if (detop) {
		if (detop->start < 0x8000)
			return -EINVAL;
		base = detop->start - 0x8000;
		de->regs = devm_ioremap(&pdev->dev, base, 0x400000);
		if (!de->regs)
			return -ENOMEM;
		return 0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	de->regs = devm_ioremap_resource(&pdev->dev, res);
	return PTR_ERR_OR_ZERO(de->regs);
}

static int sun60i_de_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun60i_de *de;
	int ret;

	de = devm_kzalloc(dev, sizeof(*de), GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	de->dev = dev;
	de->engine.ops = &sun60i_de_engine_ops;
	de->engine.node = dev->of_node;
	de->engine.id = 0;

	sun60i_de_init_topology(de);

	ret = sun60i_de_map_regs(pdev, de);
	if (ret)
		return dev_err_probe(dev, ret, "failed to map DE regs\n");

	de->clk_mixer = devm_clk_get(dev, "mod");
	if (IS_ERR(de->clk_mixer))
		return dev_err_probe(dev, PTR_ERR(de->clk_mixer),
				     "failed to get mixer module clock\n");

	de->clk_bus_mixer = devm_clk_get(dev, "bus");
	if (IS_ERR(de->clk_bus_mixer))
		return dev_err_probe(dev, PTR_ERR(de->clk_bus_mixer),
				     "failed to get mixer bus clock\n");

	de->rst_mixer = devm_reset_control_get_shared(dev, "mod");
	if (IS_ERR(de->rst_mixer))
		return dev_err_probe(dev, PTR_ERR(de->rst_mixer),
				     "failed to get mixer reset\n");

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	list_add_tail(&de->engine.list, &drv->engine_list);
	dev_set_drvdata(dev, de);

	return 0;
}

static void sun60i_de_unbind(struct device *dev, struct device *master, void *data)
{
	struct sun60i_de *de = dev_get_drvdata(dev);

	if (!de)
		return;

	sun60i_de_hw_disable_clocks(de);

	/* The DRM master owns plane cleanup through drm_mode_config_cleanup(). */
	list_del(&de->engine.list);
	dev_set_drvdata(dev, NULL);
}

static const struct component_ops sun60i_de_ops = {
	.bind = sun60i_de_bind,
	.unbind = sun60i_de_unbind,
};

static int sun60i_de_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun60i_de_ops);
}

static void sun60i_de_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun60i_de_ops);
}

static const struct of_device_id sun60i_de_of_match[] = {
	{ .compatible = "allwinner,sun60i-a733-de352-mixer-0" },
	{ .compatible = "allwinner,sun60i-a733-de" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun60i_de_of_match);

static struct platform_driver sun60i_de_platform_driver = {
	.probe = sun60i_de_probe,
	.remove = sun60i_de_remove,
	.driver = {
		.name = "sun60i-de",
		.of_match_table = sun60i_de_of_match,
	},
};
module_platform_driver(sun60i_de_platform_driver);

MODULE_DESCRIPTION("Allwinner sun60i DE352 display engine");
MODULE_LICENSE("GPL");
