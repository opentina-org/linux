/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * sunxi iommu: main structures
 *
 * Copyright (c) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 */

#include <linux/version.h>
#include "sunxi-iommu-pgtable.h"

#define DETACH_OP_DEPRECATED
//iommu domain have seperate ops
#define SEPERATE_DOMAIN_API
//dma-iommu is enclosed into iommu-core
#define DMA_IOMMU_IN_IOMMU
//not used anywhere since refactoring
#define GROUP_NOTIFIER_DEPRECATED
//iommu now have correct probe order
//no more need bus set op as workaround
#define BUS_SET_OP_DEPRECATED
//dma cookie handled by iommu core, not driver
#define COOKIE_HANDLE_BY_CORE
//iommu resv region allocation require gfp flags
#define RESV_REGION_NEED_GFP_FLAG

#include <linux/iommu.h>
/*
 * by design iommu driver should be part of iommu
 * and get to it by ../../dma-iommu.h
 * sunxi bsp have seperate root, use different path
 * to reach dma-iommu.h
 */
#include <../drivers/iommu/dma-iommu.h>

#define MAX_SG_SIZE (128 << 20)
#define MAX_SG_TABLE_SIZE ((MAX_SG_SIZE / SPAGE_SIZE) * sizeof(u32))
#define DUMP_REGION_MAP 0
#define DUMP_REGION_RESERVE 1
struct dump_region {
	u32 access_mask;
	size_t size;
	u32 type;
	dma_addr_t phys, iova;
};
struct sunxi_iommu_dev;

typedef void (*sunxi_iommu_fault_cb)(void);

void sunxi_iommu_register_fault_cb(sunxi_iommu_fault_cb cb,
				   unsigned int master_id);
void sunxi_reset_device_iommu(unsigned int master_id);
void sunxi_enable_device_iommu(unsigned int master_id, bool flag);
void sunxi_iommu_enable_interrupt(int enable);
void sunxi_iommu_prevent_hang_enable(int enable);
int sunxi_iommu_get_idx_by_name(const char *name);
void sunxi_iommu_master_ready(struct device *dev);

int sunxi_iova_test_write(dma_addr_t iova, u32 val);
unsigned long sunxi_iova_test_read(dma_addr_t iova);
void sunxi_set_debug_mode(void);
void sunxi_set_prefetch_mode(void);
int sunxi_iommu_check_cmd(struct device *dev, void *data);
u32 sunxi_iommu_dump_rsv_list(struct list_head *rsv_list, ssize_t len,
			      char *buf, size_t buf_len, bool for_sysfs_show);
int iova_show_on_irq(void);
void sunxi_iommu_register_vendorhook(void);
void sunxi_iommu_init_debugfs(struct sunxi_iommu_dev *sunxi_iommu);
void sunxi_iommu_release_debugfs(void);
ssize_t sunxi_iommu_dump_pgtable(char *buf, size_t buf_len,
					       bool for_sysfs_show);
