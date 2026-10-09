// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A733 Crypto Engine hash support
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 * Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
 *
 * This file add support for MD5 and SHA1/SHA224/SHA256/SHA384/SHA512.
 *
 * You could find the datasheet in Documentation/arch/arm/sunxi.rst
 */

#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <linux/bottom_half.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "sun60i-ce.h"

static void sun60i_ce_hash_stat_fb_inc(struct crypto_ahash *tfm)
{
	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG)) {
		struct sun60i_ce_alg_template *algt;
		struct ahash_alg *alg = crypto_ahash_alg(tfm);

		algt = container_of(alg, struct sun60i_ce_alg_template,
				    alg.hash.base);
		algt->stat_fb++;
	}
}

int sun60i_ce_hash_init_tfm(struct crypto_ahash *tfm)
{
	struct sun60i_ce_hash_tfm_ctx *op = crypto_ahash_ctx(tfm);
	struct ahash_alg *alg = crypto_ahash_alg(tfm);
	struct sun60i_ce_alg_template *algt;
	int err;

	algt = container_of(alg, struct sun60i_ce_alg_template, alg.hash.base);
	op->ce = algt->ce;

	/* FALLBACK */
	op->fallback_tfm = crypto_alloc_ahash(crypto_ahash_alg_name(tfm), 0,
					      CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(op->fallback_tfm)) {
		dev_err(algt->ce->dev, "Fallback driver could no be loaded\n");
		return PTR_ERR(op->fallback_tfm);
	}

	crypto_ahash_set_statesize(tfm,
				   crypto_ahash_statesize(op->fallback_tfm));

	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct sun60i_ce_hash_reqctx) +
				 crypto_ahash_reqsize(op->fallback_tfm) +
				 CRYPTO_DMA_PADDING);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG))
		memcpy(algt->fbname,
		       crypto_ahash_driver_name(op->fallback_tfm),
		       CRYPTO_MAX_ALG_NAME);

	err = pm_runtime_resume_and_get(op->ce->dev);
	if (err < 0)
		goto error_pm;
	return 0;
error_pm:
	crypto_free_ahash(op->fallback_tfm);
	return err;
}

void sun60i_ce_hash_exit_tfm(struct crypto_ahash *tfm)
{
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	crypto_free_ahash(tfmctx->fallback_tfm);
	pm_runtime_put_sync_suspend(tfmctx->ce->dev);
}

int sun60i_ce_hash_init(struct ahash_request *areq)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	memset(rctx, 0, sizeof(struct sun60i_ce_hash_reqctx));

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_init(&rctx->fallback_req);
}

int sun60i_ce_hash_export(struct ahash_request *areq, void *out)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_export(&rctx->fallback_req, out);
}

int sun60i_ce_hash_import(struct ahash_request *areq, const void *in)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);

	return crypto_ahash_import(&rctx->fallback_req, in);
}

int sun60i_ce_hash_final(struct ahash_request *areq)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun60i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, NULL, areq->result, 0);

	return crypto_ahash_final(&rctx->fallback_req);
}

int sun60i_ce_hash_update(struct ahash_request *areq)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, NULL, areq->nbytes);

	return crypto_ahash_update(&rctx->fallback_req);
}

int sun60i_ce_hash_finup(struct ahash_request *areq)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun60i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, areq->result,
				areq->nbytes);

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int sun60i_ce_hash_digest_fb(struct ahash_request *areq)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);

	sun60i_ce_hash_stat_fb_inc(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, tfmctx->fallback_tfm);
	ahash_request_set_callback(&rctx->fallback_req,
				   areq->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP,
				   areq->base.complete, areq->base.data);
	ahash_request_set_crypt(&rctx->fallback_req, areq->src, areq->result,
				areq->nbytes);

	return crypto_ahash_digest(&rctx->fallback_req);
}

static bool sun60i_ce_hash_need_fallback(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun60i_ce_alg_template *algt;
	struct scatterlist *sg;

	algt = container_of(alg, struct sun60i_ce_alg_template, alg.hash.base);

	if (areq->nbytes == 0) {
		if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG))
			algt->stat_fb_len0++;

		return true;
	}
	if (sg_nents_for_len(areq->src, areq->nbytes) > MAX_SG) {
		if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG))
			algt->stat_fb_maxsg++;

		return true;
	}
	sg = areq->src;
	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32))) {
			if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG))
				algt->stat_fb_srcali++;

			return true;
		}
		sg = sg_next(sg);
	}
	return false;
}

int sun60i_ce_hash_digest(struct ahash_request *areq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun60i_ce_dev *ce = ctx->ce;
	struct crypto_engine *engine;
	int e;

	if (sun60i_ce_hash_need_fallback(areq))
		return sun60i_ce_hash_digest_fb(areq);

	e = sun60i_ce_get_engine_number(ce);
	rctx->flow = e;
	engine = ce->chanlist[e].engine;

	return crypto_transfer_hash_request_to_engine(engine, areq);
}

static int sun60i_ce_hash_prepare(struct ahash_request *areq,
				  struct ce_hash_task *cet)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun60i_ce_alg_template *algt;
	struct sun60i_ce_dev *ce;
	struct scatterlist *sg;
	int nr_sgs, err;
	unsigned int len;
	u64 total_bits;
	int i, todo;
	int digestsize;

	algt = container_of(alg, struct sun60i_ce_alg_template, alg.hash.base);
	ce = algt->ce;

	digestsize = crypto_ahash_digestsize(tfm);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG))
		algt->stat_req++;

	dev_dbg(ce->dev, "%s %s len=%d\n", __func__,
		crypto_tfm_alg_name(areq->base.tfm), areq->nbytes);

	memset(cet, 0, sizeof(*cet));
	cet->t_common_ctl = cpu_to_le32(rctx->flow | CE_HASH_CTRL_LAST |
					     CE_HASH_CTRL_INT);
	cet->t_cmd = cpu_to_le32(ce->variant->alg_hash[algt->ce_algo_id]);

	rctx->nr_sgs = sg_nents_for_len(areq->src, areq->nbytes);
	nr_sgs = dma_map_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > MAX_SG) {
		dev_err(ce->dev, "Invalid sg number %d\n", nr_sgs);
		err = -EINVAL;
		goto err_out;
	}

	len = areq->nbytes;
	for_each_sg(areq->src, sg, nr_sgs, i) {
		ce_set_addr(cet->sg[i].src_addr, sg_dma_address(sg));
		todo = min(len, sg_dma_len(sg));
		cet->sg[i].src_len = cpu_to_le32(todo);
		len -= todo;
	}
	if (len > 0) {
		dev_err(ce->dev, "remaining len %d\n", len);
		err = -EINVAL;
		goto err_unmap_src;
	}

	rctx->result_len = digestsize;
	rctx->addr_res = dma_map_single(ce->dev, rctx->result, rctx->result_len,
					DMA_FROM_DEVICE);
	ce_set_addr(cet->sg[0].dst_addr, rctx->addr_res);
	cet->sg[0].dst_len = cpu_to_le32(rctx->result_len);
	if (dma_mapping_error(ce->dev, rctx->addr_res)) {
		dev_err(ce->dev, "DMA map dest\n");
		err = -EINVAL;
		goto err_unmap_src;
	}

	total_bits = (u64)areq->nbytes * 8;
	ce_set_dlen(cet->t_dlen, total_bits);
	cet->total_len_63_32 = cpu_to_le32(upper_32_bits(total_bits));
	cet->total_len_31_0 = cpu_to_le32(lower_32_bits(total_bits));

	return 0;
err_unmap_src:
	dma_unmap_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);

err_out:
	return err;
}

static void sun60i_ce_hash_unprepare(struct ahash_request *areq,
				    struct ce_hash_task *cet)
{
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun60i_ce_dev *ce = ctx->ce;

	dma_unmap_single(ce->dev, rctx->addr_res, rctx->result_len,
			 DMA_FROM_DEVICE);
	dma_unmap_sg(ce->dev, areq->src, rctx->nr_sgs, DMA_TO_DEVICE);
}

int sun60i_ce_hash_run(struct crypto_engine *engine, void *async_req)
{
	struct ahash_request *areq = ahash_request_cast(async_req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun60i_ce_hash_tfm_ctx *ctx = crypto_ahash_ctx(tfm);
	struct sun60i_ce_hash_reqctx *rctx = ahash_request_ctx_dma(areq);
	struct sun60i_ce_dev *ce = ctx->ce;
	struct sun60i_ce_flow *chan;
	int err;

	chan = &ce->chanlist[rctx->flow];

	err = sun60i_ce_hash_prepare(areq, &chan->tl->hash);
	if (err)
		return err;

	err = sun60i_ce_run_task(ce, rctx->flow, CE_TLR_HASH_RBG,
				crypto_ahash_alg_name(tfm));

	sun60i_ce_hash_unprepare(areq, &chan->tl->hash);

	if (!err)
		memcpy(areq->result, rctx->result,
		       crypto_ahash_digestsize(tfm));

	local_bh_disable();
	crypto_finalize_hash_request(engine, async_req, err);
	local_bh_enable();

	return 0;
}
