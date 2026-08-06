/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * remoteproc/sunxi_remoteproc.h
 *
 * Copyright (c) 2023 Allwinner Technology Co.,Ltd.
 *         http://www.allwinnertech.com
 *
 * allwinner sunxi remoteproc manager.
 *
 */

#ifndef __SUNXI_REMOTEPROC_H__
#define __SUNXI_REMOTEPROC_H__

#include <linux/remoteproc.h>

int sunxi_rproc_report_crash(const char *name, enum rproc_crash_type type);
int sunxi_rproc_send_mbox_msg(struct rproc *rproc, u32 data);

#endif
