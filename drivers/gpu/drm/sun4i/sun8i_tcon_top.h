/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net> */

#ifndef _SUN8I_TCON_TOP_H_
#define _SUN8I_TCON_TOP_H_

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

/* VIDEO_OUT1_SYS @0x00 — A733 User Manual 15.2.9.1 */
#define TCON_TOP_TCON_TV_SETUP_REG	0x00
#define TCON_TOP_TV0_CLK_SRC_CVBS		BIT(0)	/* 0=CCMU, 1=CVBS */
#define TCON_TOP_TV0_EDP_HDMI_CK_SEL		BIT(1)	/* 0=HDMI, 1=EDP */
#define TCON_TOP_TV0_HDMIPHY_CCU_CK_SEL		BIT(3)	/* 0=HDMI PHY, 1=CCU */

#define TCON_TOP_PORT_SEL_REG		0x1C
#define TCON_TOP_PORT_DE0_MSK			GENMASK(1, 0)
#define TCON_TOP_PORT_DE1_MSK			GENMASK(5, 4)

#define TCON_TOP_GATE_SRC_REG		0x20
#define TCON_TOP_HDMI_SRC_MSK			GENMASK(29, 28)
#define TCON_TOP_TCON_TV1_GATE			24
#define TCON_TOP_TCON_TV0_GATE			20
#define TCON_TOP_TCON_DSI_GATE			16
/* A733/sun60iw2: bit28 is HDMI pixel-path gate, not H6 hdmi_src mux. */
#define TCON_TOP_TV0_HDMI_GATE			28

#define CLK_NUM					3

struct sun8i_tcon_top {
	struct clk			*bus;
	struct clk			*ahb_gate;
	struct clk_hw_onecell_data	*clk_data;
	void __iomem			*regs;
	struct reset_control		*rst;
	struct reset_control		*rst_reg;

	/*
	 * spinlock is used to synchronize access to same
	 * register where multiple clock gates can be set.
	 */
	spinlock_t			reg_lock;
};

extern const struct of_device_id sun8i_tcon_top_of_table[];

int sun8i_tcon_top_set_hdmi_src(struct device *dev, int tcon);
int sun8i_tcon_top_de_config(struct device *dev, int mixer, int tcon);
int sun8i_tcon_top_hdmi_gate_enable(struct device *dev, bool enable);

#endif /* _SUN8I_TCON_TOP_H_ */
