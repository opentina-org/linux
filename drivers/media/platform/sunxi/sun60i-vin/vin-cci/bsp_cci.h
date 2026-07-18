/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 *
 * Copyright (c) 2007-2017 Allwinner Technology Co.,Ltd.
 *
 */

#ifndef __BSP_CCI__H__
#define __BSP_CCI__H__

#include "csi_cci_reg.h"
struct cci_tx_mode {
	struct cci_tx_buf tx_buf_mode;
	struct cci_tx_trig tx_trig_mode;
	unsigned int tx_trig_line_cnt;
};

struct cci_msg {
	struct cci_bus_fmt bus_fmt;
	unsigned char *pkt_buf;
	unsigned char pkt_num;
};

int bsp_csi_cci_set_base_addr(unsigned int sel, unsigned long addr);
void bsp_csi_cci_init(unsigned int sel);
void bsp_csi_cci_exit(unsigned int sel);
void bsp_csi_cci_init_helper(unsigned int sel);
int bsp_cci_irq_process(unsigned int sel);

int bsp_cci_tx_start_wait_done(unsigned int sel, struct cci_msg *msg);
int cci_wr_8_8(unsigned int sel, unsigned char reg, unsigned char data,
	       unsigned char slv);
int cci_wr_16_8(unsigned int sel, unsigned short reg, unsigned char data,
		unsigned char slv);
int cci_wr_16_16(unsigned int sel, unsigned short reg, unsigned short data,
		 unsigned char slv);
int cci_wr_16_32(unsigned int sel, unsigned short reg, unsigned int data,
		 unsigned char slv);
int cci_wr_8_16(unsigned int sel, unsigned char reg, unsigned short data,
		unsigned char slv);
int cci_wr_0_16(unsigned int sel, unsigned short data, unsigned char slv);
int cci_rd_8_8(unsigned int sel, unsigned char reg, unsigned char *data,
	       unsigned char slv);
int cci_rd_16_8(unsigned int sel, unsigned short reg, unsigned char *data,
		unsigned char slv);
int cci_rd_16_16(unsigned int sel, unsigned short reg, unsigned short *data,
		 unsigned char slv);
int cci_rd_16_32(unsigned int sel, unsigned short reg, unsigned int *data,
		 unsigned char slv);
int cci_rd_8_16(unsigned int sel, unsigned char reg, unsigned short *data,
		unsigned char slv);
int cci_rd_0_16(unsigned int sel, unsigned short *data, unsigned char slv);
int cci_wr_a16_d8_continuous(unsigned int sel, unsigned short reg,
			     unsigned char *data, unsigned char slv, int size);

void cci_s_power(unsigned int sel, int on_off);

#endif /* __BSP_CCI__H__ */
