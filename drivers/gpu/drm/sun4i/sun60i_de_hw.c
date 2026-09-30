// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/* Allwinner sun60i / A733 DE352 display engine programming. */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_modes.h>

#include "sun60i_de.h"
#include "sun60i_de_hw.h"

#define SUN60I_DE_RESET_OFFSET			0x8000
#define SUN60I_DE_CLK_OFFSET			0x8004
#define SUN60I_DE_MBUS_CLK_OFFSET		0x8008
/* BSP de_top DE_RESERVE_CTL: bit12 enables RCQ FIFO update mode (v3xx) */
#define SUN60I_DE_RESERVE_CTL_OFFSET		0x800c
#define SUN60I_DE_RESERVE_CTL_RCQ_FIFO_EN	BIT(12)
/* sun60iw2 (A733): BSP de_top_set_ahb_read_mode(1) before RCQ/RTMX access */
#define SUN60I_DE_RESERVE_CTL_AHB_CONFLICT	BIT(8)
#define SUN60I_DE_VER_CTL_OFFSET		0x8014
#define SUN60I_DE_DE2TCON_MUX_OFFSET		0x8010
#define SUN60I_DE_VCH2CORE_MUX_OFFSET		0x8028
#define SUN60I_DE_UCH2CORE_MUX_OFFSET		0x802c
#define SUN60I_DE_PORT2CHN_MUX_OFFSET		0x8030
#define SUN60I_DE_BUF_DEPTH_OFFSET		0x8050
#define SUN60I_DE_URGENCY_CTL_OFFSET		0x8080
#define SUN60I_DE_GATING_CTL_OFFSET		0x80e8
#define SUN60I_DE_GLB_CTL_OFFSET		0x8100
#define SUN60I_DE_OUT_SIZE_OFFSET		0x8108
#define SUN60I_DE_AUTO_CLK_OFFSET		0x810c
#define SUN60I_DE_AUTO_CLK_RCQ_GATE		BIT(16)
#define SUN60I_DE_RCQ_CTL_OFFSET		0x8110
#define SUN60I_DE_RCQ_HEADER_LADDR_OFFSET	0x8114
#define SUN60I_DE_GLB_STS_OFFSET		0x8104
#define SUN60I_DE_GLB_STS_BUSY			BIT(4)
#define SUN60I_DE_FRAME_END			BIT(0)
#define SUN60I_DE_RCQ_FINISH			BIT(2)
#define SUN60I_DE_RCQ_ACCEPT			BIT(3)
#define SUN60I_DE_IRQ_STATE_MASK		GENMASK(3, 0)

#define SUN60I_DE_DISP_CLK_SHIFT		4

#define SUN60I_DE_GLB_CTL_RTMX_EN		BIT(0)
#define SUN60I_DE_GLB_CTL_FIELD_REVERSE		BIT(9)

#define SUN60I_DE_DISP_STRIDE			0x40
#define SUN60I_DE_ASYNC_BRIDGE_OFFSET		0x804c
#define SUN60I_DE_CHN_SIZE			0x20000
#define SUN60I_DE_CHN_BASE(phy)			(0x100000 + (phy) * SUN60I_DE_CHN_SIZE)
#define SUN60I_DE_CHN_OVL_OFFSET		0x1000
#define SUN60I_DE_CHN_SCALER_OFFSET		0x4000
#define SUN60I_DE_UI_PHY_CHN			6
#define SUN60I_DE_UI_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_OVL_OFFSET)
#define SUN60I_DE_CHN_CCSC_OFFSET		0x0800
#define SUN60I_DE_CHN_SNR_OFFSET		0x6400
#define SUN60I_DE_CHN_SHARP_OFFSET		0x6000
#define SUN60I_DE_UI_CCSC_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_CCSC_OFFSET)
#define SUN60I_DE_UI_SNR_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_SNR_OFFSET)
#define SUN60I_DE_UI_SHARP_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_SHARP_OFFSET)
#define SUN60I_DE_UI_SCALER_BASE		(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_SCALER_OFFSET)
#define SUN60I_DE_DISP_BASE(disp)		(0x280000 + (disp) * 0x20000)
#define SUN60I_DE_FMT_BASE(disp)		(SUN60I_DE_DISP_BASE(disp) + 0x5000)
#define SUN60I_DE_DITHER_BASE(disp)		(SUN60I_DE_DISP_BASE(disp) + 0x8000)
#define SUN60I_DE_GAMMA_BASE(disp)		(SUN60I_DE_DISP_BASE(disp) + 0x9000)
#define SUN60I_DE_BLD_BASE(disp)		(SUN60I_DE_DISP_BASE(disp) + 0x1000)

#define SUN60I_DE_BLD_EN			0x0000
#define SUN60I_DE_BLD_PIPE_FCOLOR(pipe)		(0x0004 + (pipe) * 0x10)
#define SUN60I_DE_BLD_PIPE_IN_SIZE(pipe)	(0x0008 + (pipe) * 0x10)
#define SUN60I_DE_BLD_PIPE_IN_COORD(pipe)	(0x000c + (pipe) * 0x10)
#define SUN60I_DE_BLD_ROUT_CTL			0x0080
#define SUN60I_DE_BLD_PREMUL_CTL		0x0084
#define SUN60I_DE_BLD_BG_COLOR			0x0088
#define SUN60I_DE_BLD_OUT_SIZE			0x008c
#define SUN60I_DE_BLD_BLEND_CTL(pipe)		(0x0090 + (pipe) * 0x4)
#define SUN60I_DE_BLD_OUT_CTL			0x00fc

#define SUN60I_DE_BLD_FCOLOR_EN(pipe)		BIT(pipe)
#define SUN60I_DE_BLD_PIPE_EN(pipe)		BIT(8 + (pipe))
#define SUN60I_DE_BLD_MODE_SRCOVER		0x03010301
#define SUN60I_DE_BLD_MODE_DST			0x01000100
#define SUN60I_DE_BLD_OUT_CTL_INTERLACE		BIT(1)
#define SUN60I_DE_BLD_OUT_CTL_FMT_RGB		(0 << 8)

#define SUN60I_DE_CCSC_CTL_OFF			0x00
#define SUN60I_DE_CCSC_D0_OFF			0x04
#define SUN60I_DE_CCSC_C0_OFF			0x10
#define SUN60I_DE_CCSC_BYPASS_COEFF		0x00000400
#define SUN60I_DE_SHARP_CTL_OFF			0x00

#define SUN60I_DE_RCQ_FMT_SIZE			0x2c
#define SUN60I_DE_FMT_SIZE_REG_OFF		0x04
#define SUN60I_DE_FMT_LIMIT_Y_OFF		0x20
#define SUN60I_DE_FMT_LIMIT_C0_OFF		0x24
#define SUN60I_DE_FMT_LIMIT_C1_OFF		0x28
#define SUN60I_DE_FMT_LIMIT_FULL		0x0fff0000
#define SUN60I_DE_RCQ_OVL_DS_OFF		0xe0
#define SUN60I_DE_RCQ_OVL_DS_SIZE		0x1c
#define SUN60I_DE_CHN_TFBD_OFFSET		0x5400
#define SUN60I_DE_UI_TFBD_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_TFBD_OFFSET)

#define SUN60I_DE_VSU_CTL_SIZE			0x14
#define SUN60I_DE_VSU_ATTR_SIZE			0x08
#define SUN60I_DE_VSU_PARA_SIZE			0x1c
#define SUN60I_DE_VSU_COEFF_SIZE		0x80
#define SUN60I_DE_VSU8_CPARA_OFF		0x0c0
#define SUN60I_DE_VSU8_COEFF0_OFF		0x200
#define SUN60I_DE_VSU8_COEFF1_OFF		0x400
#define SUN60I_DE_VSU8_COEFF2_OFF		0x600
#define SUN60I_DE_VSU_SHADOW_SIZE		0x680

#define SUN60I_DE_RCQ_TFBD_SIZE			0x4
#define SUN60I_DE_RCQ_CCSC_CTL_SIZE		0x4
#define SUN60I_DE_RCQ_ALIGN			32
#define SUN60I_DE_RCQ_BLOCK_COUNT		19

#define SUN60I_DE_UI_LAY_STRIDE			0x20
#define SUN60I_DE_UI_LAY_CTL(layer)		((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x00)
#define SUN60I_DE_UI_LAY_MBSIZE(layer)		((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x04)
#define SUN60I_DE_UI_LAY_MBCOOR(layer)		((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x08)
#define SUN60I_DE_UI_LAY_PITCH(layer)		((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x0c)
#define SUN60I_DE_UI_LAY_TOP_LADDR(layer)	((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x10)
#define SUN60I_DE_UI_LAY_BOT_LADDR(layer)	((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x14)
#define SUN60I_DE_UI_LAY_FCOLOR(layer)		((layer) * SUN60I_DE_UI_LAY_STRIDE + 0x18)
#define SUN60I_DE_UI_TOP_HADDR			0x0080
#define SUN60I_DE_UI_BOT_HADDR			0x0084
#define SUN60I_DE_UI_WIN_SIZE			0x0088
#define SUN60I_DE_UI_HORI_DS			0x00f0
#define SUN60I_DE_UI_VERT_DS			0x00f8

#define SUN60I_DE_UI_CTL_EN			BIT(0)
#define SUN60I_DE_UI_CTL_ALPHA_MODE_SHIFT	1
#define SUN60I_DE_UI_CTL_FMT_SHIFT		8
#define SUN60I_DE_UI_CTL_GLB_ALPHA_SHIFT	24
#define SUN60I_DE_UI_CTL_COLOR_FILL		BIT(4)

#define SUN60I_DE_VSU8_CTL_EN			BIT(0)
#define SUN60I_DE_VSU8_SCALE_MODE_OFF		0x10
#define SUN60I_DE_VSU8_OUT_SIZE_OFF		0x40
#define SUN60I_DE_VSU8_GLB_ALPHA_OFF		0x44
#define SUN60I_DE_VSU8_Y_IN_SIZE_OFF		0x80
#define SUN60I_DE_VSU8_Y_HSTEP_OFF		0x88
#define SUN60I_DE_VSU8_Y_VSTEP_OFF		0x8c
#define SUN60I_DE_VSU8_Y_HPHASE_OFF		0x90
#define SUN60I_DE_VSU8_Y_VPHASE_OFF		0x98
/* VSU8 1:1 step: (1 << 19) << VSU8_STEP_VALID_START_BIT(1) */
#define SUN60I_DE_VSU8_STEP_1TO1		0x100000

#define SUN60I_DE_MBUS_CLK_EN			BIT(0)
#define SUN60I_DE_MBUS_RESET_DEASSERT		BIT(4)
#define SUN60I_DE_MBUS_AUTO_GATE		BIT(8)

#define SUN60I_DE_RCQ_HEAD_SIZE			16
#define SUN60I_DE_RCQ_HEAD_COUNT			\
	ALIGN(SUN60I_DE_RCQ_BLOCK_COUNT, 2)
#define SUN60I_DE_RCQ_HEAD_LEN			(SUN60I_DE_RCQ_HEAD_COUNT * \
						 SUN60I_DE_RCQ_HEAD_SIZE)
#define SUN60I_DE_RCQ_HEAD_LEN_ALIGNED		(ALIGN(SUN60I_DE_RCQ_HEAD_LEN, \
						   SUN60I_DE_RCQ_HEAD_SIZE * 2))
#define SUN60I_DE_RCQ_OVL_LAY0_SIZE		0x1c
#define SUN60I_DE_RCQ_OVL_PARA_SIZE		0x0c
#define SUN60I_DE_RCQ_OVL_PARA_OFF		0x80
#define SUN60I_DE_RCQ_BLD_ATTR_SIZE		0x60
#define SUN60I_DE_RCQ_BLD_CTL_SIZE		0x24
#define SUN60I_DE_RCQ_BLD_CTL_OFF		0x80
#define SUN60I_DE_RCQ_BLD_CK_SIZE		0x60
#define SUN60I_DE_RCQ_BLD_CK_OFF		0xa0
#define SUN60I_DE_RCQ_OVL_SHADOW_SIZE		0x100
#define SUN60I_DE_RCQ_BLD_SHADOW_SIZE		0x100

#define SUN60I_DE_AHB_TIMEOUT_OFFSET		0x80e4

/* HDMI UI phys6: clk gating bit (phy + 2), see de_rtmx_set_chn_mux sun60iw2 */
#define SUN60I_DE_HDMI_UI_GATING		BIT(8)

#define SUN60I_DE_GLB_CTL_PIXEL_MODE_SHIFT	16

#define SUN60I_DE_CHN_CDC_OFFSET		0x8000
#define SUN60I_DE_UI_CDC_BASE			(SUN60I_DE_CHN_BASE(SUN60I_DE_UI_PHY_CHN) + \
						 SUN60I_DE_CHN_CDC_OFFSET)

/* TCON DEBUG @0xfc — current scanline (DE352 RCQ wait window, see BSP rcq_wait_line) */
#define SUN60I_TCON_DEBUG_OFF			0x00fc
#define SUN60I_TCON_DEBUG_TV_LINE_MASK		GENMASK(11, 0)

struct sun60i_de_rcq_head {
	u32 low_addr;
	u32 dw0;
	u32 dirty;
	u32 reg_offset;
};

static void sun60i_de_top_auto_clk_write(struct sun60i_de *de, bool enable,
					 bool rcq_gate);
static void sun60i_de_top_glb_rtmx_write(struct sun60i_de *de, bool enable);
static void sun60i_de_top_set_de2tcon_mux(struct sun60i_de *de, u32 tcon);
static void sun60i_de_top_setup_chn_mux(struct sun60i_de *de);
static void sun60i_de_rcq_program_head(struct sun60i_de *de);
static void sun60i_de_top_rtmx_buf_depth(struct sun60i_de *de);
static void sun60i_de_top_set_out_size(struct sun60i_de *de);
static void sun60i_de_top_set_urgency(struct sun60i_de *de);

static inline void sun60i_de_writel(struct sun60i_de *de, u32 reg, u32 value)
{
	writel(value, de->regs + reg);
}

static inline u32 sun60i_de_readl(struct sun60i_de *de, u32 reg)
{
	return readl(de->regs + reg);
}

/* RMW one field — matches BSP SET_BITS / sun55i_de regmap_read pattern. */
static void sun60i_de_rmw(struct sun60i_de *de, u32 reg, u32 mask, u32 val)
{
	u32 tmp = sun60i_de_readl(de, reg);

	tmp = (tmp & ~mask) | (val & mask);
	sun60i_de_writel(de, reg, tmp);
}

static void sun60i_de_rmw_bits(struct sun60i_de *de, u32 reg, u32 shift,
			       u32 width, u32 val)
{
	u32 mask = ((width >= 32) ? ~0U : ((1U << width) - 1U)) << shift;

	sun60i_de_rmw(de, reg, mask, (val << shift) & mask);
}

static void sun60i_de_shadow_writel(void *shadow, u32 offset, u32 value)
{
	writel(value, (u8 __iomem *)shadow + offset);
}

static u32 sun60i_de_rcq_pack_dw0(dma_addr_t phy, u32 len)
{
	return (len & 0xffffff) | ((phy >> 32) & 0xff) << 24;
}

static void sun60i_de_vsu_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	sun60i_de_shadow_writel(de->rcq_vsu_shadow, offset, value);
}

static void sun60i_de_ccsc_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	sun60i_de_shadow_writel(de->rcq_ccsc_shadow, offset, value);
}

static void sun60i_de_tfbd_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	sun60i_de_shadow_writel(de->rcq_tfbd_shadow, offset, value);
}

static void sun60i_de_fmt_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	sun60i_de_shadow_writel(de->rcq_fmt_shadow, offset, value);
}

static void sun60i_de_ovl_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	sun60i_de_shadow_writel(de->rcq_ovl_shadow, offset, value);
}

static void sun60i_de_bld_writel(struct sun60i_de *de, u32 offset, u32 value)
{
	if (de->rcq_bld_shadow)
		sun60i_de_shadow_writel(de->rcq_bld_shadow, offset, value);
}

static size_t sun60i_de_rcq_pool_data_size(void)
{
	size_t size = 0;

	size += ALIGN(SUN60I_DE_RCQ_BLD_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);
	size += ALIGN(SUN60I_DE_RCQ_FMT_SIZE, SUN60I_DE_RCQ_ALIGN);
	size += ALIGN(SUN60I_DE_RCQ_OVL_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);
	size += ALIGN(SUN60I_DE_VSU_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);
	size += ALIGN(SUN60I_DE_RCQ_TFBD_SIZE, SUN60I_DE_RCQ_ALIGN);
	size += ALIGN(SUN60I_DE_RCQ_CCSC_CTL_SIZE, SUN60I_DE_RCQ_ALIGN);

	return size;
}

static struct device *sun60i_de_dma_dev(struct sun60i_de *de)
{
	/*
	 * memory-region = <&drm_cma> is on the sun4i_drv master node, not the
	 * DE component. Allocating via de->dev has no cma_area and dma_alloc_wc
	 * then wedges the SoC on A733 (hang right after the warn).
	 */
	if (de->primary.dev)
		return de->primary.dev->dev;

	return de->dev;
}

static size_t sun60i_de_rcq_total_size(void)
{
	size_t size = SUN60I_DE_RCQ_HEAD_LEN_ALIGNED + sun60i_de_rcq_pool_data_size();

	size = PAGE_ALIGN(size);
	if (size <= PAGE_SIZE)
		size = PAGE_SIZE * 2;

	return size;
}

static void sun60i_de_rcq_sync_for_device(struct sun60i_de *de)
{
	/*
	 * RCQ pool comes from dma_alloc_wc(), not dma_map_single(). Calling
	 * dma_sync_single_for_device() on a coherent/WC dma_addr is undefined
	 * and on A733 with the IOMMU can leave the mapping unusable.
	 * WC already needs only a write barrier so the DE DMA sees CPU stores.
	 */
	if (!de->rcq_cpu)
		return;

	/* Publish WC descriptor and shadow writes before the RCQ doorbell. */
	wmb();
}

static u32 sun60i_de_pack_size(u32 width, u32 height)
{
	return (width ? width - 1 : 0) |
	       (height ? (height - 1) << 16 : 0);
}

static void sun60i_de_hw_program_fmt(struct sun60i_de *de)
{
	u32 size;

	if (!de->rcq_fmt_shadow || !de->mode_valid)
		return;

	size = sun60i_de_pack_size(de->mode.crtc_hdisplay, de->mode.crtc_vdisplay);

	memset(de->rcq_fmt_shadow, 0, SUN60I_DE_RCQ_FMT_SIZE);
	sun60i_de_fmt_writel(de, SUN60I_DE_FMT_SIZE_REG_OFF, size);
	sun60i_de_fmt_writel(de, SUN60I_DE_FMT_LIMIT_Y_OFF, SUN60I_DE_FMT_LIMIT_FULL);
	sun60i_de_fmt_writel(de, SUN60I_DE_FMT_LIMIT_C0_OFF, SUN60I_DE_FMT_LIMIT_FULL);
	sun60i_de_fmt_writel(de, SUN60I_DE_FMT_LIMIT_C1_OFF, SUN60I_DE_FMT_LIMIT_FULL);
}

static void sun60i_de_hw_program_tfbd(struct sun60i_de *de)
{
	if (!de->rcq_tfbd_shadow)
		return;

	memset(de->rcq_tfbd_shadow, 0, SUN60I_DE_RCQ_TFBD_SIZE);
	sun60i_de_tfbd_writel(de, 0, 0);
}

static void sun60i_de_rcq_free(struct sun60i_de *de)
{
	struct device *dma_dev = sun60i_de_dma_dev(de);

	if (!de->rcq_cpu)
		return;

	dma_free_wc(dma_dev, de->rcq_alloc_size ? de->rcq_alloc_size :
		    sun60i_de_rcq_total_size(), de->rcq_cpu, de->rcq_dma);
	de->rcq_cpu = NULL;
	de->rcq_dma = 0;
	de->rcq_alloc_size = 0;
	de->rcq_nblocks = 0;
	de->rcq_bld_shadow = NULL;
	de->rcq_fmt_shadow = NULL;
	de->rcq_ovl_shadow = NULL;
	de->rcq_vsu_shadow = NULL;
	de->rcq_tfbd_shadow = NULL;
	de->rcq_ccsc_shadow = NULL;
	de->rcq_ready = false;
	de->rcq_head_done = false;
}

static void sun60i_de_rcq_fill_head(struct sun60i_de_rcq_head *head,
				    dma_addr_t phy, u32 len, u32 reg_off)
{
	head->low_addr = lower_32_bits(phy);
	head->dw0 = sun60i_de_rcq_pack_dw0(phy, len);
	head->dirty = 0;
	head->reg_offset = reg_off;
}

static void sun60i_de_rcq_layout_shadows(u8 *base, dma_addr_t pool_dma,
					 struct sun60i_de *de,
					 dma_addr_t *bld_dma, dma_addr_t *fmt_dma,
					 dma_addr_t *ovl_dma, dma_addr_t *vsu_dma,
					 dma_addr_t *tfbd_dma, dma_addr_t *ccsc_dma)
{
	size_t off = SUN60I_DE_RCQ_HEAD_LEN_ALIGNED;

	de->rcq_bld_shadow = base + off;
	*bld_dma = pool_dma + off;
	off += ALIGN(SUN60I_DE_RCQ_BLD_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);

	de->rcq_fmt_shadow = base + off;
	*fmt_dma = pool_dma + off;
	off += ALIGN(SUN60I_DE_RCQ_FMT_SIZE, SUN60I_DE_RCQ_ALIGN);

	de->rcq_ovl_shadow = base + off;
	*ovl_dma = pool_dma + off;
	off += ALIGN(SUN60I_DE_RCQ_OVL_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);

	de->rcq_vsu_shadow = base + off;
	*vsu_dma = pool_dma + off;
	off += ALIGN(SUN60I_DE_VSU_SHADOW_SIZE, SUN60I_DE_RCQ_ALIGN);

	de->rcq_tfbd_shadow = base + off;
	*tfbd_dma = pool_dma + off;
	off += ALIGN(SUN60I_DE_RCQ_TFBD_SIZE, SUN60I_DE_RCQ_ALIGN);

	de->rcq_ccsc_shadow = base + off;
	*ccsc_dma = pool_dma + off;
}

static int sun60i_de_rcq_init(struct sun60i_de *de)
{
	struct sun60i_de_rcq_head *heads;
	struct device *dma_dev = sun60i_de_dma_dev(de);
	dma_addr_t bld_dma, fmt_dma, ovl_dma, vsu_dma, tfbd_dma, ccsc_dma;
	dma_addr_t bld_base = SUN60I_DE_BLD_BASE(de->display_id);
	dma_addr_t fmt_base = SUN60I_DE_FMT_BASE(de->display_id);
	size_t size;
	u8 *base;
	unsigned int idx = 0;

	if (de->rcq_ready)
		return 0;

	size = sun60i_de_rcq_total_size();

	if (!dma_dev->cma_area) {
		dev_err(de->dev,
			"de: no cma_area on dma_dev %s (need master memory-region)\n",
			dev_name(dma_dev));
		return -ENODEV;
	}

	base = dma_alloc_wc(dma_dev, size, &de->rcq_dma, GFP_KERNEL);
	if (!base) {
		dev_err(de->dev, "de: rcq dma_alloc_wc(%zu) failed\n", size);
		return -ENOMEM;
	}

	de->rcq_cpu = base;
	memset(base, 0, size);
	heads = (struct sun60i_de_rcq_head *)base;

	sun60i_de_rcq_layout_shadows(base, de->rcq_dma, de, &bld_dma, &fmt_dma,
				     &ovl_dma, &vsu_dma, &tfbd_dma, &ccsc_dma);

	/* BLD ×3 */
	sun60i_de_rcq_fill_head(&heads[idx++], bld_dma, SUN60I_DE_RCQ_BLD_ATTR_SIZE,
				bld_base);
	sun60i_de_rcq_fill_head(&heads[idx++],
				bld_dma + SUN60I_DE_RCQ_BLD_CTL_OFF,
				SUN60I_DE_RCQ_BLD_CTL_SIZE,
				bld_base + SUN60I_DE_RCQ_BLD_CTL_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				bld_dma + SUN60I_DE_RCQ_BLD_CK_OFF,
				SUN60I_DE_RCQ_BLD_CK_SIZE,
				bld_base + SUN60I_DE_RCQ_BLD_CK_OFF);
	/* FMT */
	sun60i_de_rcq_fill_head(&heads[idx++], fmt_dma, SUN60I_DE_RCQ_FMT_SIZE,
				fmt_base);
	/* OVL ×6 (LAY0-3 + PARA + DS) */
	sun60i_de_rcq_fill_head(&heads[idx++], ovl_dma, SUN60I_DE_RCQ_OVL_LAY0_SIZE,
				SUN60I_DE_UI_BASE);
	sun60i_de_rcq_fill_head(&heads[idx++],
				ovl_dma + SUN60I_DE_UI_LAY_STRIDE,
				SUN60I_DE_RCQ_OVL_LAY0_SIZE,
				SUN60I_DE_UI_BASE + SUN60I_DE_UI_LAY_STRIDE);
	sun60i_de_rcq_fill_head(&heads[idx++],
				ovl_dma + SUN60I_DE_UI_LAY_STRIDE * 2,
				SUN60I_DE_RCQ_OVL_LAY0_SIZE,
				SUN60I_DE_UI_BASE + SUN60I_DE_UI_LAY_STRIDE * 2);
	sun60i_de_rcq_fill_head(&heads[idx++],
				ovl_dma + SUN60I_DE_UI_LAY_STRIDE * 3,
				SUN60I_DE_RCQ_OVL_LAY0_SIZE,
				SUN60I_DE_UI_BASE + SUN60I_DE_UI_LAY_STRIDE * 3);
	sun60i_de_rcq_fill_head(&heads[idx++],
				ovl_dma + SUN60I_DE_RCQ_OVL_PARA_OFF,
				SUN60I_DE_RCQ_OVL_PARA_SIZE,
				SUN60I_DE_UI_BASE + SUN60I_DE_RCQ_OVL_PARA_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				ovl_dma + SUN60I_DE_RCQ_OVL_DS_OFF,
				SUN60I_DE_RCQ_OVL_DS_SIZE,
				SUN60I_DE_UI_BASE + SUN60I_DE_RCQ_OVL_DS_OFF);
	/* VSU8 ×7 */
	sun60i_de_rcq_fill_head(&heads[idx++], vsu_dma, SUN60I_DE_VSU_CTL_SIZE,
				SUN60I_DE_UI_SCALER_BASE);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_OUT_SIZE_OFF,
				SUN60I_DE_VSU_ATTR_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_OUT_SIZE_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_Y_IN_SIZE_OFF,
				SUN60I_DE_VSU_PARA_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_Y_IN_SIZE_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_CPARA_OFF,
				SUN60I_DE_VSU_PARA_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_CPARA_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_COEFF0_OFF,
				SUN60I_DE_VSU_COEFF_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_COEFF0_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_COEFF1_OFF,
				SUN60I_DE_VSU_COEFF_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_COEFF1_OFF);
	sun60i_de_rcq_fill_head(&heads[idx++],
				vsu_dma + SUN60I_DE_VSU8_COEFF2_OFF,
				SUN60I_DE_VSU_COEFF_SIZE,
				SUN60I_DE_UI_SCALER_BASE + SUN60I_DE_VSU8_COEFF2_OFF);
	/* TFBD + CCSC disable */
	sun60i_de_rcq_fill_head(&heads[idx++], tfbd_dma, SUN60I_DE_RCQ_TFBD_SIZE,
				SUN60I_DE_UI_TFBD_BASE);
	sun60i_de_rcq_fill_head(&heads[idx++], ccsc_dma, SUN60I_DE_RCQ_CCSC_CTL_SIZE,
				SUN60I_DE_UI_CCSC_BASE);

	WARN_ON(idx != SUN60I_DE_RCQ_BLOCK_COUNT);

	de->rcq_ready = true;
	de->rcq_nblocks = SUN60I_DE_RCQ_BLOCK_COUNT;
	de->rcq_alloc_size = size;

	return 0;
}

/*
 * DE352 RCQ updates must land in the vertical blanking window (BSP
 * rcq_wait_line=true).  Pulsing RCQ mid-frame can leave stale/white pixels.
 */
static void sun60i_de_rcq_wait_safe_line(struct sun60i_de *de)
{
	u32 status;
	u32 status_reg = SUN60I_DE_GLB_STS_OFFSET +
			 de->display_id * SUN60I_DE_DISP_STRIDE;

	if (!de->rtmx_running)
		return;

	/*
	 * BSP submits RCQ from the display sync tasklet.  Use RTMX frame-end as
	 * the same synchronization point: clear the stale W1C flag, wait for a
	 * fresh frame boundary, then pulse RCQ.  A timeout is non-fatal during
	 * the first enable because TCON may not have started scanning yet.
	 */
	sun60i_de_writel(de, status_reg, SUN60I_DE_FRAME_END);
	if (readl_poll_timeout_atomic(de->regs + status_reg, status,
				      status & SUN60I_DE_FRAME_END,
				      50, 50000))
		dev_dbg(de->dev, "de: no frame-end before RCQ update\n");
}

/*
 * Initialise the RCQ head dirty flags only.  The head pointer is pushed into
 * the RCQ FIFO by sun60i_de_rcq_submit() on every update (BSP re-pushes each
 * request), so we must not push it here as well or the FIFO would double-count.
 */
static void sun60i_de_rcq_program_head(struct sun60i_de *de)
{
	struct sun60i_de_rcq_head *heads = de->rcq_cpu;
	u32 head_reg = SUN60I_DE_RCQ_HEADER_LADDR_OFFSET +
		       de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 head_len;
	unsigned int i, nblocks;

	nblocks = de->rcq_nblocks ? de->rcq_nblocks : SUN60I_DE_RCQ_BLOCK_COUNT;
	head_len = ALIGN(nblocks, 2) * SUN60I_DE_RCQ_HEAD_SIZE;
	head_len = ALIGN(head_len, SUN60I_DE_RCQ_HEAD_SIZE * 2);

	for (i = 0; i < nblocks; i++)
		heads[i].dirty = 0;

	sun60i_de_rcq_sync_for_device(de);

	/* The hardware requires the RCQ length to be written last. */
	sun60i_de_writel(de, head_reg, lower_32_bits(de->rcq_dma));
	sun60i_de_writel(de, head_reg + 0x4, upper_32_bits(de->rcq_dma));
	sun60i_de_writel(de, head_reg + 0x8, head_len);
}

static void sun60i_de_rcq_submit(struct sun60i_de *de)
{
	struct sun60i_de_rcq_head *heads = de->rcq_cpu;
	u32 ctl_reg = SUN60I_DE_RCQ_CTL_OFFSET +
		      de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 status_reg = SUN60I_DE_GLB_STS_OFFSET +
			 de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 status;
	unsigned int i, nblocks;

	if (!de->rcq_head_done)
		return;

	nblocks = de->rcq_nblocks ? de->rcq_nblocks : SUN60I_DE_RCQ_BLOCK_COUNT;

	for (i = 0; i < nblocks; i++)
		heads[i].dirty = 1;

	/*
	 * dma_alloc_wc() memory is write-combining: flush the CPU write buffer
	 * to DRAM so the RCQ DMA engine reads the freshly filled head+shadow,
	 * not stale/zero data (a zeroed shadow disables BLD -> black screen).
	 */
	wmb();
	sun60i_de_rcq_sync_for_device(de);

	sun60i_de_rcq_wait_safe_line(de);

	/* Do not mistake a previous request's sticky W1C bits for completion. */
	sun60i_de_writel(de, status_reg,
			 SUN60I_DE_RCQ_ACCEPT | SUN60I_DE_RCQ_FINISH);

	/*
	 * DE352 (Android dump): RESERVE_CTL=0 (no RCQ FIFO), head programmed
	 * once @0x8114, updates via de_top_set_rcq_update() = RCQ_CTL bit0
	 * only.  RCQ FIFO + head re-push is de355-only.
	 */
	sun60i_de_writel(de, ctl_reg, BIT(0));

	if (readl_poll_timeout_atomic(de->regs + status_reg, status,
				      status & SUN60I_DE_RCQ_FINISH,
				      10, 50000)) {
		dev_err_ratelimited(de->dev,
				    "de: RCQ update timed out (status=0x%08x)\n",
				    status);
		return;
	}

	/* Ack this request so the next submit must observe a fresh completion. */
	sun60i_de_writel(de, status_reg,
			 status & (SUN60I_DE_RCQ_ACCEPT | SUN60I_DE_RCQ_FINISH));
}

enum sun60i_de_rgb_format {
	SUN60I_DE_FMT_ARGB8888 = 0x00,
	SUN60I_DE_FMT_XRGB8888 = 0x04,
	SUN60I_DE_FMT_RGB565 = 0x0a,
};

/* VSU8: BSP disables for RGB 1:1; partial enable → fixed green on A733 */
static bool sun60i_de_needs_scaler(struct sun60i_de *de)
{
	/*
	 * BSP de_rtmx_chn_fix_size(): RGB/YUV444 with ovl_out == scn_out
	 * leaves scale_en=0 and de_scaler_disable() — VSU8 must not run with
	 * partial setup (missing C-plane + FIR coeffs → fixed green garbage).
	 */
	if (!de->scanout.enabled)
		return false;

	return de->scanout.src_w != de->mode.crtc_hdisplay ||
	       de->scanout.src_h != de->mode.crtc_vdisplay;
}

static void sun60i_de_hw_program_vsu8(struct sun60i_de *de)
{
	u32 size;

	if (!de->rcq_vsu_shadow)
		return;

	memset(de->rcq_vsu_shadow, 0, SUN60I_DE_VSU_SHADOW_SIZE);

	if (!sun60i_de_needs_scaler(de)) {
		/*
		 * A523 patch 0009: 1:1 @1080p bypass VSU (en=0).  Line buffer
		 * is 2560px; unity step is only needed above that width.
		 */
		sun60i_de_vsu_writel(de, 0, 0);

		return;
	}

	size = sun60i_de_pack_size(de->mode.crtc_hdisplay, de->mode.crtc_vdisplay);

	sun60i_de_vsu_writel(de, 0, SUN60I_DE_VSU8_CTL_EN);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_SCALE_MODE_OFF, 0);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_OUT_SIZE_OFF, size);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_GLB_ALPHA_OFF, 0xff);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_Y_IN_SIZE_OFF, size);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_Y_HSTEP_OFF, SUN60I_DE_VSU8_STEP_1TO1);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_Y_VSTEP_OFF, SUN60I_DE_VSU8_STEP_1TO1);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_CPARA_OFF, size);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_CPARA_OFF + 0x08, SUN60I_DE_VSU8_STEP_1TO1);
	sun60i_de_vsu_writel(de, SUN60I_DE_VSU8_CPARA_OFF + 0x0c, SUN60I_DE_VSU8_STEP_1TO1);
}

/* UI channel: BSP leaves channel CSC off ("ui channel no csc") */
static void sun60i_de_hw_program_ccsc(struct sun60i_de *de)
{
	if (!de->rcq_ccsc_shadow)
		return;

	memset(de->rcq_ccsc_shadow, 0, SUN60I_DE_RCQ_CCSC_CTL_SIZE);
	sun60i_de_ccsc_writel(de, SUN60I_DE_CCSC_CTL_OFF, 0);

}

static u32 sun60i_de_pack_coord(u32 x, u32 y)
{
	return (x & 0xffff) | ((y & 0xffff) << 16);
}

int sun60i_de_hw_bus_init(struct sun60i_de *de)
{
	if (!de->regs)
		return -EINVAL;

	/* Avoid reads until the DE module clock and reset are enabled. */
	if (!de->clk_mixer) {
		u32 bit = BIT(de->display_id * SUN60I_DE_DISP_CLK_SHIFT);

		/* Keyed enable: BIT(16) | display gate — write-only. */
		sun60i_de_writel(de, SUN60I_DE_CLK_OFFSET, bit | BIT(16));
		sun60i_de_writel(de, SUN60I_DE_RESET_OFFSET, bit | BIT(16));
		udelay(50);
	}

	if (!de->clk_bus_mixer) {
		sun60i_de_writel(de, SUN60I_DE_MBUS_CLK_OFFSET,
				 SUN60I_DE_MBUS_RESET_DEASSERT);
		udelay(10);
		sun60i_de_writel(de, SUN60I_DE_MBUS_CLK_OFFSET,
				 SUN60I_DE_MBUS_RESET_DEASSERT |
				 SUN60I_DE_MBUS_CLK_EN);
	}

	sun60i_de_writel(de, SUN60I_DE_RESERVE_CTL_OFFSET,
			 SUN60I_DE_RESERVE_CTL_AHB_CONFLICT);

	return 0;
}

int sun60i_de_hw_bus_verify(struct sun60i_de *de)
{
	if (!de->regs)
		return -EINVAL;

	return 0;
}

/*
 * DE352 UI path (DRM de_top_set_chn_mux_v2 + port2chn):
 *   uch2core @0x802c: RMW width-2 at (type_id<<2), value=disp
 *   port2chn @0x8030+disp*4: RMW width-4 at (port<<2), value=type_id+8 (UI)
 * type_id 0 = first UI (= phys chn 6 on DE352 map).
 */
static void sun60i_de_top_setup_chn_mux(struct sun60i_de *de)
{
	const u32 type_id = de->ui_channel; /* 0 for primary UI */
	const u32 port = de->bld_port;
	const u32 hw_disp = de->display_id;
	u32 port_reg = SUN60I_DE_PORT2CHN_MUX_OFFSET + de->display_id * 0x4;

	/* Match DRM v2: UI adds +8 in the port→chn table. */
	sun60i_de_rmw_bits(de, SUN60I_DE_UCH2CORE_MUX_OFFSET,
			   type_id << 2, 2, hw_disp);
	sun60i_de_rmw_bits(de, port_reg, port << 2, 4, type_id + 8);
}

/*
 * Reference steady state: PORT2CHN0=0x0000a810, UCH2CORE=0.
 * Write-only — RMW of this block historically AHB-stalled on A733.
 */
static void sun60i_de_top_enable_chn_gating(struct sun60i_de *de)
{
	/*
	 * DRM BSP comments this out for de352; disp2 SUN60IW2
	 * de_rtmx_set_chn_mux() sets GATING@0x80e8 per channel before RCQ
	 * head. HDMI UI phys6 uses gating id phy+2 = 8.
	 */
	sun60i_de_writel(de, SUN60I_DE_GATING_CTL_OFFSET,
			 SUN60I_DE_HDMI_UI_GATING);
}

static void sun60i_de_top_set_de2tcon_mux(struct sun60i_de *de, u32 tcon)
{
	u32 shift = de->display_id * 4;
	u32 val;

	/*
	 * Write-only (RMW read hung A733). Match reference dump width:
	 * DE2TCON=0x0000fff4 — unused display nibbles = 0xf in the low 16 bits.
	 * Values outside the low 16 bits can stall the interconnect.
	 */
	val = 0xffff;
	val &= ~(0xfu << shift);
	val |= (tcon & 0xf) << shift;
	sun60i_de_writel(de, SUN60I_DE_DE2TCON_MUX_OFFSET, val);
}

static void sun60i_de_top_set_urgency(struct sun60i_de *de)
{
	/*
	 * Preserve the low-half enable bits while setting the urgency depth.
	 */
	sun60i_de_writel(de, SUN60I_DE_URGENCY_CTL_OFFSET, 0x0800ffff);
}

/*
 * DE352 display_config_v2 (REF vmlinux disasm) minus RCQ head:
 *   urgency → BUF → GLB → AUTO(bit0) → out_size → [de2tcon]
 * (clk_enable already done in bus_init; support_rcq_gate=0)
 */
static void sun60i_de_top_set_out_size(struct sun60i_de *de)
{
	u32 out_reg = SUN60I_DE_OUT_SIZE_OFFSET +
		      de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 value = sun60i_de_pack_size(de->mode.crtc_hdisplay,
					de->mode.crtc_vdisplay);

	sun60i_de_writel(de, out_reg, value);
}

static void sun60i_de_top_auto_clk_write(struct sun60i_de *de, bool enable,
					 bool rcq_gate)
{
	u32 auto_reg = SUN60I_DE_AUTO_CLK_OFFSET +
		       de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 val = 0;

	if (enable)
		val |= BIT(0);
	if (rcq_gate)
		val |= SUN60I_DE_AUTO_CLK_RCQ_GATE;

	/* Write-only: avoid DETOP read in RTMX window. */
	sun60i_de_writel(de, auto_reg, val);
}

static void sun60i_de_top_glb_rtmx_write(struct sun60i_de *de, bool enable)
{
	u32 glb_reg = SUN60I_DE_GLB_CTL_OFFSET +
		      de->display_id * SUN60I_DE_DISP_STRIDE;
	u32 glb_ctl = 0;

	if (enable) {
		glb_ctl = SUN60I_DE_GLB_CTL_RTMX_EN |
			  (0 << SUN60I_DE_GLB_CTL_PIXEL_MODE_SHIFT);
		if (de->mode.flags & DRM_MODE_FLAG_INTERLACE)
			glb_ctl |= SUN60I_DE_GLB_CTL_FIELD_REVERSE;
	}

	sun60i_de_writel(de, glb_reg, glb_ctl);
}

static void sun60i_de_top_rtmx_buf_depth(struct sun60i_de *de)
{
	sun60i_de_writel(de, SUN60I_DE_BUF_DEPTH_OFFSET, 0x2000);
}

/*
 * DE352 BSP de_top_display_config_v2() order:
 *   urgency → BUF → GLB → AUTO_CLK → OUT_SIZE →
 *   DE2TCON → RCQ_HEAD (caller) → channel mux (caller)
 *
 * This intentionally differs from sun55i.  On A733, arming RCQ_HEAD before
 * DE2TCON causes the following DETOP write to stall the interconnect during
 * the first cold-boot fbdev commit.
 *
 */
static void sun60i_de_top_rtmx_enable_atomic(struct sun60i_de *de)
{
	sun60i_de_top_glb_rtmx_write(de, true);
	sun60i_de_top_auto_clk_write(de, true, false);
	sun60i_de_top_set_out_size(de);
	sun60i_de_top_set_de2tcon_mux(de, de->tcon_id);
	/* Make all DETOP routing writes visible before RCQ is armed. */
	wmb();
}

static void sun60i_de_top_rtmx_enable(struct sun60i_de *de, bool enable)
{
	if (!enable) {
		sun60i_de_top_glb_rtmx_write(de, false);
		sun60i_de_top_auto_clk_write(de, false, false);
		return;
	}

	sun60i_de_top_rtmx_enable_atomic(de);
}

/*
 * DE352 top (0x8100 block) — BSP references:
 *
 * A733 programming order:
 *   bus_init:  mod_clk/mbus/RESERVE_CTL only
 *   commit:    RCQ pool+head (19 blocks) → shadow → hdmi chn_mux → gating
 *              → async/buf → GLB+AUTO → de2tcon → pulse
 */
static int sun60i_de_map_format(u32 drm_format, u32 *de_format,
				u32 *alpha_mode)
{
	switch (drm_format) {
	case DRM_FORMAT_ARGB8888:
		*de_format = SUN60I_DE_FMT_ARGB8888;
		*alpha_mode = 2;
		return 0;
	case DRM_FORMAT_XRGB8888:
		*de_format = SUN60I_DE_FMT_XRGB8888;
		*alpha_mode = 1;
		return 0;
	case DRM_FORMAT_RGB565:
		*de_format = SUN60I_DE_FMT_RGB565;
		*alpha_mode = 1;
		return 0;
	default:
		return -EINVAL;
	}
}

static void sun60i_de_program_blender(struct sun60i_de *de, bool enabled)
{
	bool interlace = !!(de->mode.flags & DRM_MODE_FLAG_INTERLACE);
	u32 route = 0x00ffffff;
	u32 out_ctl = SUN60I_DE_BLD_OUT_CTL_FMT_RGB;
	u32 blend = SUN60I_DE_BLD_MODE_SRCOVER;
	u32 bld_en;
	u32 pipe_fcolor;

	if (interlace)
		out_ctl |= SUN60I_DE_BLD_OUT_CTL_INTERLACE;

	if (!de->rcq_bld_shadow)
		return;

	if (enabled) {
		route &= ~GENMASK(3, 0);
		route |= de->bld_port;
	}

	if (!enabled) {
		bld_en = 0;
		pipe_fcolor = 0;
	} else {
		/* BSP de_bld_output_set_attr(): RGB pipes use FCOLOR black bg + SRCOVER */
		bld_en = SUN60I_DE_BLD_PIPE_EN(0) | SUN60I_DE_BLD_FCOLOR_EN(0);
		pipe_fcolor = 0xff000000;
		blend = SUN60I_DE_BLD_MODE_SRCOVER;
	}

	sun60i_de_bld_writel(de, SUN60I_DE_BLD_EN, bld_en);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_PIPE_FCOLOR(0), pipe_fcolor);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_PIPE_IN_SIZE(0),
			     enabled ? sun60i_de_pack_size(de->mode.crtc_hdisplay,
							   de->mode.crtc_vdisplay) : 0);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_PIPE_IN_COORD(0),
			     0);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_ROUT_CTL, route);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_PREMUL_CTL, 0);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_BG_COLOR, 0);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_OUT_SIZE,
			     sun60i_de_pack_size(de->mode.crtc_hdisplay,
						 de->mode.crtc_vdisplay));
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_BLEND_CTL(0), blend);
	sun60i_de_bld_writel(de, SUN60I_DE_BLD_OUT_CTL, out_ctl);
}

static void sun60i_de_disable_overlay(struct sun60i_de *de)
{
	unsigned int i;

	if (!de->rcq_ovl_shadow)
		return;

	for (i = 0; i < 4; i++) {
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_CTL(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_MBSIZE(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_MBCOOR(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_PITCH(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_TOP_LADDR(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_BOT_LADDR(i), 0);
		sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_FCOLOR(i), 0);
	}

	sun60i_de_ovl_writel(de, SUN60I_DE_UI_TOP_HADDR, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_BOT_HADDR, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_WIN_SIZE, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_HORI_DS, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_VERT_DS, 0);
	/* DS RCQ block @0xE0 includes hori/vert_ds; clear whole region */
	memset((u8 *)de->rcq_ovl_shadow + SUN60I_DE_RCQ_OVL_DS_OFF, 0,
	       SUN60I_DE_RCQ_OVL_DS_SIZE);
}

static int sun60i_de_program_overlay_layer(struct sun60i_de *de,
					   const struct sun60i_de_scanout_state *scanout,
					   unsigned int layer, u32 *top_haddr)
{
	u32 de_format;
	u32 alpha_mode;
	u32 ctl;
	int ret;

	if (!scanout->enabled)
		return 0;

	if (scanout->src_w != scanout->crtc_w ||
	    scanout->src_h != scanout->crtc_h)
		return -EINVAL;

	ret = sun60i_de_map_format(scanout->format, &de_format, &alpha_mode);
	if (ret)
		return ret;

	ctl = SUN60I_DE_UI_CTL_EN |
	      (alpha_mode << SUN60I_DE_UI_CTL_ALPHA_MODE_SHIFT) |
	      (de_format << SUN60I_DE_UI_CTL_FMT_SHIFT) |
	      (0xff << SUN60I_DE_UI_CTL_GLB_ALPHA_SHIFT);

	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_CTL(layer), ctl);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_MBSIZE(layer),
			     sun60i_de_pack_size(scanout->src_w, scanout->src_h));
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_MBCOOR(layer),
			     sun60i_de_pack_coord(scanout->crtc_x, scanout->crtc_y));
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_PITCH(layer), scanout->pitch);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_TOP_LADDR(layer),
			     lower_32_bits(scanout->addr));
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_BOT_LADDR(layer), 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_LAY_FCOLOR(layer), 0);

	*top_haddr |= (upper_32_bits(scanout->addr) & 0xff) << (layer * 8);

	return 0;
}

static int sun60i_de_program_overlay(struct sun60i_de *de)
{
	u32 top_haddr = 0;
	int ret;

	if (!de->rcq_ovl_shadow)
		return -EINVAL;

	memset(de->rcq_ovl_shadow, 0, SUN60I_DE_RCQ_OVL_SHADOW_SIZE);
	sun60i_de_disable_overlay(de);

	ret = sun60i_de_program_overlay_layer(de, &de->scanout, 0, &top_haddr);
	if (ret)
		return ret;

	ret = sun60i_de_program_overlay_layer(de, &de->cursor_scanout, 1,
					     &top_haddr);
	if (ret)
		return ret;

	sun60i_de_ovl_writel(de, SUN60I_DE_UI_TOP_HADDR, top_haddr);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_BOT_HADDR, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_WIN_SIZE,
			     sun60i_de_pack_size(de->mode.crtc_hdisplay,
						 de->mode.crtc_vdisplay));
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_HORI_DS, 0);
	sun60i_de_ovl_writel(de, SUN60I_DE_UI_VERT_DS, 0);

	return 0;
}

static void sun60i_de_sync_one_scanout(struct sun60i_de *de,
				       const struct sun60i_de_scanout_state *scanout)
{
	struct device *dma_dev = sun60i_de_dma_dev(de);
	size_t size;

	if (!scanout->enabled || !scanout->addr || !scanout->pitch)
		return;

	size = (size_t)scanout->pitch * scanout->src_h;
	dma_sync_single_for_device(dma_dev, scanout->addr, size,
				   DMA_TO_DEVICE);
}

static void sun60i_de_sync_scanout(struct sun60i_de *de)
{
	sun60i_de_sync_one_scanout(de, &de->scanout);
	sun60i_de_sync_one_scanout(de, &de->cursor_scanout);
}

int sun60i_de_hw_commit(struct sun60i_de *de)
{
	bool full_setup;
	bool first_rtmx;
	int ret;

	if (!de->mode_valid)
		return 0;

	/*
	 * sun55i_de_enable() order:
	 *   urgency → RCQ head → de2tcon → buf → chn_mux → size → AUTO → GLB
	 * Then shadows + RCQ submit.  All DETOP updates are RMW.
	 */
	full_setup = !de->rtmx_running ||
		     de->programmed_hdisplay != de->mode.crtc_hdisplay ||
		     de->programmed_vdisplay != de->mode.crtc_vdisplay;
	first_rtmx = !de->rtmx_running;

	sun60i_de_top_set_urgency(de);
	/*
	 * Complete the DE352 static display transaction before RCQ_HEAD.  The
	 * BSP enables GLB/AUTO and routes DE2TCON first; doing DE2TCON after the
	 * head is armed hard-stalls A733 on the first fbdev modeset.
	 */
	if (first_rtmx) {
		sun60i_de_top_rtmx_buf_depth(de);
		sun60i_de_top_rtmx_enable_atomic(de);
		de->rtmx_running = true;
		de->programmed_hdisplay = de->mode.crtc_hdisplay;
		de->programmed_vdisplay = de->mode.crtc_vdisplay;
	}

	ret = sun60i_de_rcq_init(de);
	if (ret)
		return ret;

	sun60i_de_sync_scanout(de);

	if (full_setup) {
		sun60i_de_hw_program_fmt(de);
		sun60i_de_hw_program_vsu8(de);
		sun60i_de_hw_program_tfbd(de);
		sun60i_de_hw_program_ccsc(de);
		de->programmed_hdisplay = de->mode.crtc_hdisplay;
		de->programmed_vdisplay = de->mode.crtc_vdisplay;
	}

	/* Rebuild the complete layer state because the shadow starts cleared. */
	ret = sun60i_de_program_overlay(de);
	if (ret)
		return ret;

	sun60i_de_program_blender(de, de->scanout.enabled ||
				 de->cursor_scanout.enabled);
	if (!de->rcq_head_done) {
		sun60i_de_rcq_program_head(de);
		de->rcq_head_done = true;
	}

	/* BSP configures channel routing after the RCQ head is installed. */
	if (full_setup) {
		sun60i_de_top_setup_chn_mux(de);
		sun60i_de_top_enable_chn_gating(de);
	}

	/* First setup already programmed OUT_SIZE before RCQ_HEAD. */
	if (!first_rtmx)
		sun60i_de_top_set_out_size(de);

	sun60i_de_rcq_submit(de);

	return 0;
}

void sun60i_de_hw_disable(struct sun60i_de *de)
{
	sun60i_de_disable_overlay(de);
	sun60i_de_program_blender(de, false);
	/* Latch the disabled plane state before stopping RTMX/freeing its RCQ. */
	sun60i_de_rcq_submit(de);
	sun60i_de_top_rtmx_enable(de, false);
	/*
	 * Module, MBUS and reset lifetime belongs to display_clocks and the
	 * clk/reset framework.  Do not modify those gates directly here: the
	 * caller drops the matching clock/reset references after RTMX is idle.
	 */

	de->rtmx_running = false;
	de->rcq_head_done = false;
	de->programmed_hdisplay = 0;
	de->programmed_vdisplay = 0;
	sun60i_de_rcq_free(de);
}
