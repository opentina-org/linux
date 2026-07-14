/* SPDX-License-Identifier: (GPL-2.0-or-later OR MIT) */
/*
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#ifndef _DT_BINDINGS_CLK_SUN60I_A733_CPUPLL_H_
#define _DT_BINDINGS_CLK_SUN60I_A733_CPUPLL_H_

#define CLK_PLL_CPU_BACK	0
#define CLK_PLL_CPU_L		1
#define CLK_PLL_CPU_B		2
#define CLK_PLL_CPU_DSU		3
#define CLK_CPU_L		4
#define CLK_CPU_B		5
#define CLK_CPU_DSU		6

#define CLK_CPUPLL_MAX_NO	(CLK_CPU_DSU + 1)

#endif /* _DT_BINDINGS_CLK_SUN60I_A733_CPUPLL_H_ */
