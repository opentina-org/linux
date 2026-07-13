// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner Sun60i (A733) USB host platform helpers
 *
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#include "usb-sun60i.h"

#if IS_ENABLED(CONFIG_SUNXI_IOMMU)
#include "../../iommu/sunxi-iommu.h"
#endif

#define SUN60I_HCDS_PER_PORT		2

#define SUN60I_IOMMU_MAX_MASTERS	16

#define SUN60I_USB_PORT_READY_MASK	(BIT(SUN60I_USB_CLIENT_EHCI) | \
					 BIT(SUN60I_USB_CLIENT_OHCI))

#define SUN60I_USB_PMU_PASSBY_OHCI_BULK		BIT(15)
#define SUN60I_USB_PMU_PASSBY_INCR16		BIT(11)
#define SUN60I_USB_PMU_PASSBY_INCR4		BIT(9)
#define SUN60I_USB_PMU_PASSBY_INCRX_ALIGN	BIT(8)
#define SUN60I_USB_PMU_PASSBY_UTMI		BIT(0)

#define SUN60I_USB_PMU_PASSBY_ENABLE		(SUN60I_USB_PMU_PASSBY_OHCI_BULK | \
						 SUN60I_USB_PMU_PASSBY_INCR16 | \
						 SUN60I_USB_PMU_PASSBY_INCR4 | \
						 SUN60I_USB_PMU_PASSBY_INCRX_ALIGN | \
						 SUN60I_USB_PMU_PASSBY_UTMI)

static DEFINE_MUTEX(sun60i_usb_lock);
static atomic_t sun60i_usb_phy_cnt[SUN60I_USB_MAX_PORTS];
static atomic_t sun60i_usb_passby_cnt[SUN60I_USB_MAX_PORTS];
static atomic_t sun60i_port_client_mask[SUN60I_USB_MAX_PORTS];
#if IS_ENABLED(CONFIG_SUNXI_IOMMU)
static atomic_t sun60i_iommu_master_users[SUN60I_IOMMU_MAX_MASTERS];
#endif

int sun60i_usb_port_power_sync(struct sun60i_usb_host *host, unsigned int client)
{
	unsigned int port = host->port;
	unsigned int bit = BIT(client);
	unsigned int mask;

	if (port >= SUN60I_USB_MAX_PORTS || client > SUN60I_USB_CLIENT_OHCI)
		return -EINVAL;

	mask = atomic_fetch_or(bit, &sun60i_port_client_mask[port]) | bit;
	if (mask != SUN60I_USB_PORT_READY_MASK)
		return -EPROBE_DEFER;

	return 0;
}
EXPORT_SYMBOL_GPL(sun60i_usb_port_power_sync);

void sun60i_usb_port_power_unsync(struct sun60i_usb_host *host, unsigned int client)
{
	unsigned int port = host->port;
	unsigned int bit = BIT(client);

	if (port >= SUN60I_USB_MAX_PORTS || client > SUN60I_USB_CLIENT_OHCI)
		return;

	atomic_andnot(bit, &sun60i_port_client_mask[port]);
}
EXPORT_SYMBOL_GPL(sun60i_usb_port_power_unsync);

#if IS_ENABLED(CONFIG_SUNXI_IOMMU)
static int sun60i_usb_get_iommu_config(struct device *dev,
				       unsigned int *master_id,
				       bool *enable)
{
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_args(dev->of_node, "iommus",
					 "#iommu-cells", 0, &args);
	if (ret)
		return ret;

	if (args.args_count < 2)
		return -EINVAL;

	*master_id = args.args[0];
	*enable = !!args.args[1];

	return 0;
}
#endif

int sun60i_usb_iommu_attach(struct device *dev)
{
#if IS_ENABLED(CONFIG_SUNXI_IOMMU)
	unsigned int master_id;
	bool enable;
	int ret;

	ret = sun60i_usb_get_iommu_config(dev, &master_id, &enable);
	if (ret)
		return 0;

	if (!enable || master_id >= SUN60I_IOMMU_MAX_MASTERS)
		return 0;

	if (atomic_inc_return(&sun60i_iommu_master_users[master_id]) == 1) {
		sunxi_reset_device_iommu(master_id);
		sunxi_enable_device_iommu(master_id, true);
		sunxi_iommu_master_ready(dev);
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(sun60i_usb_iommu_attach);

void sun60i_usb_iommu_detach(struct device *dev)
{
#if IS_ENABLED(CONFIG_SUNXI_IOMMU)
	unsigned int master_id;
	bool enable;
	int ret;

	ret = sun60i_usb_get_iommu_config(dev, &master_id, &enable);
	if (ret)
		return;

	if (!enable || master_id >= SUN60I_IOMMU_MAX_MASTERS)
		return;

	if (atomic_dec_return(&sun60i_iommu_master_users[master_id]) == 0)
		sunxi_enable_device_iommu(master_id, false);
#endif
}
EXPORT_SYMBOL_GPL(sun60i_usb_iommu_detach);

int sun60i_usb_dma_setup(struct device *dev)
{
	struct device_dma_parameters *parms;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	parms = dev->dma_parms;
	if (!parms) {
		parms = devm_kzalloc(dev, sizeof(*parms), GFP_KERNEL);
		if (!parms)
			return -ENOMEM;
		dev->dma_parms = parms;
	}

	dma_set_max_seg_size(dev, SZ_64K);

	return 0;
}
EXPORT_SYMBOL_GPL(sun60i_usb_dma_setup);

static void __iomem *sun60i_usb_map_pmu(struct device *dev,
					struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return NULL;

	return devm_ioremap(dev, (res->start & ~0x400UL) + 0x800,
			    sizeof(u32));
}

static void sun60i_usb_passby_set(struct sun60i_usb_host *host, bool enable)
{
	u32 val;

	if (!host->pmu)
		return;

	val = readl(host->pmu);
	if (enable)
		val |= SUN60I_USB_PMU_PASSBY_ENABLE;
	else
		val &= ~SUN60I_USB_PMU_PASSBY_ENABLE;
	writel(val, host->pmu);
}

int sun60i_usb_host_init(struct sun60i_usb_host *host,
			 struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	host->dev = dev;
	host->powered = false;

	ret = of_property_read_u32(dev->of_node, "allwinner,usb-port",
				   &host->port);
	if (ret)
		host->port = 0;

	if (host->port >= SUN60I_USB_MAX_PORTS)
		return -EINVAL;

	host->pmu = sun60i_usb_map_pmu(dev, pdev);
	if (!host->pmu)
		return -ENOMEM;

	host->phy = devm_phy_get(dev, "usb");
	if (IS_ERR(host->phy))
		return dev_err_probe(dev, PTR_ERR(host->phy),
				     "failed to get USB PHY\n");

	ret = devm_clk_bulk_get_all(dev, &host->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get clocks\n");
	host->num_clks = ret;

	host->rst_hci = devm_reset_control_get_optional_exclusive(dev, "hci");
	if (IS_ERR(host->rst_hci))
		return PTR_ERR(host->rst_hci);

	return 0;
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_init);

void sun60i_usb_host_exit(struct sun60i_usb_host *host)
{
	if (host->powered)
		sun60i_usb_host_power_off(host);
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_exit);

int sun60i_usb_host_power_on(struct sun60i_usb_host *host)
{
	unsigned int port = host->port;
	int ret;

	if (host->powered)
		return 0;

	ret = clk_bulk_prepare_enable(host->num_clks, host->clks);
	if (ret)
		return ret;

	mutex_lock(&sun60i_usb_lock);

	if (!atomic_read(&sun60i_usb_passby_cnt[port]))
		sun60i_usb_passby_set(host, true);
	atomic_inc(&sun60i_usb_passby_cnt[port]);

	if (!atomic_read(&sun60i_usb_phy_cnt[port])) {
		ret = phy_power_on(host->phy);
		if (ret)
			goto err_unlock;
	}
	atomic_inc(&sun60i_usb_phy_cnt[port]);

	mutex_unlock(&sun60i_usb_lock);

	host->powered = true;

	return 0;

err_unlock:
	if (atomic_read(&sun60i_usb_passby_cnt[port]) == 1)
		sun60i_usb_passby_set(host, false);
	if (atomic_read(&sun60i_usb_passby_cnt[port]))
		atomic_dec(&sun60i_usb_passby_cnt[port]);
	mutex_unlock(&sun60i_usb_lock);
	clk_bulk_disable_unprepare(host->num_clks, host->clks);

	return ret;
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_power_on);

int sun60i_usb_host_reset_deassert(struct sun60i_usb_host *host)
{
	if (!host->rst_hci)
		return 0;

	return reset_control_deassert(host->rst_hci);
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_reset_deassert);

void sun60i_usb_host_reset_assert(struct sun60i_usb_host *host)
{
	if (host->rst_hci)
		reset_control_assert(host->rst_hci);
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_reset_assert);

void sun60i_usb_host_power_off(struct sun60i_usb_host *host)
{
	unsigned int port = host->port;

	if (!host->powered)
		return;

	mutex_lock(&sun60i_usb_lock);

	if (atomic_read(&sun60i_usb_phy_cnt[port]) == 1)
		phy_power_off(host->phy);
	if (atomic_read(&sun60i_usb_phy_cnt[port]))
		atomic_dec(&sun60i_usb_phy_cnt[port]);

	if (atomic_read(&sun60i_usb_passby_cnt[port]) == 1)
		sun60i_usb_passby_set(host, false);
	if (atomic_read(&sun60i_usb_passby_cnt[port]))
		atomic_dec(&sun60i_usb_passby_cnt[port]);

	mutex_unlock(&sun60i_usb_lock);

	reset_control_assert(host->rst_hci);
	clk_bulk_disable_unprepare(host->num_clks, host->clks);

	host->powered = false;
}
EXPORT_SYMBOL_GPL(sun60i_usb_host_power_off);
