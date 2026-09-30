/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */

#ifndef _SUN60I_DE_H_
#define _SUN60I_DE_H_

#include <linux/clk.h>
#include <linux/reset.h>

#include <drm/drm_device.h>
#include <drm/drm_modes.h>
#include <drm/drm_plane.h>

#include "sunxi_engine.h"

struct sun60i_de_scanout_state {
	bool enabled;
	dma_addr_t addr;
	u32 pitch;
	u32 format;
	u32 src_x;
	u32 src_y;
	u32 src_w;
	u32 src_h;
	u32 crtc_x;
	u32 crtc_y;
	u32 crtc_w;
	u32 crtc_h;
};

struct sun60i_de {
	struct sunxi_engine engine;
	struct device *dev;
	struct drm_plane primary;
	struct drm_plane cursor;
	struct clk *clk_mixer;
	struct clk *clk_bus_mixer;
	struct reset_control *rst_mixer;
	void __iomem *regs;
	struct drm_display_mode mode;
	struct sun60i_de_scanout_state scanout;
	struct sun60i_de_scanout_state cursor_scanout;
	u8 display_id;
	u8 tcon_id;
	u8 ui_channel;
	u8 bld_port;
	u8 chn_cfg_mode;
	bool mode_valid;
	bool enabled;
	bool rtmx_running;
	u32 programmed_hdisplay;
	u32 programmed_vdisplay;
	bool rcq_ready;
	bool rcq_head_done;

	dma_addr_t rcq_dma;
	size_t rcq_alloc_size;
	unsigned int rcq_nblocks;
	void *rcq_cpu;
	void *rcq_bld_shadow;
	void *rcq_fmt_shadow;
	void *rcq_ovl_shadow;
	void *rcq_vsu_shadow;
	void *rcq_tfbd_shadow;
	void *rcq_ccsc_shadow;
};

static inline struct sun60i_de *engine_to_sun60i_de(struct sunxi_engine *engine)
{
	return container_of(engine, struct sun60i_de, engine);
}

static inline struct sun60i_de *plane_to_sun60i_de(struct drm_plane *plane)
{
	if (plane->type == DRM_PLANE_TYPE_CURSOR)
		return container_of(plane, struct sun60i_de, cursor);

	return container_of(plane, struct sun60i_de, primary);
}

#endif /* _SUN60I_DE_H_ */
