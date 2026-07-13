// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner Sun60i (A733) OHCI driver
 *
 * Copyright (c) 2025 Allwinner Technology Co.,Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"
#include "usb-sun60i.h"

#define DRIVER_DESC "OHCI Allwinner Sun60i driver"

struct sun60i_ohci_priv {
	struct sun60i_usb_host host;
};

static struct hc_driver __read_mostly sun60i_ohci_hc_driver;

static struct sun60i_ohci_priv *hcd_to_sun60i_ohci(struct usb_hcd *hcd)
{
	return (struct sun60i_ohci_priv *)hcd_to_ohci(hcd)->priv;
}

static int sun60i_ohci_probe(struct platform_device *pdev)
{
	struct sun60i_ohci_priv *priv;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq, err;

	if (usb_disabled())
		return -ENODEV;

	err = sun60i_usb_dma_setup(&pdev->dev);
	if (err)
		return err;

	hcd = usb_create_hcd(&sun60i_ohci_hc_driver, &pdev->dev,
			     dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	priv = hcd_to_sun60i_ohci(hcd);

	err = sun60i_usb_host_init(&priv->host, pdev);
	if (err)
		goto err_put_hcd;

	err = sun60i_usb_host_power_on(&priv->host);
	if (err)
		goto err_put_hcd;

	err = sun60i_usb_port_power_sync(&priv->host, SUN60I_USB_CLIENT_OHCI);
	if (err == -EPROBE_DEFER) {
		sun60i_usb_host_power_off(&priv->host);
		usb_put_hcd(hcd);
		return -EPROBE_DEFER;
	}
	if (err)
		goto err_power_off;

	hcd->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_power_off;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto err_power_off;
	}

	platform_set_drvdata(pdev, hcd);

	err = sun60i_usb_iommu_attach(&pdev->dev);
	if (err)
		goto err_power_off;

	err = sun60i_usb_host_reset_deassert(&priv->host);
	if (err)
		goto err_iommu_detach;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_reset_assert;

	device_wakeup_enable(hcd->self.controller);

	return 0;

err_reset_assert:
	sun60i_usb_host_reset_assert(&priv->host);
err_iommu_detach:
	sun60i_usb_iommu_detach(&pdev->dev);
err_power_off:
	sun60i_usb_port_power_unsync(&priv->host, SUN60I_USB_CLIENT_OHCI);
	sun60i_usb_host_power_off(&priv->host);
err_put_hcd:
	usb_put_hcd(hcd);

	return err;
}

static void sun60i_ohci_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct sun60i_ohci_priv *priv = hcd_to_sun60i_ohci(hcd);

	usb_remove_hcd(hcd);
	sun60i_usb_iommu_detach(&pdev->dev);
	sun60i_usb_port_power_unsync(&priv->host, SUN60I_USB_CLIENT_OHCI);
	sun60i_usb_host_power_off(&priv->host);
	usb_put_hcd(hcd);
}

#ifdef CONFIG_OF
static const struct of_device_id sun60i_ohci_ids[] = {
	{ .compatible = "allwinner,sun60i-a733-ohci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun60i_ohci_ids);
#endif

static struct platform_driver sun60i_ohci_driver = {
	.probe		= sun60i_ohci_probe,
	.remove		= sun60i_ohci_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "sun60i-ohci",
		.of_match_table = of_match_ptr(sun60i_ohci_ids),
	},
};

static const struct ohci_driver_overrides sun60i_ohci_overrides __initconst = {
	.extra_priv_size = sizeof(struct sun60i_ohci_priv),
};

static int __init sun60i_ohci_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ohci_init_driver(&sun60i_ohci_hc_driver, &sun60i_ohci_overrides);
	return platform_driver_register(&sun60i_ohci_driver);
}
module_init(sun60i_ohci_init);

static void __exit sun60i_ohci_exit(void)
{
	platform_driver_unregister(&sun60i_ohci_driver);
}
module_exit(sun60i_ohci_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Allwinner Technology Co.,Ltd.");
MODULE_LICENSE("GPL");
