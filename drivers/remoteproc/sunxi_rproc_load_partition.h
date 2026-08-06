/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sunxi's rproc rsc helper internal interface
 *
 * Copyright (c) 2023 Allwinner Technology Co.,Ltd.
 *
 * Author: shihongfu <fanjiahao@allwinnertech.com>
 *
 */

#ifndef __SUNXI_RPROC_LOAD_PARTITION_H__
#define __SUNXI_RPROC_LOAD_PARTITION_H__
#include <linux/remoteproc.h>

int load_from_partition(const char *partition, void *dst, size_t size);

#endif /* __SUNXI_RPROC_LOAD_PARTITION_H__ */
