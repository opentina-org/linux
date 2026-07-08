/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (c) 2007-2020 Allwinner Technology Co.,Ltd.
 *
 */

/* #define DEBUG */
#define SUNXI_MODNAME "irrx"
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <media/rc-map.h>
#include <linux/version.h>
#include "sunxi-ir-rx.h"
#include "sunxi-ir-keymap.h"
#include <linux/pinctrl/consumer.h>
#include <media/rc-core.h>

static inline bool sunxi_ir_eq_margin(unsigned d1, unsigned d2, unsigned margin)
{
	return d1 > (d2 - margin) && d1 < (d2 + margin);
}

#define eq_margin sunxi_ir_eq_margin

#define SUNXI_IRRX_TIME_UNIT		0x2
#define SUNXI_IRRX_DATA_SHIFT		0x7
#define SUNXI_IRRX_DATA_MASK		0x7f
#define SUNXI_IRRX_SIGNAL_CNT_SHIFT	0x8
#define SUNXI_IRRX_SIGNAL_CNT_MASK	0x7f
#define SUNXI_IRRX_SIGNAL_INVERT_VAL	0x1
#define SUNXI_IRRX_SIGNAL_INVERT_SHIFT	0x2
#define SUNXI_IRRX_INTS_MASK		0xff
#define SUNXI_IRRX_INTS_CLEAR		0xef
#define SUNXI_IRRX_NS_TO_US_UNIT	1000
#define	SUNXI_IRRX_VENDOR		0x0001
#define	SUNXI_IRRX_PRODUCT		0x0001
#define	SUNXI_IRRX_VERSION		0x0100

static struct rc_map_list sunxi_map = {
	.map = {
		.scan    = sunxi_nec_scan,
		.size    = ARRAY_SIZE(sunxi_nec_scan),
		.rc_proto = RC_PROTO_NEC,	/* Legacy IR type */
		.name    = RC_MAP_SUNXI,
	}
};

static inline void sunxi_irrx_save_regs(struct sunxi_ir_rx *chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_irrx_regs_offset); i++)
		chip->regs_backup[i] = readl(chip->reg_base + sunxi_irrx_regs_offset[i]);
}

static inline void sunxi_irrx_restore_regs(struct sunxi_ir_rx *chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sunxi_irrx_regs_offset); i++)
		writel(chip->regs_backup[i], chip->reg_base + sunxi_irrx_regs_offset[i]);
}

static inline u32 ir_get_data(void __iomem *reg_base)
{
	return readl(reg_base + IR_RXDAT_REG);
}

/* Translate OpenFirmware node properties into platform_data */
static struct of_device_id const sunxi_ir_recv_of_match[] = {
	{ .compatible = "allwinner,irrx",},
	{ },
};
MODULE_DEVICE_TABLE(of, sunxi_ir_recv_of_match);

static void sunxi_irrx_recv(u32 reg_data, struct sunxi_ir_rx *chip)
{
	bool pulse_now;
	u32 ir_duration;
	struct device *dev = &chip->pdev->dev;

	pulse_now = reg_data >> SUNXI_IRRX_DATA_SHIFT; /* get the polarity */
	ir_duration = reg_data & SUNXI_IRRX_DATA_MASK; /* get duration, number of clocks */

	if (chip->pulse_pre == pulse_now) {
		/* the signal sunperposition */
		chip->rawir.duration += ir_duration;
		dev_dbg(dev, "raw: polar=%d; dur=%d\n", pulse_now, ir_duration);
		return;
	}
	if (!chip->is_receiving) {
		/* get the first pulse signal */
		chip->rawir.pulse = pulse_now;
		chip->rawir.duration = ir_duration;
		chip->is_receiving = 1;
		dev_dbg(dev, "get frist pulse, add head!\n");
		dev_dbg(dev, "raw: polar=%d; dur=%d\n", pulse_now, ir_duration);
		chip->pulse_pre = pulse_now;
		return;
	}

	chip->rawir.duration *= IR_SIMPLE_UNIT;

	/*
	 * on kernels after linux-5.10, the nec protocol decoding interface has changed,
	 * and the incoming time has changed from ns to us
	 */
	chip->rawir.duration = DIV_ROUND_CLOSEST((chip->rawir.duration), SUNXI_IRRX_NS_TO_US_UNIT);

	dev_dbg(dev, "pulse: polar=%d, dur: %u ns\n",
		chip->rawir.pulse, chip->rawir.duration);
	if (chip->boot_code == 0) {
		chip->boot_code = 1;
		if (eq_margin(chip->rawir.duration, NEC_BOOT_CODE, NEC_UNIT * SUNXI_IRRX_TIME_UNIT)) {
			chip->protocol = NEC;
			ir_raw_event_store(chip->rcdev, &chip->rawir);
		} else {
			chip->protocol = RC5;
			ir_raw_event_store(chip->rcdev, &chip->rawir);
		}
	} else {
		if (((chip->rawir.duration > chip->threshold_low) &&
					(chip->rawir.duration < chip->threshold_high)) &&
					(chip->protocol == RC5)) {
			chip->rawir.duration = chip->rawir.duration / SUNXI_IRRX_TIME_UNIT;
			ir_raw_event_store(chip->rcdev, &chip->rawir);
		} else {
			ir_raw_event_store(chip->rcdev, &chip->rawir);
		}
	}

	chip->rawir.pulse = pulse_now;
	chip->rawir.duration = ir_duration;
	dev_dbg(dev, "raw: polar=%d; dur=%d\n", pulse_now, ir_duration);
	chip->pulse_pre = pulse_now;
}

static irqreturn_t sunxi_irrx_irq(int irq, void *dev_id)
{
	struct sunxi_ir_rx *chip = (struct sunxi_ir_rx *)dev_id;
	struct device *dev = &chip->pdev->dev;
	u32 intsta, dcnt;
	u32 i = 0;
	u32 reg_data;

	dev_dbg(dev, "IR RX IRQ Serve\n");

	/* Clear the interrupt */
	intsta = readl(chip->reg_base + IR_RXINTS_REG);
	intsta |= intsta & SUNXI_IRRX_INTS_MASK;
	writel(intsta, chip->reg_base + IR_RXINTS_REG);

	/* get the count of signal */
	dcnt = (intsta >> SUNXI_IRRX_SIGNAL_CNT_SHIFT) & SUNXI_IRRX_SIGNAL_CNT_MASK;
	dev_dbg(dev, "receive cnt: %d\n", dcnt);
	/* Read FIFO and fill the raw event */
	for (i = 0; i < dcnt; i++) {
		/* get the data from fifo */
		reg_data = ir_get_data(chip->reg_base);
		/* Byte in FIFO format YXXXXXXX(B)
		 * Y:polarity(0:low level, 1:high level)
		 * X:Number of clocks
		 */
		sunxi_irrx_recv(reg_data, chip);
	}

	if (intsta & IR_RXINTS_RXPE) {
		/* The last pulse can not call ir_raw_event_store() since miss
		 * invert level in above, manu call
		 */
		if (chip->rawir.duration) {
			chip->rawir.duration *= IR_SIMPLE_UNIT;
			dev_dbg(dev, "pulse: polar=%d, dur: %u ns\n",
				chip->rawir.pulse, chip->rawir.duration);
			ir_raw_event_store(chip->rcdev, &chip->rawir);
		}
		dev_dbg(dev, "handle raw data.\n");
		/* handle ther decoder thread */
		ir_raw_event_handle(chip->rcdev);
		chip->is_receiving = 0;
		chip->boot_code = 0;
		chip->pulse_pre = false;

		if (chip->wakeup)
			pm_wakeup_event(chip->rcdev->input_dev->dev.parent, 0);
	}

	if (intsta & IR_RXINTS_RXOF) {
		/* FIFO Overflow */
		dev_err(dev, "ir_rx_irq_service: Rx FIFO Overflow!!\n");
		chip->is_receiving = 0;
		chip->boot_code = 0;
		chip->pulse_pre = false;
	}

	return IRQ_HANDLED;
}

static void sunxi_ir_mode_set(void __iomem *reg_base, enum ir_mode set_mode)
{
	u32 ctrl_reg = 0;

	switch (set_mode) {
	case CIR_MODE_ENABLE:
		ctrl_reg = readl(reg_base + IR_CTRL_REG);
		ctrl_reg |= IR_CIR_MODE;
		break;
	case IR_MODULE_ENABLE:
		ctrl_reg = readl(reg_base + IR_CTRL_REG);
		ctrl_reg |= IR_ENTIRE_ENABLE;
		break;
	case IR_BOTH_PULSE_MODE:
		ctrl_reg = readl(reg_base + IR_CTRL_REG);
		ctrl_reg |= IR_BOTH_PULSE;
		break;
	case IR_LOW_PULSE_MODE:
		ctrl_reg = readl(reg_base + IR_CTRL_REG);
		ctrl_reg |= IR_LOW_PULSE;
		break;
	case IR_HIGH_PULSE_MODE:
		ctrl_reg = readl(reg_base + IR_CTRL_REG);
		ctrl_reg |= IR_HIGH_PULSE;
		break;
	default:
		dev_err(NULL, "sunxi_ir_mode_set error!!\n");
		return;
	}
	writel(ctrl_reg, reg_base + IR_CTRL_REG);
}

static void ir_sample_config(void __iomem *reg_base,
					enum ir_sample_config set_sample)
{
	u32 sample_reg = 0;

	sample_reg = readl(reg_base + IR_SPLCFG_REG);

	switch (set_sample) {
	case IR_SAMPLE_REG_CLEAR:
		sample_reg = 0;
		break;
	case IR_CLK_SAMPLE:
		sample_reg |= IR_SAMPLE_DEV;
		break;
	case IR_FILTER_TH_NEC:
		sample_reg |= IR_RXFILT_VAL;
		break;
	case IR_FILTER_TH_RC5:
		sample_reg |= IR_RXFILT_VAL_RC5;
		break;
	case IR_IDLE_TH:
		sample_reg |= IR_RXIDLE_VAL;
		break;
	case IR_ACTIVE_TH:
		sample_reg |= IR_ACTIVE_T;
		sample_reg |= IR_ACTIVE_T_C;
		break;
	case IR_ACTIVE_TH_SAMPLE:
		sample_reg |= IR_ACTIVE_T_SAMPLE;
		sample_reg &= ~IR_ACTIVE_T_C;
		break;
	default:
		return;
	}
	writel(sample_reg, reg_base + IR_SPLCFG_REG);
}

static void ir_signal_invert(void __iomem *reg_base)
{
	u32 reg_val;

	reg_val = SUNXI_IRRX_SIGNAL_INVERT_VAL << SUNXI_IRRX_SIGNAL_INVERT_SHIFT;
	writel(reg_val, reg_base + IR_RXCFG_REG);
}

static void ir_irq_config(void __iomem *reg_base, enum ir_irq_config set_irq)
{
	u32 irq_reg = 0;

	switch (set_irq) {
	case IR_IRQ_STATUS_CLEAR:
		writel(SUNXI_IRRX_INTS_CLEAR, reg_base + IR_RXINTS_REG);
		return;
	case IR_IRQ_ENABLE:
		irq_reg = readl(reg_base + IR_RXINTE_REG);
		irq_reg |= IR_IRQ_STATUS;
		break;
	case IR_IRQ_FIFO_SIZE:
		irq_reg = readl(reg_base + IR_RXINTE_REG);
		irq_reg |= IR_FIFO_20;
		break;
	default:
		return;
	}
	writel(irq_reg, reg_base + IR_RXINTE_REG);
}

static void sunxi_irrx_reg_cfg(void __iomem *reg_base)
{
	/* Enable IR Mode */
	sunxi_ir_mode_set(reg_base, CIR_MODE_ENABLE);
	/* Config IR Smaple Register */
	ir_sample_config(reg_base, IR_SAMPLE_REG_CLEAR);
	ir_sample_config(reg_base, IR_CLK_SAMPLE);
	ir_sample_config(reg_base, IR_IDLE_TH); /* Set Idle Threshold */

	/* rc5 Set Active Threshold */
	ir_sample_config(reg_base, IR_ACTIVE_TH_SAMPLE);
	ir_sample_config(reg_base, IR_FILTER_TH_NEC); /* Set Filter Threshold */
	ir_signal_invert(reg_base);
	/* Clear All Rx Interrupt Status */
	ir_irq_config(reg_base, IR_IRQ_STATUS_CLEAR);
	/* Set Rx Interrupt Enable */
	ir_irq_config(reg_base, IR_IRQ_ENABLE);

	/* Rx FIFO Threshold = FIFOsz/2; */
	ir_irq_config(reg_base, IR_IRQ_FIFO_SIZE);
	/* for NEC decode which start with high level in the header so should
	 * use IR_HIGH_PULSE_MODE mode, but some ICs don't support this function
	 * therefor use IR_BOTH_PULSE_MODE mode as default
	 */
	sunxi_ir_mode_set(reg_base, IR_BOTH_PULSE_MODE);
	/* Enable IR Module */
	sunxi_ir_mode_set(reg_base, IR_MODULE_ENABLE);
}

static int sunxi_irrx_clk_cfg(struct sunxi_ir_rx *chip)
{

	unsigned long rate;
	int ret;
	struct device *dev = &chip->pdev->dev;

	ret = reset_control_reset(chip->reset);
	if (ret) {
		dev_err(dev, "ir rx reset failed!\n");
		return ret;
	}

	rate = clk_get_rate(chip->bclk);
	dev_dbg(dev, "%s: get ir bus clk rate %dHZ\n", __func__, (__u32)rate);

	rate = clk_get_rate(chip->pclk);
	dev_dbg(dev, "%s: get ir parent clk rate %dHZ\n", __func__, (__u32)rate);

	ret = clk_set_parent(chip->mclk, chip->pclk);
	if (ret) {
		dev_err(dev, "%s: set ir_clk parent failed!\n", __func__);
		return ret;
	}

	ret = clk_set_rate(chip->mclk, IR_CLK);
	if (ret) {
		dev_err(dev, "set ir clock freq to %d failed!\n", IR_CLK);
		return ret;
	}

	rate = clk_get_rate(chip->mclk);
	dev_dbg(dev, "%s: get ir_clk rate %dHZ\n", __func__, (__u32)rate);

	ret = clk_prepare_enable(chip->bclk);
	if (ret) {
		dev_err(dev, "try to enable bus clk failed!\n");
		goto assert_reset;
	}

	ret = clk_prepare_enable(chip->mclk);
	if (ret) {
		dev_err(dev, "try to enable ir_clk failed!\n");
		goto clk_disable;
	}

	return 0;

assert_reset:
	reset_control_assert(chip->reset);

clk_disable:
	clk_disable_unprepare(chip->bclk);

	return ret;
}

static void sunxi_irrx_clk_uncfg(struct sunxi_ir_rx *chip)
{
	clk_disable_unprepare(chip->mclk);
	clk_disable_unprepare(chip->bclk);

	reset_control_assert(chip->reset);
}

static int sunxi_irrx_select_pinctrl_state(struct pinctrl *pctrl, char *name, struct sunxi_ir_rx *chip)
{
	int ret = 0;
	struct pinctrl_state *pctrl_state = NULL;
	struct device *dev = &chip->pdev->dev;

	if (!pctrl)
		return 0;

	pctrl_state = pinctrl_lookup_state(pctrl, name);
	if (IS_ERR(pctrl_state)) {
		dev_err(dev, "IR pinctrl_lookup_state(%s) failed! return %p \n",
				name, pctrl_state);
		return -1;
	}

	ret = pinctrl_select_state(pctrl, pctrl_state);
	if (ret) {
		dev_err(dev, "IR pinctrl_select_state(%s) failed! return %d \n",
				name, ret);
		return ret;
	}

	return 0;
}

static int sunxi_irrx_hw_init(struct sunxi_ir_rx *chip)
{
	int ret = 0;
	struct device *dev = &chip->pdev->dev;

	if (chip->supply) {
		ret = regulator_set_voltage(chip->supply, chip->supply_vol,
				chip->supply_vol);
		if (ret)
			dev_err(dev, "ir rx set regulator voltage failed!\n");
	}

	if (chip->supply) {
		ret = regulator_enable(chip->supply);
		if (ret)
			dev_err(dev, "ir rx regulator enable failed!\n");
	}

	ret = sunxi_irrx_select_pinctrl_state(chip->pctrl, PINCTRL_STATE_DEFAULT, chip);

	ret = sunxi_irrx_clk_cfg(chip);
	if (ret) {
		dev_err(dev, "ir rx clk configure failed!\n");
		return ret;
	}

	sunxi_irrx_reg_cfg(chip->reg_base);

	return 0;
}

static void sunxi_irrx_hw_exit(struct sunxi_ir_rx *chip)
{
	sunxi_irrx_clk_uncfg(chip);
	sunxi_irrx_select_pinctrl_state(chip->pctrl, PINCTRL_STATE_SLEEP, chip);
	if (chip->supply) {
		regulator_disable(chip->supply);
	}
}

static int sunxi_irrx_resource_get(struct platform_device *pdev,
				struct sunxi_ir_rx *chip)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "fail to get IORESOURCE_MEM\n");
		return -EINVAL;
	}

	chip->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->reg_base)) {
		dev_err(dev, "%s:Failed to ioremap() io memory region.\n", __func__);
		return PTR_ERR(chip->reg_base);
	}
	dev_dbg(dev, "ir base: %p !\n", chip->reg_base);

	chip->rtc_flag = of_property_read_u32(np, "rtc_reg", &chip->rtc_reg);
	if (!chip->rtc_flag) {
		chip->rtc_addr = ioremap(chip->rtc_reg, 4);
		if (!chip->rtc_addr) {
			dev_err(dev, "ioremap fail\n");
			return -EBUSY;
		}
	}

	chip->irq_num = platform_get_irq(pdev, 0);
	if (chip->irq_num < 0)
		return -EINVAL;
	dev_dbg(dev, "ir irq num: %d !\n", chip->irq_num);

	chip->pctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(chip->pctrl)) {
		dev_dbg(dev, "IR pinctrl not available\n");
		chip->pctrl = NULL;
	}

	chip->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(chip->reset)) {
		dev_err(dev, "Failed to get reset handle!\n");
		return PTR_ERR(chip->reset);
	}

	chip->bclk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(chip->bclk)) {
		dev_err(dev, "%s:Failed to get bus clk.\n", __func__);
		return PTR_ERR(chip->bclk);
	}

	chip->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(chip->pclk)) {
		dev_err(dev, "%s:Failed to get parent clk.\n", __func__);
		return PTR_ERR(chip->pclk);
	}

	chip->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(chip->mclk)) {
		dev_err(dev, "%s:Failed to get ir rx clk.\n", __func__);
		return PTR_ERR(chip->mclk);
	}

	if (of_property_read_u32(np, "ir_protocol_used", &chip->ir_protocols))
		chip->ir_protocols = NEC;

	chip->wakeup = of_property_read_bool(np, "wakeup-source");
	if (chip->wakeup)
		device_init_wakeup(&pdev->dev, chip->wakeup);

	return 0;
}

static int sunxi_irrx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rc_dev *rcdev;
	int ret;
	struct sunxi_ir_rx *chip;

	dev_dbg(dev, "sunxi-ir probe start !\n");
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(dev, "chip: not enough memory for ir data\n");
		return -ENOMEM;
	}

	chip->threshold_low = RC5_UNIT + RC5_UNIT / 2;
	chip->threshold_high = 2 * RC5_UNIT + RC5_UNIT / 2;

	pdev->id = of_alias_get_id(pdev->dev.of_node, "ir");
	if (pdev->id < 0)
		pdev->id = 0;

	/* initialize hardware resource */
	ret = sunxi_irrx_resource_get(pdev, chip);
	if (ret) {
		dev_err(dev, "initialize hardware resource failed!\n");
		goto err0;
	}
	dev_dbg(dev, "sunxi-ir initialize hardware res success!\n");

	rcdev = devm_rc_allocate_device(dev, RC_DRIVER_IR_RAW);
	if (!rcdev) {
		dev_err(dev, "rc dev allocate fail !\n");
		ret = -ENOMEM;
		goto err0;
	}
	dev_dbg(dev, "sunxi-ir allocate rc device success!\n");

	/* initialize rcdev */
	rcdev->priv = chip;
	rcdev->device_name = SUNXI_IR_DEVICE_NAME;
	rcdev->input_phys = SUNXI_IR_DEVICE_NAME "/input0";
	rcdev->input_id.bustype = BUS_HOST;
	rcdev->input_id.vendor = SUNXI_IRRX_VENDOR;
	rcdev->input_id.product = SUNXI_IRRX_PRODUCT;
	rcdev->input_id.version = SUNXI_IRRX_VERSION;

	rcdev->dev.parent = &pdev->dev;
	rcdev->driver_type = RC_DRIVER_IR_RAW;
	rcdev->driver_name = SUNXI_IR_DRIVER_NAME;
	/*
	 * timeout as an adjustable time in driver/media/rc/rc-main.c, is used to control the time for reporting up events.
	 * timoout = 0 indicates that the time for reporting up only depends on the specific protocol.
	 */
	rcdev->timeout = 0;

	if (chip->ir_protocols == NEC)
		rcdev->allowed_protocols = (u64)RC_PROTO_BIT_NEC;
	if (chip->ir_protocols == RC5)
		rcdev->allowed_protocols = (u64)RC_PROTO_BIT_RC5;
	if (chip->ir_protocols == RC5ANDNEC)
		rcdev->allowed_protocols = (u64)RC_PROTO_BIT_RC5 |
			(u64)RC_PROTO_BIT_NEC;
	rcdev->enabled_protocols = rcdev->allowed_protocols;
	rcdev->map_name = of_get_property(pdev->dev.of_node, "linux,rc-map-name", NULL);
	if (!rcdev->map_name)
		rcdev->map_name = RC_MAP_SUNXI;

	rc_map_register(&sunxi_map);

	ret = devm_rc_register_device(dev, rcdev);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		goto err1;
	}
	dev_dbg(dev, "sunxi-ir register rc device success!\n");

	chip->rcdev = rcdev;
	chip->pdev = pdev;

	ret = sunxi_irrx_hw_init(chip);
	if (ret) {
		dev_err(dev, "%s: sunxi_irrx_hw_init failed.\n", __func__);
		goto err1;
	}
	dev_dbg(dev, "sunxi-ir hardware setup success!\n");

	platform_set_drvdata(pdev, chip);
	ret = devm_request_irq(dev, chip->irq_num, sunxi_irrx_irq, 0,
				"RemoteIR_RX", chip);
	if (ret) {
		dev_err(dev, "%s: request irq fail.\n", __func__);
		ret = -EBUSY;
		goto err2;
	}

	dev_info(dev, "Allwinner IR receiver initialized (protocol=%u)\n",
		 chip->ir_protocols);

	return 0;

err2:
	sunxi_irrx_hw_exit(chip);
err1:
	rc_map_unregister(&sunxi_map);
err0:
	return ret;
}

static void sunxi_irrx_remove(struct platform_device *pdev)
{
	struct sunxi_ir_rx *chip = platform_get_drvdata(pdev);

	sunxi_irrx_hw_exit(chip);

	rc_map_unregister(&sunxi_map);
}

static int sunxi_irrx_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_ir_rx *chip = platform_get_drvdata(pdev);

	dev_dbg(dev, "enter: sunxi_ir_rx_suspend.\n");

	if (device_may_wakeup(dev)) {
		if (chip->wakeup)
			enable_irq_wake(chip->irq_num);
		dev_dbg(dev, "enter: sunxi_ir_rx_suspend enable irq wakeup.\n");
	} else {

		disable_irq_nosync(chip->irq_num);

		sunxi_irrx_save_regs(chip);

		sunxi_irrx_hw_exit(chip);
	}

	return 0;
}

static int sunxi_irrx_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_ir_rx *chip = platform_get_drvdata(pdev);
	u32 rtc_val = 0, scancode;
	u8 not_address, command, not_command;

	/* Use RTC reg to store atf ir decode value and input this code to android */
	if (!chip->rtc_flag) {
		rtc_val = readl(chip->rtc_addr);
		writel(0, chip->rtc_addr);
	}
	dev_dbg(dev, "enter: sunxi_ir_rx_resume.\n");

	if (device_may_wakeup(dev)) {
		if (chip->wakeup)
			disable_irq_wake(chip->irq_num);
		if (rtc_val) {
			not_address = (u8)((rtc_val >> 16) & 0xff);
			command     = (u8)((rtc_val >>  8) & 0xff);
			not_command = (u8)((rtc_val >>  0) & 0xff);
			scancode = not_command << 8 | command << 16 | not_address;
			rc_keydown(chip->rcdev, RC_PROTO_NEC, scancode, 0);
		}
		dev_dbg(dev, "enter: sunxi_ir_rx_suspend disable irq wakeup.\n");
	} else {

		sunxi_irrx_hw_init(chip);

		sunxi_irrx_restore_regs(chip);

		enable_irq(chip->irq_num);
	}

	return 0;
}

static const struct dev_pm_ops sunxi_ir_recv_pm_ops = {
	.suspend        = sunxi_irrx_suspend,
	.resume         = sunxi_irrx_resume,
};

static struct platform_driver sunxi_ir_recv_driver = {
	.probe  = sunxi_irrx_probe,
	.remove = sunxi_irrx_remove,
	.driver = {
		.name   = SUNXI_IR_DRIVER_NAME,
		
		.of_match_table = of_match_ptr(sunxi_ir_recv_of_match),
		.pm	= &sunxi_ir_recv_pm_ops,
	},
};
module_platform_driver(sunxi_ir_recv_driver);
MODULE_DESCRIPTION("SUNXI IR Receiver driver");
MODULE_AUTHOR("QIn");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.9");
