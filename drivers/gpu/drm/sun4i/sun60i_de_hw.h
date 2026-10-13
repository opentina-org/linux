/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */

#ifndef _SUN60I_DE_HW_H_
#define _SUN60I_DE_HW_H_

#include "sun60i_de.h"

int sun60i_de_hw_commit(struct sun60i_de *de);
void sun60i_de_hw_disable(struct sun60i_de *de);
int sun60i_de_hw_bus_init(struct sun60i_de *de);
int sun60i_de_hw_bus_verify(struct sun60i_de *de);

#endif /* _SUN60I_DE_HW_H_ */
