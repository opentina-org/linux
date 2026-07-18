/* SPDX-License-Identifier: GPL-2.0-only */
/* VIN-local BSP GPIO helpers (replaces global sunxi-gpio.h). */

#ifndef _VIN_GPIO_H_
#define _VIN_GPIO_H_

#include <linux/types.h>

#ifndef GPIO_INDEX_INVALID
#define GPIO_INDEX_INVALID	(0xffffffffU)
#endif

/*
 * Allwinner sys_config-style GPIO descriptor used by VIN sensor/power code.
 */
struct gpio_config {
	u32	gpio;
	u32	mul_sel;
	u32	pull;
	u32	drv_level;
	u32	data;
};

#endif /* _VIN_GPIO_H_ */
