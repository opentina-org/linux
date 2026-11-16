/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2017 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * A V4L2 driver for nvp6324 cameras and AHD Coax protocol.
 *
 * Authors:  Li Huiyu <lihuiyu@allwinnertech.com>
 *
 */

#ifndef __STRFUNC_H__
#define __STRFUNC_H__

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* __cplusplus */

#define STRFMT_ADDR32    "%#010lX"
#define STRFMT_ADDR32_2  "0x%08lX"

extern int StrToNumber(char *str, unsigned int *ulValue);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __STRFUNC_H__ */
