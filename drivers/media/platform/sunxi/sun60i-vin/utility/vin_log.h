/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright (c) 2007-2017 Allwinner Technology Co.,Ltd.
 *
 */

#ifndef __VIN__LOG__H__
#define __VIN__LOG__H__

#include <linux/printk.h>

#if IS_ENABLED(CONFIG_CCI_MODULE) || IS_ENABLED(CONFIG_CCI)
#include "../vin-cci/bsp_cci.h"
#define USE_SPECIFIC_CCI
#endif

#define VIN_LOG_MD				(1 << 0)	/* 0x1 */
#define VIN_LOG_FLASH				(1 << 1)	/* 0x2 */
#define VIN_LOG_CCI				(1 << 2)	/* 0x4 */
#define VIN_LOG_CSI				(1 << 3)	/* 0x8 */
#define VIN_LOG_MIPI				(1 << 4)	/* 0x10 */
#define VIN_LOG_ISP				(1 << 5)	/* 0x20 */
#define VIN_LOG_STAT				(1 << 6)	/* 0x40 */
#define VIN_LOG_SCALER				(1 << 7)	/* 0x80 */
#define VIN_LOG_POWER				(1 << 8)	/* 0x100 */
#define VIN_LOG_CONFIG				(1 << 9)	/* 0x200 */
#define VIN_LOG_VIDEO				(1 << 10)	/* 0x400 */
#define VIN_LOG_FMT				(1 << 11)	/* 0x800 */
#define VIN_LOG_TDM				(1 << 12)	/* 0x1000 */
#define VIN_LOG_RP				(1 << 13)	/* 0x2000 */
#define VIN_LOG_LARGE				(1 << 14)	/*0x4000*/

extern unsigned int vin_log_mask;

#if IS_ENABLED(CONFIG_VIN_LOG)
#define vin_log(flag, arg...) do { \
	if (flag & vin_log_mask) { \
		switch (flag) { \
		case VIN_LOG_MD: \
			pr_debug("[VIN_LOG_MD]" arg); \
			break; \
		case VIN_LOG_FLASH: \
			pr_debug("[VIN_LOG_FLASH]" arg); \
			break; \
		case VIN_LOG_CCI: \
			pr_debug("[VIN_LOG_CCI]" arg); \
			break; \
		case VIN_LOG_CSI: \
			pr_debug("[VIN_LOG_CSI]" arg); \
			break; \
		case VIN_LOG_MIPI: \
			pr_debug("[VIN_LOG_MIPI]" arg); \
			break; \
		case VIN_LOG_ISP: \
			pr_debug("[VIN_LOG_ISP]" arg); \
			break; \
		case VIN_LOG_STAT: \
			pr_debug("[VIN_LOG_STAT]" arg); \
			break; \
		case VIN_LOG_SCALER: \
			pr_debug("[VIN_LOG_SCALER]" arg); \
			break; \
		case VIN_LOG_POWER: \
			pr_debug("[VIN_LOG_POWER]" arg); \
			break; \
		case VIN_LOG_CONFIG: \
			pr_debug("[VIN_LOG_CONFIG]" arg); \
			break; \
		case VIN_LOG_VIDEO: \
			pr_debug("[VIN_LOG_VIDEO]" arg); \
			break; \
		case VIN_LOG_FMT: \
			pr_debug("[VIN_LOG_FMT]" arg); \
			break; \
		case VIN_LOG_TDM: \
			pr_debug("[VIN_LOG_TDM]" arg); \
			break; \
		case VIN_LOG_RP: \
			pr_debug("[VIN_LOG_RP]" arg); \
			break; \
		case VIN_LOG_LARGE: \
			pr_debug("[VIN_LOG_LARGE]" arg); \
			break; \
		default: \
			pr_debug("[VIN_LOG]" arg); \
			break; \
		} \
	} \
} while (0)
#else
#define vin_log(flag, arg...) do { } while (0)
#endif

#define vin_err(x, arg...)	pr_err("[vin] " x, ##arg)
#define vin_warn(x, arg...)	pr_warn("[vin] " x, ##arg)
#define vin_print(x, arg...)	pr_info("[vin] " x, ##arg)

#define DEV_DBG_EN   0
#if (DEV_DBG_EN == 1)
#define sensor_dbg(x, arg...)	pr_debug("[%s]" x, SENSOR_NAME, ##arg)
#else
#define sensor_dbg(x, arg...)
#endif
#define sensor_err(x, arg...)	pr_err("[%s] " x, SENSOR_NAME, ##arg)
#define sensor_print(x, arg...)	pr_info("[%s]" x, SENSOR_NAME, ##arg)

#define ACT_DEV_DBG_EN 0
#define act_err(x, arg...)	pr_err("[%s] " x, SUNXI_ACT_NAME, ##arg)
#if ACT_DEV_DBG_EN
#define act_dbg(x, arg...)	pr_debug("[%s]" x, SUNXI_ACT_NAME, ##arg)
#else
#define act_dbg(x, arg...)
#endif

#define SENSOR_POWER_DBG_EN   0
#if (SENSOR_POWER_DBG_EN == 1)
#define sensor_power_dbg(x, arg...)	pr_debug("[sensor_power_debug]" x, ##arg)
#else
#define sensor_power_dbg(x, arg...)
#endif
#define sensor_power_err(x, arg...)	pr_err("[sensor_power_err]" x, ##arg)
#define sensor_power_warn(x, arg...)	pr_warn("[sensor_power_warn] " x, ##arg)
#define sensor_power_print(x, arg...)	pr_info("[sensor_power]" x, ##arg)

#ifdef USE_SPECIFIC_CCI
#define cci_print(x, arg...)	pr_info("[VIN_DEV_CCI]" x, ##arg)
#define cci_err(x, arg...)	pr_err("[VIN_DEV_CCI_ERR]" x, ##arg)
#else
#define cci_print(x, arg...)	pr_info("[VIN_DEV_I2C]" x, ##arg)
#define cci_err(x, arg...)	pr_err("[VIN_DEV_I2C_ERR]" x, ##arg)
#endif

#endif	/* __VIN__LOG__H__ */
