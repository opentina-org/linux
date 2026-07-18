/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * configs for sensor list.
 *
 * Copyright (c) 2020 by Allwinner Technology Co.,Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Li Huiyu <lihuiyu@allwinnertech.com>
 *
 */

#ifndef __SENSOR_LIST_H__
#define __SENSOR_LIST_H__

#include "../../platform/platform_cfg.h"
#include "../../vin-video/vin_core.h"
#include "../../vin.h"

struct fetch_sl{
	char *sub;
	int flag;
	void (*fun)(struct sensor_list *, const char *, struct device_node *, int);
};

int sensor_list_get_parms(struct sensor_list *sensors, struct device_node *node, int sel);

#endif
