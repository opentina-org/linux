/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2007 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */

/*
 ******************************************************************************
 *
 * sunxi_cci.h
 *
 * Hawkview ISP - sunxi_cci.h module
 *
 * Version         Author         Date         Description
 *
 *   2.0         Yang Feng     2014/06/23     Second Version
 *
 ******************************************************************************
 */

#ifndef _SUNXI_CCI_H_
#define _SUNXI_CCI_H_

#include "../platform/platform_cfg.h"
#include <linux/i2c.h>

struct cci_dev {
	struct platform_device *pdev;
	struct i2c_adapter adap;
	unsigned int id;
	spinlock_t slock;
	int irq;
	int use_cnt;
	wait_queue_head_t wait;

	void __iomem *base;

	struct list_head cci_list;
	struct pinctrl *pctrl;
	struct clk *clock;
};

int sunxi_cci_platform_register(void);
void sunxi_cci_platform_unregister(void);

#endif /* _SUNXI_CCI_H_ */
