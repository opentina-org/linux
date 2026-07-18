/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * config.h for device tree and sensor list parser.
 *
 * Copyright (c) 2017 by Allwinner Technology Co.,Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *	Yang Feng <yangfeng@allwinnertech.com>
 *
 */

#ifndef __CONFIG__H__
#define __CONFIG__H__

#include "../vin-video/vin_core.h"
#include "../vin.h"
#include "cfg_op.h"

int parse_modules_from_device_tree(struct vin_md *vind);

#endif /* __CONFIG__H__ */
