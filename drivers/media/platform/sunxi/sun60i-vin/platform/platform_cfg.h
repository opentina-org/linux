/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Copyright (c) 2007-2017 Allwinner Technology Co.,Ltd.
 *
 * Authors:  Zhao Wei <zhaowei@allwinnertech.com>
 *
 */

#ifndef __PLATFORM_CFG__H__
#define __PLATFORM_CFG__H__

/* #define FPGA_VER */
/* Use dma_alloc_coherent instead of BSP ion/dma-heap path */
#define VIN_USE_DMA_ALLOC
#if !defined(VIN_USE_DMA_ALLOC)
#define SUNXI_MEM
#endif
#define disp_sync_video 1

#if IS_ENABLED(CONFIG_BUF_AUTO_UPDATE)
#define BUF_AUTO_UPDATE
#endif
#if IS_ENABLED(CONFIG_MULTI_FRM_MERGE_INT)
#define MULTI_FRM_MERGE_INT
#endif

#if IS_ENABLED(CONFIG_SUPPORT_ISP_TDM)
#define SUPPORT_ISP_TDM
#endif
#define MIPI_COMBO_CSI

#if IS_ENABLED(CONFIG_SENSOR_POWER)  || IS_ENABLED(CONFIG_SENSOR_POWER_MODULE)
#define SENSOR_POER_BEFORE_VIN
#endif

#define MIPI_PING_CONFIG
#define VIPP_200
#define ISP_600
#define TDM_V200
#define CSIC_DMA_VER_140_000
#define PARSER_MULTI_CH_SRC_TYPE

#define CSI_VE_ONLINE_VIDEO 0

#define vin_dma_addr_t dma_addr_t

#ifndef FPGA_VER
#include <linux/clk.h>
//#include <sunxi-clk.h>
#include <linux/clk-provider.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/regulator/consumer.h>
#include <linux/mm.h>
#endif

#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/of.h>
#include "vin_gpio.h"
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/version.h>

#include "../utility/vin_os.h"
#include "../vin-mipi/combo_common.h"

#ifdef FPGA_VER
#define DPHY_CLK (48*1000*1000)
#else
#define DPHY_CLK (150*1000*1000)
#endif

enum isp_platform {
	ISP500 =  0,
	ISP520,
	ISP521,
	ISP_VERSION_NUM,
};

#include "sun60iw2_vin_cfg.h"

#define MOV_ROUND_UP(x, n)	(((x) + (1 << (n)) - 1) >> (n))

struct mbus_framefmt_res {
	u32 res_pix_fmt;
	u32 res_mipi_bps;
	u8 res_combo_mode;
	u8 res_wdr_mode;
	u8 res_time_hs;
	u8 res_deskew;
	u8 res_lp_mode;
	u8 fps;
	u8 pclk_dly;
};

enum steam_on_seq {
	MIPI_BEFORE_SENSOR  = 0,
	SENSOR_BEFORE_MIPI,
	MIPI_NEXT_SENSOR,
};

enum sdram_dfs_status {
	SENSOR_NOT_DEBUG = 0,
	SENSOR_ALREADY_DEBUG,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define V4L2_MBUS_CSI2_1_LANE			(1 << 0)
#define V4L2_MBUS_CSI2_2_LANE			(1 << 1)
#define V4L2_MBUS_CSI2_3_LANE			(1 << 2)
#define V4L2_MBUS_CSI2_4_LANE			(1 << 3)

#define V4L2_MBUS_CSI2_CHANNEL_0		(1 << 4)
#define V4L2_MBUS_CSI2_CHANNEL_1		(1 << 5)
#define V4L2_MBUS_CSI2_CHANNEL_2		(1 << 6)
#define V4L2_MBUS_CSI2_CHANNEL_3		(1 << 7)

#define V4L2_MBUS_CSI2_LANES		(V4L2_MBUS_CSI2_1_LANE | \
					 V4L2_MBUS_CSI2_2_LANE | \
					 V4L2_MBUS_CSI2_3_LANE | \
					 V4L2_MBUS_CSI2_4_LANE)
#define V4L2_MBUS_CSI2_CHANNELS		(V4L2_MBUS_CSI2_CHANNEL_0 | \
					 V4L2_MBUS_CSI2_CHANNEL_1 | \
					 V4L2_MBUS_CSI2_CHANNEL_2 | \
					 V4L2_MBUS_CSI2_CHANNEL_3)
#endif

#define CSI_CH_0	(1 << 20)
#define CSI_CH_1	(1 << 21)
#define CSI_CH_2	(1 << 22)
#define CSI_CH_3	(1 << 23)

#define MAX_DETECT_NUM	3

/*
 * The subdevices' group IDs.
 */
#define VIN_GRP_ID_SENSOR	(1 << 8)
#define VIN_GRP_ID_MIPI		(1 << 9)
#define VIN_GRP_ID_CSI		(1 << 10)
#define VIN_GRP_ID_ISP		(1 << 11)
#define VIN_GRP_ID_SCALER	(1 << 12)
#define VIN_GRP_ID_CAPTURE	(1 << 13)
#define VIN_GRP_ID_STAT		(1 << 14)
#define VIN_GRP_ID_TDM_RX	(1 << 15)

#define VIN_ALIGN_WIDTH 16
#define VIN_ALIGN_HEIGHT 16

#define VIN_FALSE 0
#define VIN_TRUE 1

#if IS_ENABLED(CONFIG_RV_RUN_CAR_REVERSE)
#define VIN_INIT_DRIVERS(fn) late_initcall(fn)
#else
#define VIN_INIT_DRIVERS(fn) module_init(fn)
#endif

#endif /* __PLATFORM_CFG__H__ */
