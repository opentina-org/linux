# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RPMSG)		+= rpmsg_core.o
obj-$(CONFIG_RPMSG_CHAR)	+= rpmsg_char.o
obj-$(CONFIG_RPMSG_CTRL)	+= rpmsg_ctrl.o
obj-$(CONFIG_RPMSG_NS)		+= rpmsg_ns.o
obj-$(CONFIG_RPMSG_MTK_SCP)	+= mtk_rpmsg.o
CFLAGS_qcom_glink_native.o	:= -I$(src)
qcom_glink-objs			:= qcom_glink_native.o qcom_glink_ssr.o
obj-$(CONFIG_RPMSG_QCOM_GLINK) += qcom_glink.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_RPM) += qcom_glink_rpm.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_SMEM) += qcom_glink_smem.o
obj-$(CONFIG_RPMSG_QCOM_SMD)	+= qcom_smd.o
obj-$(CONFIG_RPMSG_VIRTIO)	+= virtio_rpmsg_bus.o

# Allwinner (Tina BSP) rpmsg
ccflags-$(CONFIG_AW_RPMSG_CTRL)		+= -I$(src)
ccflags-$(CONFIG_AW_RPMSG_VIRTIO)	+= -I$(src)
obj-$(CONFIG_AW_RPMSG_CTRL)		+= sunxi_rpmsg_ctrl.o
sunxi_rpmsg_ctrl-y			:= rpmsg_client.o
sunxi_rpmsg_ctrl-y			+= rpmsg_master.o
obj-$(CONFIG_AW_RPMSG_HEARTBEAT)	+= rpmsg_heartbeat.o
obj-$(CONFIG_AW_RPMSG_NOTIFY)		+= rpmsg_notify.o
obj-$(CONFIG_AW_RPMSG_VIRTIO)		+= aw_virtio_rpmsg_bus.o
