// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * sun60i internal DWC HDMI PHY programming (from BSP phy_snps.c).
 *
 * A733 uses top-PHY for the pixel PLL. BSP sun60i skips snps init reset and
 * still programs MPLL/drive over internal PHY I2CM, then GEN2 power + ENTMDS.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>


#include "sun60i_hdmi.h"

/* DW-HDMI register offsets (from synopsys/dw-hdmi.h, not in UAPI header) */
#define SUN60I_DW_HDMI_PHY_CONF0		0x3000
#define SUN60I_DW_HDMI_PHY_STAT0		0x3004
#define SUN60I_DW_HDMI_PHY_CONF0_ENTMDS		BIT(6)
#define SUN60I_DW_HDMI_PHY_CONF0_SVSRET		BIT(5)
#define SUN60I_DW_HDMI_PHY_CONF0_GEN2_PDDQ	BIT(4)
#define SUN60I_DW_HDMI_PHY_CONF0_GEN2_TXPWRON	BIT(3)
#define SUN60I_DW_HDMI_PHY_CONF0_SELDATAENPOL	BIT(1)
#define SUN60I_DW_HDMI_PHY_CONF0_SELDIPIF	BIT(0)
#define SUN60I_DW_HDMI_PHY_CONF0_ENHPDRXSENSE	BIT(2)
#define SUN60I_DW_HDMI_PHY_TX_PHY_LOCK		BIT(0)
#define SUN60I_DW_HDMI_PHY_I2CM_SLAVE		0x3020
#define SUN60I_DW_HDMI_PHY_I2CM_SLAVE_GEN2	0x69
#define SUN60I_DW_HDMI_JTAG_PHY_CONFIG		0x3034
#define SUN60I_DW_HDMI_JTAG_I2C_MODE		BIT(4)
#define SUN60I_DW_HDMI_IH_I2CMPHY_STAT0		0x0108
#define SUN60I_DW_HDMI_IH_MUTE_I2CMPHY_STAT0	0x0188
#define SUN60I_DW_HDMI_PHY_I2CM_ADDRESS		0x3021
#define SUN60I_DW_HDMI_PHY_I2CM_DATAO_1		0x3022
#define SUN60I_DW_HDMI_PHY_I2CM_DATAO_0		0x3023
#define SUN60I_DW_HDMI_PHY_I2CM_DATAI_1		0x3024
#define SUN60I_DW_HDMI_PHY_I2CM_DATAI_0		0x3025
#define SUN60I_DW_HDMI_PHY_I2CM_OPERATION	0x3026
#define SUN60I_DW_HDMI_PHY_I2CM_INT		0x3027
#define SUN60I_DW_HDMI_PHY_I2CM_CTLINT		0x3028
#define SUN60I_DW_HDMI_PHY_I2CM_DIV		0x3029
#define SUN60I_DW_HDMI_PHY_I2CM_SOFTRSTZ	0x302a
#define SUN60I_PHY_I2CM_DONE_STATUS		BIT(0)
#define SUN60I_PHY_I2CM_NACK_STATUS		BIT(4)
#define SUN60I_PHY_I2CM_ARB_STATUS		BIT(0)
#define SUN60I_I2CMPHY_DONE			BIT(1)
#define SUN60I_I2CMPHY_ERROR			BIT(0)
#define SUN60I_DW_HDMI_MC_CLKDIS		0x4001
#define SUN60I_DW_HDMI_MC_PHYRSTZ		0x4005
#define SUN60I_MC_PHYRSTZ_ASSERT		0x00
#define SUN60I_MC_PHYRSTZ_DEASSERT		0x01

#define SUN60I_SNPS_MPLL_REG1		0x06
#define SUN60I_SNPS_MPLL_REG2		0x10
#define SUN60I_SNPS_MPLL_REG3		0x11
#define SUN60I_SNPS_DRIVE_REG1		0x19
#define SUN60I_SNPS_DRIVE_REG2		0x0e
#define SUN60I_SNPS_DRIVE_REG3		0x09

struct sun60i_snps_mpll_entry {
	unsigned int pixel_khz;
	u16 data[3];
};

struct sun60i_snps_drive_entry {
	unsigned int min_khz;
	unsigned int max_khz;
	u16 data[3];
};

/* sun60i_mpll_rep0 @ 8bpp (C422-0), NOT sun50i values */
static const struct sun60i_snps_mpll_entry sun60i_snps_mpll[] = {
	{ 148500, { 0x0001, 0x2080, 0x020a } },
	{ 154000, { 0x0001, 0x2080, 0x020a } }, /* sun60i: same as 148500/165000 */
	{ 165000, { 0x0001, 0x2080, 0x020a } },
	{ 297000, { 0x0000, 0x3041, 0x0205 } },
};

/* sun60i_drive[] from BSP phy_snps.c */
static const struct sun60i_snps_drive_entry sun60i_snps_drive[] = {
	{  25000, 165000, { 0x0007, 0x8160, 0x8188 } },
	{ 165000, 340000, { 0x0004, 0x8040, 0x8e85 } },
	{ 340000, 600000, { 0x0000, 0x80c0, 0x82f6 } },
};

static void sun60i_snps_phy_set_bit(struct sun60i_hdmi *hdmi, u32 reg,
				      u32 mask, bool on)
{
	u8 tmp = readb(hdmi->regs + reg);

	if (on)
		tmp |= mask;
	else
		tmp &= ~mask;
	writeb(tmp, hdmi->regs + reg);
}

static void sun60i_snps_phy_write_mask(struct sun60i_hdmi *hdmi, u32 reg,
				       u8 mask, u8 val)
{
	u8 tmp = readb(hdmi->regs + reg);

	tmp &= ~mask;
	tmp |= mask & (val << (ffs(mask) - 1));
	writeb(tmp, hdmi->regs + reg);
}

static void sun60i_snps_phy_set_power(struct sun60i_hdmi *hdmi, bool on)
{
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_GEN2_PDDQ, !on);
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_GEN2_TXPWRON, on);
}

static void sun60i_snps_phy_config_svsret(struct sun60i_hdmi *hdmi)
{
	/*
	 * BSP dw_phy_config_svsret() + dw_hdmi PHY configure path: leave
	 * SVSRET asserted before I2C/MPLL programming. Without it CONF0 stays
	 * ~0x4E and internal TX_PHY_LOCK (STAT0 bit0) never sets on A733.
	 */
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_SVSRET, false);
	udelay(5);
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_SVSRET, true);
}

static void sun60i_snps_phy_mc_reset(struct sun60i_hdmi *hdmi, bool deassert)
{
	writeb(deassert ? SUN60I_MC_PHYRSTZ_DEASSERT : SUN60I_MC_PHYRSTZ_ASSERT,
	       hdmi->regs + SUN60I_DW_HDMI_MC_PHYRSTZ);
}

static void sun60i_snps_phy_prepare_interface(struct sun60i_hdmi *hdmi)
{
	/*
	 * BSP dw_phy_config_interface(): select internal PHY I2C and program
	 * slave address directly. Do NOT use dw_hdmi_phy_i2c_set_addr() — its
	 * hdmi_phy_test_clear() pulse breaks I2CM on sun60i.
	 */
	writeb(SUN60I_DW_HDMI_JTAG_I2C_MODE,
	       hdmi->regs + SUN60I_DW_HDMI_JTAG_PHY_CONFIG);
	writeb(SUN60I_DW_HDMI_PHY_I2CM_SLAVE_GEN2,
	       hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_SLAVE);

	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_SELDATAENPOL, true);
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_SELDIPIF, false);
}

static void sun60i_snps_phy_mute_i2c_irq(struct sun60i_hdmi *hdmi)
{
	/*
	 * PHY-I2CM is polled locally while the Linux IRQ is disabled.  Mask
	 * only that sub-source.  Setting IH_MUTE.MUTE_ALL here also masks the
	 * external DDC master; its transfer reaches DONE, but dw-hdmi never
	 * receives the completion IRQ and consequently falls back to no-EDID
	 * modes such as 1024x768.
	 */
	writeb(0xff, hdmi->regs + SUN60I_DW_HDMI_IH_MUTE_I2CMPHY_STAT0);
}

static void sun60i_snps_phy_irq_begin(struct sun60i_hdmi *hdmi)
{
	if (hdmi->irq <= 0 || hdmi->irq_snps_masked)
		return;

	disable_irq_nosync(hdmi->irq);
	hdmi->irq_snps_masked = true;
}

static void sun60i_snps_phy_irq_end(struct sun60i_hdmi *hdmi)
{
	if (hdmi->irq <= 0 || !hdmi->irq_snps_masked)
		return;

	enable_irq(hdmi->irq);
	hdmi->irq_snps_masked = false;
}

static void sun60i_snps_enable_mc_clocks(struct sun60i_hdmi *hdmi)
{
	writeb(0x00, hdmi->regs + SUN60I_DW_HDMI_MC_CLKDIS);
}

static void sun60i_snps_phy_gen2_reset(struct dw_hdmi *dw_hdmi,
				      struct sun60i_hdmi *hdmi)
{
	/*
	 * sun8i A83T path before internal PHY I2C. BSP sun60i skips snps init
	 * reset but A733 may still need MC PHY reset to kick I2CM.
	 */
	if (dw_hdmi)
		dw_hdmi_phy_gen2_reset(dw_hdmi);
	else if (hdmi && hdmi->regs)
		writeb(SUN60I_MC_PHYRSTZ_DEASSERT, hdmi->regs + SUN60I_DW_HDMI_MC_PHYRSTZ);

	udelay(10);
}

static void sun60i_snps_phy_assert_power(struct dw_hdmi *dw_hdmi,
					 struct sun60i_hdmi *hdmi, bool on)
{
	sun60i_snps_phy_set_power(hdmi, on);
	if (!dw_hdmi)
		return;

	dw_hdmi_phy_gen2_txpwron(dw_hdmi, on ? 1 : 0);
	dw_hdmi_phy_gen2_pddq(dw_hdmi, on ? 0 : 1);
}

static void sun60i_snps_phy_config_init(struct dw_hdmi *dw_hdmi,
					struct sun60i_hdmi *hdmi)
{
	/*
	 * BSP _snps_phy_config_init(): MC PHY reset, power down, SVSRET pulse,
	 * hold PHY in reset, then arm I2C interface for MPLL/drive writes.
	 */
	sun60i_snps_phy_mc_reset(hdmi, true);
	sun60i_snps_phy_assert_power(dw_hdmi, hdmi, false);
	sun60i_snps_phy_config_svsret(hdmi);
	sun60i_snps_phy_gen2_reset(dw_hdmi, hdmi);
	sun60i_snps_phy_prepare_interface(hdmi);
}

static void sun60i_snps_phy_init_i2cm_hw(struct sun60i_hdmi *hdmi)
{
	/*
	 * dw_hdmi leaves PHY_I2CM_INT/CTLINT at 0xff after initialize_hdmi_ih_mutes().
	 * disp2 dw_phy_initialize() unmasks (mask=0), sets polarity, standard mode.
	 * Pulse SOFTRSTZ 0->1 to clear a stuck controller; leave deasserted (1).
	 */
	writeb(0x00, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_DIV);
	writeb(0x00, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_SOFTRSTZ);
	udelay(10);
	writeb(0x01, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_SOFTRSTZ);

	writeb(BIT(3), hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_INT);
	writeb(BIT(3) | BIT(7), hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_CTLINT);
	sun60i_snps_phy_write_mask(hdmi, SUN60I_DW_HDMI_PHY_I2CM_INT, BIT(2), 0);
	sun60i_snps_phy_write_mask(hdmi, SUN60I_DW_HDMI_PHY_I2CM_CTLINT, BIT(2), 0);
	sun60i_snps_phy_write_mask(hdmi, SUN60I_DW_HDMI_PHY_I2CM_CTLINT, BIT(6), 0);
	sun60i_snps_phy_mute_i2c_irq(hdmi);
}

static void sun60i_snps_phy_prepare_i2cm(struct sun60i_hdmi *hdmi)
{
	/*
	 * Poll IH_I2CMPHY_STAT0 directly; keep IH muted so dw_hdmi hardirq
	 * does not fire during atomic commit (was: IRQ storm + disable #413).
	 */
	writeb(0xff, hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
	sun60i_snps_phy_mute_i2c_irq(hdmi);
}

static const struct sun60i_snps_mpll_entry *
sun60i_snps_find_mpll(unsigned int pixel_khz)
{
	const struct sun60i_snps_mpll_entry *best = &sun60i_snps_mpll[0];
	unsigned int best_diff = UINT_MAX;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sun60i_snps_mpll); i++) {
		unsigned int diff = abs((int)sun60i_snps_mpll[i].pixel_khz -
					(int)pixel_khz);

		if (diff < best_diff) {
			best = &sun60i_snps_mpll[i];
			best_diff = diff;
		}
	}

	return best;
}

#define SUN60I_SNPS_I2C_PROBE_MS		50
#define SUN60I_SNPS_I2C_XFER_MS			100
#define SUN60I_SNPS_TXREADY_WAIT_MS		200
#define SUN60I_SNPS_PIXEL_SETTLE_MS		20
#define SUN60I_SNPS_I2C_POLL_US			10

static bool sun60i_snps_phy_locked(struct sun60i_hdmi *hdmi)
{
	return !!(readb(hdmi->regs + SUN60I_DW_HDMI_PHY_STAT0) &
		  SUN60I_DW_HDMI_PHY_TX_PHY_LOCK);
}

static void sun60i_snps_phy_finish_tx(struct dw_hdmi *dw_hdmi,
				    struct sun60i_hdmi *hdmi)
{
	sun60i_snps_phy_assert_power(dw_hdmi, hdmi, true);
	sun60i_hdmi_snps_phy_enable_tmds(hdmi);
}

static bool sun60i_snps_wait_internal_lock(struct sun60i_hdmi *hdmi,
					   unsigned int ms)
{
	unsigned int i;

	for (i = 0; i < ms; i++) {
		if (sun60i_snps_phy_locked(hdmi))
			return true;
		udelay(1000);
	}

	return false;
}

static bool sun60i_snps_wait_txready(struct dw_hdmi *dw_hdmi,
				     struct sun60i_hdmi *hdmi)
{
	unsigned int i;

	mdelay(SUN60I_SNPS_PIXEL_SETTLE_MS);
	sun60i_snps_phy_finish_tx(dw_hdmi, hdmi);

	for (i = 0; i < SUN60I_SNPS_TXREADY_WAIT_MS; i++) {
		if (sun60i_hdmi_top_phy_wait_txready(hdmi, 1))
			return true;

		udelay(1000);
	}

	return false;
}

static bool sun60i_snps_phy_i2c_error(struct sun60i_hdmi *hdmi)
{
	u8 ctl = readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_CTLINT);

	return !!(ctl & (SUN60I_PHY_I2CM_NACK_STATUS |
			 SUN60I_PHY_I2CM_ARB_STATUS));
}

static bool sun60i_snps_phy_i2c_done_ms(struct sun60i_hdmi *hdmi,
					unsigned int ms)
{
	u8 ih_stat = 0;
	u8 phy_int = 0;
	unsigned int tries = ms * (1000000 / SUN60I_SNPS_I2C_POLL_US);
	unsigned int i;

	for (i = 0; i < tries; i++) {
		ih_stat = readb(hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
		phy_int = readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_INT);

		if (ih_stat & (SUN60I_I2CMPHY_DONE | SUN60I_I2CMPHY_ERROR))
			break;
		if (phy_int & SUN60I_PHY_I2CM_DONE_STATUS)
			break;
		if (sun60i_snps_phy_i2c_error(hdmi))
			break;

		udelay(SUN60I_SNPS_I2C_POLL_US);
	}

	if (sun60i_snps_phy_i2c_error(hdmi))
		return false;

	if (phy_int & SUN60I_PHY_I2CM_DONE_STATUS) {
		if (ih_stat)
			writeb(ih_stat, hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
		return true;
	}

	if (!(ih_stat & (SUN60I_I2CMPHY_DONE | SUN60I_I2CMPHY_ERROR)))
		return false;

	writeb(ih_stat, hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
	return (ih_stat & SUN60I_I2CMPHY_DONE) && !(ih_stat & SUN60I_I2CMPHY_ERROR);
}

static void sun60i_snps_phy_log_i2cm(struct sun60i_hdmi *hdmi, const char *tag)
{
	dev_warn(hdmi->dev,
		 "snps-phy: %s slave=0x%02x jtag=0x%02x ih=0x%02x pint=0x%02x ctl=0x%02x op=0x%02x ih_mute=0x%02x conf0=0x%02x\n",
		 tag,
		 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_SLAVE),
		 readb(hdmi->regs + SUN60I_DW_HDMI_JTAG_PHY_CONFIG),
		 readb(hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0),
		 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_INT),
		 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_CTLINT),
		 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_OPERATION),
		 readb(hdmi->regs + SUN60I_DW_HDMI_IH_MUTE_I2CMPHY_STAT0),
		 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_CONF0));
}

static int sun60i_snps_phy_i2c_write_ms(struct sun60i_hdmi *hdmi, u16 data,
					u8 addr, unsigned int wait_ms)
{
	sun60i_snps_phy_prepare_i2cm(hdmi);
	writeb(0xff, hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
	writeb(addr, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_ADDRESS);
	writeb(data >> 8, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_DATAO_1);
	writeb(data & 0xff, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_DATAO_0);
	writeb(0x10, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_OPERATION);

	if (!sun60i_snps_phy_i2c_done_ms(hdmi, wait_ms)) {
		sun60i_snps_phy_log_i2cm(hdmi, "I2C write timeout");
		dev_warn(hdmi->dev, "snps-phy: I2C write 0x%04x @0x%02x failed\n",
			 data, addr);
		return -EIO;
	}

	return 0;
}

static bool sun60i_snps_phy_i2c_probe(struct sun60i_hdmi *hdmi, u16 data,
				      u8 addr)
{
	sun60i_snps_phy_prepare_i2cm(hdmi);
	writeb(0xff, hdmi->regs + SUN60I_DW_HDMI_IH_I2CMPHY_STAT0);
	writeb(addr, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_ADDRESS);
	writeb(data >> 8, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_DATAO_1);
	writeb(data & 0xff, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_DATAO_0);
	writeb(0x10, hdmi->regs + SUN60I_DW_HDMI_PHY_I2CM_OPERATION);

	return sun60i_snps_phy_i2c_done_ms(hdmi, SUN60I_SNPS_I2C_PROBE_MS);
}

static bool sun60i_snps_phy_i2c_probe_mpll(struct sun60i_hdmi *hdmi,
					   unsigned int pixel_khz)
{
	const struct sun60i_snps_mpll_entry *entry = sun60i_snps_find_mpll(pixel_khz);

	return sun60i_snps_phy_i2c_probe(hdmi, entry->data[0],
					 SUN60I_SNPS_MPLL_REG1);
}

static bool sun60i_snps_phy_i2c_alive(struct dw_hdmi *dw_hdmi,
				      struct sun60i_hdmi *hdmi,
				      unsigned int pixel_khz)
{
	bool ok;

	/* config_init() already left analog off and I2CM armed. */
	ok = sun60i_snps_phy_i2c_probe_mpll(hdmi, pixel_khz);
	if (ok)
		return true;

	sun60i_snps_phy_log_i2cm(hdmi, "I2CM probe failed");

	sun60i_snps_phy_gen2_reset(dw_hdmi, hdmi);
	sun60i_snps_phy_init_i2cm_hw(hdmi);
	ok = sun60i_snps_phy_i2c_probe_mpll(hdmi, pixel_khz);
	if (!ok)
		sun60i_snps_phy_log_i2cm(hdmi, "I2CM probe failed retry");

	return ok;
}

static int sun60i_snps_program_mpll(struct sun60i_hdmi *hdmi,
				    unsigned int pixel_khz)
{
	const struct sun60i_snps_mpll_entry *entry = sun60i_snps_find_mpll(pixel_khz);
	int ret;

	ret = sun60i_snps_phy_i2c_write_ms(hdmi, entry->data[0],
					   SUN60I_SNPS_MPLL_REG1, SUN60I_SNPS_I2C_XFER_MS);
	if (ret)
		return ret;
	ret = sun60i_snps_phy_i2c_write_ms(hdmi, entry->data[1],
					   SUN60I_SNPS_MPLL_REG2, SUN60I_SNPS_I2C_XFER_MS);
	if (ret)
		return ret;

	return sun60i_snps_phy_i2c_write_ms(hdmi, entry->data[2],
					    SUN60I_SNPS_MPLL_REG3, SUN60I_SNPS_I2C_XFER_MS);
}

static int sun60i_snps_program_drive(struct sun60i_hdmi *hdmi,
				     unsigned int tmds_khz)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sun60i_snps_drive); i++) {
		const struct sun60i_snps_drive_entry *e = &sun60i_snps_drive[i];
		int ret;

		if (tmds_khz < e->min_khz || tmds_khz >= e->max_khz)
			continue;

		ret = sun60i_snps_phy_i2c_write_ms(hdmi, e->data[0],
						SUN60I_SNPS_DRIVE_REG1,
						SUN60I_SNPS_I2C_XFER_MS);
		if (ret)
			return ret;
		ret = sun60i_snps_phy_i2c_write_ms(hdmi, e->data[1],
						SUN60I_SNPS_DRIVE_REG2,
						SUN60I_SNPS_I2C_XFER_MS);
		if (ret)
			return ret;
		return sun60i_snps_phy_i2c_write_ms(hdmi, e->data[2],
						 SUN60I_SNPS_DRIVE_REG3,
						 SUN60I_SNPS_I2C_XFER_MS);
	}

	return -EINVAL;
}

void sun60i_hdmi_snps_phy_probe_init(struct dw_hdmi *dw_hdmi,
				     struct sun60i_hdmi *hdmi)
{
	if (!dw_hdmi || !hdmi || !hdmi->regs)
		return;

	/*
	 * BSP dw_phy_init(): I2C access + HPD sense. sun60i snps init skips
	 * reset/power in configure; interface is armed once here at bind.
	 */
	sun60i_snps_phy_prepare_interface(hdmi);
	sun60i_snps_phy_init_i2cm_hw(hdmi);
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_ENHPDRXSENSE, true);
}

int sun60i_hdmi_snps_phy_configure(struct dw_hdmi *dw_hdmi,
				   struct sun60i_hdmi *hdmi,
				   const struct drm_display_mode *mode)
{
	unsigned int pixel_khz = mode->clock;
	unsigned int tmds_khz = mode->clock;
	bool i2c_ok;
	int ret;

	if (!dw_hdmi || !hdmi || !hdmi->regs || !mode->clock)
		return -EINVAL;

	sun60i_snps_phy_irq_begin(hdmi);

	/*
	 * BSP sun60i: skip PHY reset in init; top-PHY already configured.
	 * Internal PHY I2CM is often dead on A733 — probe once, else MMIO-only.
	 */
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_ENTMDS, false);

	sun60i_snps_phy_config_init(dw_hdmi, hdmi);
	sun60i_snps_phy_init_i2cm_hw(hdmi);
	sun60i_snps_enable_mc_clocks(hdmi);

	i2c_ok = sun60i_snps_phy_i2c_alive(dw_hdmi, hdmi, pixel_khz);
	if (i2c_ok) {
		/* MPLL/drive while analog off (BSP snps_phy_config order). */
		ret = sun60i_snps_program_mpll(hdmi, pixel_khz);
		if (ret) {
			dev_warn(hdmi->dev,
				 "snps-phy: MPLL I2C failed (%d), continuing MMIO-only\n",
				 ret);
			i2c_ok = false;
		}
		if (i2c_ok) {
			ret = sun60i_snps_program_drive(hdmi, tmds_khz);
			if (ret) {
				dev_warn(hdmi->dev,
					 "snps-phy: drive I2C failed (%d), continuing MMIO-only\n",
					 ret);
				i2c_ok = false;
			}
		}
	}

	sun60i_snps_phy_config_svsret(hdmi);
	sun60i_snps_phy_assert_power(dw_hdmi, hdmi, true);

	if (i2c_ok && !sun60i_snps_wait_internal_lock(hdmi, 500))
		dev_warn(hdmi->dev,
			 "snps-phy: internal lock timeout (stat0=0x%02x)\n",
			 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_STAT0));

	if (!sun60i_snps_wait_txready(dw_hdmi, hdmi))
		dev_warn(hdmi->dev,
			 "snps-phy: top-PHY txready timeout (conf0=0x%02x stat0=0x%02x)\n",
			 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_CONF0),
			 readb(hdmi->regs + SUN60I_DW_HDMI_PHY_STAT0));

	/* Always leave ENTMDS on even if txready never asserted. */
	sun60i_snps_phy_finish_tx(dw_hdmi, hdmi);

	mdelay(10);

	sun60i_snps_phy_irq_end(hdmi);

	return 0;
}

void sun60i_hdmi_snps_phy_enable_tmds(struct sun60i_hdmi *hdmi)
{
	if (!hdmi || !hdmi->regs)
		return;

	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_ENTMDS, true);
}

void sun60i_hdmi_snps_phy_disable(struct dw_hdmi *dw_hdmi,
				  struct sun60i_hdmi *hdmi)
{
	if (!hdmi || !hdmi->regs)
		return;

	/*
	 * BSP sun60i snps_phy_disconfig(): skip dw_phy_standby() — powering
	 * down the internal PHY between modesets breaks the next top-PHY path.
	 */
	sun60i_snps_phy_set_bit(hdmi, SUN60I_DW_HDMI_PHY_CONF0,
				SUN60I_DW_HDMI_PHY_CONF0_ENTMDS, false);
}
