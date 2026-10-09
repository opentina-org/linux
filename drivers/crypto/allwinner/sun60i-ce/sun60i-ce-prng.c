// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A733 Crypto Engine PRNG support
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 * Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
 *
 * This file handle the PRNG
 *
 * You could find a link for the datasheet in Documentation/arch/arm/sunxi.rst
 */
#include "sun60i-ce.h"
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <crypto/internal/rng.h>

int sun60i_ce_prng_init(struct crypto_tfm *tfm)
{
	struct sun60i_ce_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	memset(ctx, 0, sizeof(struct sun60i_ce_rng_tfm_ctx));
	return 0;
}

void sun60i_ce_prng_exit(struct crypto_tfm *tfm)
{
	struct sun60i_ce_rng_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	kfree_sensitive(ctx->seed);
	ctx->seed = NULL;
	ctx->slen = 0;
}

int sun60i_ce_prng_seed(struct crypto_rng *tfm, const u8 *seed,
		       unsigned int slen)
{
	struct sun60i_ce_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);

	if (ctx->seed && ctx->slen != slen) {
		kfree_sensitive(ctx->seed);
		ctx->slen = 0;
		ctx->seed = NULL;
	}
	if (!ctx->seed)
		ctx->seed = kmalloc(slen, GFP_KERNEL | GFP_DMA);
	if (!ctx->seed)
		return -ENOMEM;

	memcpy(ctx->seed, seed, slen);
	ctx->slen = slen;

	return 0;
}

int sun60i_ce_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen)
{
	struct sun60i_ce_rng_tfm_ctx *ctx = crypto_rng_ctx(tfm);
	struct rng_alg *alg = crypto_rng_alg(tfm);
	struct sun60i_ce_alg_template *algt;
	struct sun60i_ce_dev *ce;
	dma_addr_t dma_iv, dma_dst;
	int err = 0;
	int flow = 3;
	unsigned int todo;
	struct sun60i_ce_flow *chan;
	struct ce_hash_task *cet;
	void *d;

	algt = container_of(alg, struct sun60i_ce_alg_template, alg.rng);
	ce = algt->ce;

	if (ctx->slen == 0) {
		dev_err(ce->dev, "not seeded\n");
		return -EINVAL;
	}

	/* we want dlen + seedsize rounded up to a multiple of PRNG_DATA_SIZE */
	todo = dlen + ctx->slen + PRNG_DATA_SIZE * 2;
	todo -= todo % PRNG_DATA_SIZE;

	d = kzalloc(todo, GFP_KERNEL | GFP_DMA);
	if (!d) {
		err = -ENOMEM;
		goto err_mem;
	}

	dev_dbg(ce->dev, "%s PRNG slen=%u dlen=%u todo=%u multi=%u\n", __func__,
		slen, dlen, todo, todo / PRNG_DATA_SIZE);

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	algt->stat_req++;
	algt->stat_bytes += todo;
#endif

	dma_iv = dma_map_single(ce->dev, ctx->seed, ctx->slen, DMA_TO_DEVICE);
	if (dma_mapping_error(ce->dev, dma_iv)) {
		dev_err(ce->dev, "Cannot DMA MAP IV\n");
		err = -EFAULT;
		goto err_iv;
	}

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
	cet->t_cmd = cpu_to_le32(CE_HASH_CMD_PRNG | CE_ID_HASH_SHA1 |
				     CE_HASH_PRNG_RELOAD);
	ce_set_addr(cet->t_key, dma_iv);
	ce_set_dlen(cet->t_dlen, todo);
	ce_set_addr(cet->sg[0].dst_addr, dma_dst);
	cet->sg[0].dst_len = cpu_to_le32(todo);

	err = sun60i_ce_run_task(ce, flow, CE_TLR_HASH_RBG, "PRNG");
	mutex_unlock(&ce->rnglock);

	pm_runtime_put(ce->dev);

err_pm:
	dma_unmap_single(ce->dev, dma_dst, todo, DMA_FROM_DEVICE);
err_dst:
	dma_unmap_single(ce->dev, dma_iv, ctx->slen, DMA_TO_DEVICE);

	if (!err) {
		memcpy(dst, d, dlen);
		memcpy(ctx->seed, d + dlen, ctx->slen);
	}
err_iv:
	kfree_sensitive(d);
err_mem:
	return err;
}
