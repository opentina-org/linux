# SPDX-License-Identifier: GPL-2.0-only

ccflags-$(CONFIG_SUNXI_NSI) += -DCONFIG_SUNXI_NSI_CPU_CHANNEL -DAW_NSI_CPU_CHANNEL

obj-$(CONFIG_SUNXI_NSI) += sunxi_nsi.o
sunxi_nsi-y := sunxi-nsi.o sunxi-nsi-ecc.o sunxi-nsi-bw.o
