/*
 * Copyright (c) 2007-2018 Allwinner Technology Co.,Ltd.
 *
 */

#ifndef _VE_RPMSG_H_
#define _VE_RPMSG_H_

typedef int amp_ctrl(void);

struct ve_amp_ctrl {
	amp_ctrl *rv_start;
	amp_ctrl *rv_stop;
	bool rv_irq_state;
	bool iommu_need;
	struct platform_device *pdev;
};

void amp_ve_init(void *amp_ops);
void amp_ve_exit(void);

#endif