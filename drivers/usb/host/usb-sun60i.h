/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Allwinner Sun60i (A733) USB host platform helpers
 *
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#ifndef __USB_SUN60I_H
#define __USB_SUN60I_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define SUN60I_USB_MAX_PORTS	3

#define SUN60I_USB_CLIENT_EHCI	0
#define SUN60I_USB_CLIENT_OHCI	1

struct sun60i_usb_host {
	struct device *dev;
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *rst_hci;
	struct phy *phy;
	void __iomem *pmu;
	unsigned int port;
	bool powered;
};

int sun60i_usb_dma_setup(struct device *dev);
int sun60i_usb_iommu_attach(struct device *dev);
void sun60i_usb_iommu_detach(struct device *dev);
int sun60i_usb_port_power_sync(struct sun60i_usb_host *host, unsigned int client);
void sun60i_usb_port_power_unsync(struct sun60i_usb_host *host, unsigned int client);
int sun60i_usb_host_reset_deassert(struct sun60i_usb_host *host);
void sun60i_usb_host_reset_assert(struct sun60i_usb_host *host);
int sun60i_usb_host_init(struct sun60i_usb_host *host,
			 struct platform_device *pdev);
void sun60i_usb_host_exit(struct sun60i_usb_host *host);
int sun60i_usb_host_power_on(struct sun60i_usb_host *host);
void sun60i_usb_host_power_off(struct sun60i_usb_host *host);

#endif /* __USB_SUN60I_H */
