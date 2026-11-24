/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2017 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * A V4L2 driver for N123 cameras.
 *
 * Authors:  Miachel chen <michaelchen@allwinnertech.com>
 *
 */

#define AHD_1080P_1CH  /* else is 720P_4CH */

u32 gpio_i2c_write(u8 da, u8 reg, u8 val);
u32 gpio_i2c_read(u8 da, u8 reg);
int check_id(unsigned int dec);
int check_rev(unsigned int dec);
void read_bank_value(void);
int sensor_init_hardware(u32 val);
