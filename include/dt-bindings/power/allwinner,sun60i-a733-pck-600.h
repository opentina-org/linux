/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Power domain indices for Allwinner A733 (sun60iw2) PCK-600.
 * Domain ID equals hardware slot index (BASE = id << 12).
 */

#ifndef _DT_BINDINGS_POWER_SUN60I_A733_PCK600_H_
#define _DT_BINDINGS_POWER_SUN60I_A733_PCK600_H_

#define PD_VI			0
#define PD_DE_SYS		1
#define PD_VE_DEC		2
#define PD_VE_ENC		3
#define PD_NPU			4
#define PD_GPU_TOP		5
#define PD_GPU_CORE		6
#define PD_PCIE			7
#define PD_USB2			8
#define PD_VO			9
#define PD_VO1			10

/* Aliases matching BSP sun60iw2-power.h naming used by reference DTS */
#define SUN60IW2_PCK_VI		PD_VI
#define SUN60IW2_PCK_DE_SYS	PD_DE_SYS
#define SUN60IW2_PCK_VE_DEC	PD_VE_DEC
#define SUN60IW2_PCK_VE_ENC	PD_VE_ENC
#define SUN60IW2_PCK_NPU	PD_NPU
#define SUN60IW2_PCK_GPU_TOP	PD_GPU_TOP
#define SUN60IW2_PCK_GPU_CORE	PD_GPU_CORE
#define SUN60IW2_PCK_PCIE	PD_PCIE
#define SUN60IW2_PCK_USB2	PD_USB2
#define SUN60IW2_PCK_VO		PD_VO
#define SUN60IW2_PCK_VO1	PD_VO1

#endif /* _DT_BINDINGS_POWER_SUN60I_A733_PCK600_H_ */
