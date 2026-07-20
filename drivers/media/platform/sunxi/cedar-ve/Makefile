# SPDX-License-Identifier: GPL-2.0-only
#
# Allwinner A733 / sun60iw2 cedar-ve
#

ccflags-y += -I$(srctree)/$(src)
ccflags-y += -I$(srctree)/drivers/iommu
ccflags-$(CONFIG_VIDEO_SUN60I_CEDAR_VE_DEBUG) += -DDYNAMIC_DEBUG_MODULE

obj-$(CONFIG_VIDEO_SUN60I_CEDAR_VE) += sunxi-ve.o

sunxi-ve-objs := cedar_ve.o
sunxi-ve-objs += flush_cache.o
sunxi-ve-objs += platforms/ve_plat_sun60iw2.o

ifneq ($(CONFIG_RPMSG),)
sunxi-ve-objs += rpmsg_ve.o
else
sunxi-ve-objs += rpmsg_ve_stub.o
endif
