// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A733 Crypto Engine TRNG support
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 * Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
 *
 * This file handle the TRNG
 *
 * You could find a link for the datasheet in Documentation/arch/arm/sunxi.rst
 */
#include "sun60i-ce.h"
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/hw_random.h>
/*
 * Note that according to the algorithm ID, 2 versions of the TRNG exists,
 * The first present in H3/H5/R40/A64 and the second present in H6.
 * This file adds support for both, but only the second is working
 * reliabily according to rngtest.
 **/

static int sun60i_ce_trng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct sun60i_ce_dev *ce;
	dma_addr_t dma_dst;
	int err = 0;
	int flow = 3;
	unsigned int todo;
	struct sun60i_ce_flow *chan;
	struct ce_hash_task *cet;
	void *d;

	ce = container_of(rng, struct sun60i_ce_dev, trng);

	/* round the data length to a multiple of 32*/
	todo = max + 32;
	todo -= todo % 32;

	d = kzalloc(todo, GFP_KERNEL | GFP_DMA);
	if (!d)
		return -ENOMEM;

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	ce->hwrng_stat_req++;
	ce->hwrng_stat_bytes += todo;
#endif

	dma_dst = dma_map_single(ce->dev, d, todo, DMA_FROM_DEVICE);
	if (dma_mapping_error(ce->dev, dma_dst)) {
		dev_err(ce->dev, "Cannot DMA MAP DST\n");
		err = -EFAULT;
		goto err_dst;
	}

	err = pm_runtime_resume_and_get(ce->dev);
	if (err < 0)
		goto err_pm;

	mutex_lock(&ce->rnglock);
	chan = &ce->chanlist[flow];

	cet = &chan->tl->hash;
	memset(cet, 0, sizeof(*cet));
	cet->t_common_ctl = cpu_to_le32(flow | CE_HASH_CTRL_LAST |
					     CE_HASH_CTRL_INT);
	cet->t_cmd = cpu_to_le32(CE_HASH_CMD_TRNG | CE_ID_HASH_SHA256);
	ce_set_dlen(cet->t_dlen, todo);
	ce_set_addr(cet->sg[0].dst_addr, dma_dst);
	cet->sg[0].dst_len = cpu_to_le32(todo);

	err = sun60i_ce_run_task(ce, flow, CE_TLR_HASH_RBG, "TRNG");
	mutex_unlock(&ce->rnglock);

	pm_runtime_put(ce->dev);

err_pm:
	dma_unmap_single(ce->dev, dma_dst, todo, DMA_FROM_DEVICE);

	if (!err) {
		memcpy(data, d, max);
		err = max;
	}
err_dst:
	kfree_sensitive(d);
	return err;
}

int sun60i_ce_hwrng_register(struct sun60i_ce_dev *ce)
{
	int ret;

	ce->trng.name = "sun60i-ce Crypto Engine TRNG";
	ce->trng.read = sun60i_ce_trng_read;

	ret = hwrng_register(&ce->trng);
	if (ret)
		dev_err(ce->dev, "Fail to register the TRNG\n");
	return ret;
}

void sun60i_ce_hwrng_unregister(struct sun60i_ce_dev *ce)
{
	hwrng_unregister(&ce->trng);
}
