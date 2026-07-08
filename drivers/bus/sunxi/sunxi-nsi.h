/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * SUNXI MBUS support
 *
 * Copyright (c) 2015 Allwinner Technology Co.,Ltd.
 *
 */

#ifndef __LINUX_SUNXI_MBUS_H
#define __LINUX_SUNXI_MBUS_H

#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>

enum NSI_TOPOLOGY_TYPE_E {
	NSI_TOPO_V0, /* legacy, all master to one RA and one TA and one DDR, e.g sun50iw10 */
	NSI_TOPO_V1, /* CPU isolated, CPU bypass RA and TA, e.g sun55iw3 */
	NSI_TOPO_V2,
};

enum NSI_CLK_PATH_E {
	NSI_CLK_PATH_V0, /* legacy, nsi clock is mbus clock */
	NSI_CLK_PATH_V1, /* nsi module clock depart from mbus clock */
};

enum NSI_CHANNEL_TYPE_E {
	NSI_DEFAULT_CHANNEL = 0,
	NSI_SINGLE_CHANNLE,
	NSI_DUAL_CHANNEL,
};

struct nsi_pmu_data {
	struct device *dev_nsi;
	unsigned long period;
	spinlock_t bwlock;
};

enum NSI_MASTER_TYPE_E {
	NSI_IA_MASTER,
	NSI_CPU_MASTER,
};
struct nsi_master_dev {
	const char *name;
	struct device *dev;
	uint32_t id;
	enum NSI_MASTER_TYPE_E type;
	union {
		struct {
			void *base;
			struct clk *pmu_clk;
		} cpu_direct;
		struct {
			u32 ia_index;
		} ia_master;
	};
};

struct nsi_bus {
	struct cdev cdev;
	struct device *dev;
	void __iomem *base;
	void __iomem *cpu_base;
	struct clk *pclk;  /* PLL clock */
	struct clk *mclk;  /* mbus clock */
	struct reset_control *reset;
	unsigned long rate;

	/*
	 * master dedicated clock
	 * master id -> clk_idx idx -> clks idx -> clk for this master
	 * clk = clks[ clk_idx[ master_id ] ]
	 */
	struct clk **clks;
	u32 *clk_idx;

	/*
	 * how "mater - IA - RA - RDM - RA - TA" connect
	 * different topology have different ways to calculate
	 * bandwidth from regs
	 */
	enum NSI_TOPOLOGY_TYPE_E topo_tpye;

	/*
	 * different clk path have different clks/resets to deal with
	 * use a point array to hold them
	 */
	enum NSI_CLK_PATH_E clk_path_type;
	enum NSI_CHANNEL_TYPE_E channel_type;
	struct reset_control *reset_cfg;
	u32 ia_pmu_data_unit;
	u32 ra_pmu_data_unit;
	u32 ta_pmu_data_unit;
	u32 cpu_pmu_data_unit;
	int irq;
	u32 skip_mask;
	struct nsi_master_dev *master;
	u32	sub_node_id_mapping;
	u32 master_cnt;
};

/* MBUS PMU ids (SUN60IW2 / A733) */
enum nsi_pmu {
	MBUS_PMU_GMAC		= 0,
	MBUS_PMU_MSI_LITE0	= 1,
	MBUS_PMU_DE		= 2,
	MBUS_PMU_EINK		= 3,
	MBUS_PMU_DI		= 4,
	MBUS_PMU_G2D		= 5,
	MBUS_PMU_GPU		= 6,
	MBUS_PMU_VE0		= 7,
	MBUS_PMU_VE1		= 8,
	MBUS_PMU_VE2		= 9,
	MBUS_PMU_GIC		= 10,
	MBUS_PMU_MSI_LITE1	= 11,
	MBUS_PMU_MSI_LITE2	= 12,
	MBUS_PMU_USB_PCIE	= 13,
	MBUS_PMU_IOMMU0		= 14,
	MBUS_PMU_IOMMU1		= 15,
	MBUS_PMU_ISP		= 16,
	MBUS_PMU_CSI		= 17,
	MBUS_PMU_NPU		= 18,
	MBUS_PMU_CPU		= 19,
	MBUS_PMU_CPU1		= 20,
	MBUS_PMU_IAG_MAX,
	MBUS_PMU_TAG		= 32,
	MBUS_PMU_MAX,
};

static const char *const pmu_name[] = {
	"gmac", "msi_list0", "de", "eink", "di", "g2d", "gpu", "ve0", "ve1", "ve2", "gic",
	"msi_lite1", "msi_lite2", "usb_pcie", "iommu0", "iommu1", "isp", "csi", "npu", "cpu0", "cpu1",
	"total"
};

#define get_name(n)      pmu_name[n]

extern int sunxi_nsi_ecc_init(struct platform_device *pdev);
extern void sunxi_nsi_ecc_exit(struct platform_device *pdev);
int sunxi_sid_get_ecc_status(void);

#if IS_ENABLED(CONFIG_SUNXI_NSI)
extern int nsi_port_setpri(enum nsi_pmu port, unsigned int pri);
extern int nsi_port_sethpr(enum nsi_pmu port, unsigned int qos);
extern bool nsi_probed(void);
extern int notrace nsi_port_setmode(enum nsi_pmu port, unsigned int mode);
extern int notrace nsi_port_setio(enum nsi_pmu port, bool io);
extern int notrace nsi_port_set_abs_bwl(enum nsi_pmu port, unsigned int bwl);
extern int notrace nsi_port_set_abs_bwlen(enum nsi_pmu port, bool en);
extern int notrace nsi_set_cpu_rw_bw_en(unsigned int dir_mask, unsigned enabled);
extern int notrace nsi_set_cpu_rw_bwl(unsigned int cpu_port, unsigned int bwl);
extern int notrace nsi_unset_cpu_rw_bwl(unsigned int cpu_port);
#endif

extern void sunxi_nsi_distribute_mater_get(int mask);
extern void sunxi_nsi_distribute_mater_put(int mask);
extern ssize_t nsi_pmu_latency_wr_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_latency_rd_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_bandwidth_wr_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_bandwidth_rd_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_bandwidth_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_available_pmu_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_cmd_rd_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t nsi_pmu_cmd_wr_show(struct device *dev,
			struct device_attribute *da, char *buf);
extern ssize_t __nsi_pmu_timer_store_v2(unsigned long period);
extern ssize_t nsi_pmu_timer_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);
extern struct nsi_bus sunxi_nsi;
extern struct nsi_pmu_data hw_nsi_pmu;

#define NSI_MAJOR         137
#define NSI_MINORS        256

#define MBUS_PRI_MAX      0x3
#define MBUS_QOS_MAX      0x2

#define for_each_ports(port) for (port = 0; port < MBUS_PMU_IAG_MAX; port++)

/* n = 0~32 */
#define IAG_MODE(n)		   (0x0010 + (0x200 * (n)))
#define IAG_PRI_CFG(n)		   (0x0014 + (0x200 * (n)))
#define IAG_INPUT_OUTPUT_CFG(n)	   (0x0018 + (0x200 * (n)))
#define IAG_BAND_WIDTH(n)	   (0x0028 + (0x200 * (n)))
#define IAG_BAND_WIDTH_LIMIT_MAX_BITS (12)
#define IAG_BAND_WIDTH_LIMIT_MAX_VALUE ((1 << IAG_BAND_WIDTH_LIMIT_MAX_BITS) - 1)
#define IAG_SATURATION(n)	   (0x002c + (0x200 * (n)))
#define IAG_SATURATION_LIMIT_MAX_BITS (10)
#define IAG_SATURATION_LIMIT_MAX_VALUE ((1 << IAG_SATURATION_LIMIT_MAX_BITS) - 1)

#define IAG_QOS_CFG(n)		   (0x000C + (0x200 * (n)))
#define IAG_QOS_SHIFT(port)		16
#define IAG_QOS_SET(qos, port)	   ((qos & 0x1) << IAG_QOS_SHIFT(port))
#define IAG_QOS_GET(val, port)   ((val >> IAG_QOS_SHIFT(port)) & 0x1)

/* Counter n = 0 ~ 19 */
#define MBUS_PMU_ENABLE(n)         (0x00c0 + (0x200 * (n)))
#define MBUS_PMU_CLR(n)            (0x00c4 + (0x200 * (n)))
#define MBUS_PMU_CYCLE(n)          (0x00c8 + (0x200 * (n)))
#define MBUS_PMU_RQ_RD(n)          (0x00cc + (0x200 * (n)))
#define MBUS_PMU_RQ_WR(n)          (0x00d0 + (0x200 * (n)))
#define MBUS_PMU_DT_RD(n)          (0x00d4 + (0x200 * (n)))
#define MBUS_PMU_DT_WR(n)          (0x00d8 + (0x200 * (n)))
#define MBUS_PMU_LA_RD(n)          (0x00dc + (0x200 * (n)))
#define MBUS_PMU_LA_WR(n)          (0x00e0 + (0x200 * (n)))

#define MBUS_PORT_MODE          (MBUS_PMU_MAX + 0)
#define MBUS_PORT_QOS_PRI           (MBUS_PMU_MAX + 1)
#define MBUS_INPUT_OUTPUT       (MBUS_PMU_MAX + 2)
#define MBUS_PORT_HPR           (MBUS_PMU_MAX + 3)
#define MBUS_PORT_ABS_BWL	(MBUS_PMU_MAX + 4)
#define MBUS_PORT_ABS_BWLEN	(MBUS_PMU_MAX + 5)

#define CPU_PMU_EN		0x0020
#define CPU_PMU_CLR		0x0024
#define CPU_PMU_PER		0x0028
#define CPU_CHL0_PMU_REQ_R	0x0080
#define CPU_CHL0_PMU_REQ_W	0x0084
#define CPU_CHL0_PMU_DATA_R	0x0088
#define CPU_CHL0_PMU_DATA_W	0x008c
#define CPU_CHL0_PMU_LAT_R	0x0090
#define CPU_CHL0_PMU_LAT_W	0x0094
#define CPU_IAG_MODE		0x0030
#define CPU_PRI_CFG		   0x0034
#define CPU_INPUT_OUTPUT_CFG	   0x0038
#define CPU_BAND_WIDTH_LIMIT(n)	   (0x0048 + ((n) * 8))
#define CPU_BAND_WIDTH_LIMIT_MAX_BITS (12)
#define CPU_BAND_WIDTH_LIMIT_MAX_VALUE ((1 << CPU_BAND_WIDTH_LIMIT_MAX_BITS) - 1)
#define CPU_SATURATION_LIMIT(n)    (0x004c + ((n) * 8))
#define CPU_SATURATION_LIMIT_MAX_BITS (10)
#define CPU_SATURATION_LIMIT_MAX_VALUE ((1 << CPU_SATURATION_LIMIT_MAX_BITS) - 1)
#define CPU_QOS_CFG		   0x002c
#define CPU_BW_LIMIT_EN_BIT        16

#define CPU_ABS_RW		(MBUS_PMU_MAX + 6)

#define nsi_disable_port_by_index(dev) \
	nsi_port_control_by_index(dev, false)
#define nsi_enable_port_by_index(dev) \
	nsi_port_control_by_index(dev, true)

#endif
