/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 *    Filename: cedarv_ve.h
 *     Version: 0.01alpha
 * Description: Video engine driver API, Don't modify it in user space.
 *     License: GPLv2
 *
 *		Author  : xyliu <xyliu@allwinnertech.com>
 *		Date    : 2016/04/13
 *
 */
 /* Notice: It's video engine driver API, Don't modify it in user space. */

#ifndef _VE_PLAT_H_
#define _VE_PLAT_H_

#define DVFS_EFUSE_OFF	(0x48)

struct ve_dvfs_pair {
	unsigned int dvfs_index;
	const char dvfs_name[64];
	const char dvfs_freq_name[64];
	const char dvfs_volt_name[64];
};

extern struct ve_dvfs_pair ve_dvfs[3];

#endif
