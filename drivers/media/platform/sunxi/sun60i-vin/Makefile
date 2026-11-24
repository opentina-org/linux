# SPDX-License-Identifier: GPL-2.0-or-later
# Makefile for Allwinner A733 / sun60iw2 VIN (CSI / MIPI / ISP / VIPP).
# Copyright 2010 Steven King <sfking@fdwdc.com>
# Copyright(c) 2022-2027 Allwinnertech Co., Ltd

obj-$(CONFIG_CSI_VIN) += modules/sensor/
obj-$(CONFIG_CSI_VIN) += modules/actuator/
obj-$(CONFIG_CSI_VIN) += vin_io.o
obj-$(CONFIG_SENSOR_POWER) += modules/sensor_power/

vin_io-$(CONFIG_CSI_CCI) := vin-cci/csi_cci_reg.o
vin_io-$(CONFIG_CSI_CCI) += vin-cci/bsp_cci.o
vin_io-$(CONFIG_FLASH) += modules/flash/flash.o
vin_io-y += vin-cci/cci_helper.o
vin_io-y += vin-cci/sunxi_cci.o
vin_io-y += utility/vin_supply.o
vin_io-y += utility/vin_os.o
vin_io-y += modules/sensor/sensor-compat-ioctl32.o
vin_io-y += modules/sensor/sensor_helper.o

obj-$(CONFIG_CSI_VIN) += vin_v4l2.o
vin_v4l2-y := vin-csi/sunxi_csi.o
vin_v4l2-y += vin-csi/parser_reg.o
vin_v4l2-y += vin-mipi/sunxi_mipi.o
vin_v4l2-y += vin-mipi/combo_csi/combo_csi_reg.o
vin_v4l2-y += vin-isp/sunxi_isp.o
vin_v4l2-y += vin-isp/isp600/isp600_reg_cfg.o
vin_v4l2-$(CONFIG_SUPPORT_ISP_TDM) += vin-tdm/tdm200/tdm200_reg.o
vin_v4l2-y += vin-vipp/vipp200/vipp200_reg.o
vin_v4l2-y += vin-video/dma140/dma140_reg.o
vin_v4l2-$(CONFIG_SUPPORT_ISP_TDM) += vin-tdm/vin_tdm.o
vin_v4l2-y += modules/sensor-list/sensor_list.o
vin_v4l2-y += vin-vipp/sunxi_scaler.o
vin_v4l2-y += modules/flash/flash.o
vin_v4l2-y += utility/bsp_common.o
vin_v4l2-y += utility/config.o
vin_v4l2-y += vin-stat/vin_h3a.o
vin_v4l2-y += vin-video/vin_video.o
vin_v4l2-y += vin-video/vin_core.o
vin_v4l2-y += top_reg.o
vin_v4l2-y += vin.o

ccflags-y := -DDYNAMIC_DEBUG_MODULE
