// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A733 Crypto Engine driver
 *
 * Copyright (C) 2015-2019 Corentin Labbe <clabbe.montjoie@gmail.com>
 * Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
 *
 * Core file which registers crypto algorithms supported by the CryptoEngine.
 *
 * You could find a link for the datasheet in Documentation/arch/arm/sunxi.rst
 */

#include <crypto/engine.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "sun60i-ce.h"

static const struct ce_variant sun60i_a733_ce_variant = {
	.alg_cipher = { CE_ALG_AES, CE_ALG_DES, CE_ALG_3DES },
	.alg_hash = { 0, 1, 2, 3, 4, 5 },
	.op_mode = { CE_OP_ECB, CE_OP_CBC },
	.ce_clks = {
		{ "bus", 0, 200000000 },
		{ "mod", 400000000, 0 },
		{ "ram", 0, 0 },
		{ "sys", 0, 0 },
	},
};

static void sun60i_ce_dump_task_descriptors(struct sun60i_ce_flow *chan)
{
	print_hex_dump(KERN_INFO, "TASK: ", DUMP_PREFIX_NONE, 16, 4,
		       chan->tl, sizeof(*chan->tl), false);
}

/*
 * sun60i_ce_get_engine_number() get the next channel slot
 * This is a simple round-robin way of getting the next channel
 * The flow 3 is reserve for xRNG operations
 */
int sun60i_ce_get_engine_number(struct sun60i_ce_dev *ce)
{
	return atomic_inc_return(&ce->flow) % (MAXFLOW - 1);
}

int sun60i_ce_run_task(struct sun60i_ce_dev *ce, int flow, u32 load,
			      const char *name)
{
	unsigned long timeout;
	dma_addr_t task_addr = ce->chanlist[flow].t_phy;
	u32 v;
	u8 error;
	int err;

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	ce->chanlist[flow].stat_req++;
#endif

	mutex_lock(&ce->mlock);

	v = readl(ce->base + CE_ICR);
	v |= BIT(flow);
	writel(v, ce->base + CE_ICR);

	reinit_completion(&ce->chanlist[flow].complete);
	ce->chanlist[flow].status = 0;
	writel(lower_32_bits(task_addr), ce->base + CE_TDA_LO);
	writel(upper_32_bits(task_addr) & CE_TDA_HI_MASK,
	       ce->base + CE_TDA_HI);

	/* Make the coherent descriptor visible before asking CE to fetch it. */
	dma_wmb();
	writel(load, ce->base + CE_TLR);
	mutex_unlock(&ce->mlock);

	timeout = wait_for_completion_timeout(&ce->chanlist[flow].complete,
					msecs_to_jiffies(CE_DMA_TIMEOUT_MS));
	if (!timeout) {
		dev_err(ce->dev, "DMA timeout for %s on flow %d\n", name, flow);
		return -ETIMEDOUT;
	}

	if (ce->chanlist[flow].status == CE_ISR_DONE)
		return 0;

	error = (readl(ce->base + CE_ESR) >> (flow * 8)) & 0xff;
	err = -EIO;
	switch (error) {
	case CE_ERR_ALGO_NOTSUP:
		dev_err(ce->dev, "%s: algorithm not supported\n", name);
		break;
	case CE_ERR_KEYSRAM:
		dev_err(ce->dev, "%s: key SRAM access error\n", name);
		break;
	case CE_ERR_KEYLADDER:
		dev_err(ce->dev, "%s: key ladder configuration error\n", name);
		break;
	case CE_ERR_DATALEN:
		dev_err(ce->dev, "%s: data length error\n", name);
		break;
	default:
		dev_err(ce->dev, "%s: error %#x on flow %d\n",
			name, error, flow);
		break;
	}
	sun60i_ce_dump_task_descriptors(&ce->chanlist[flow]);

	return err;
}

static irqreturn_t sun60i_ce_irq_handler(int irq, void *data)
{
	struct sun60i_ce_dev *ce = data;
	int flow;
	u32 pending;

	pending = readl(ce->base + CE_ISR);
	if (!pending)
		return IRQ_NONE;

	for (flow = 0; flow < MAXFLOW; flow++) {
		u32 status = CE_ISR_CHAN_STATUS(pending, flow);

		if (!status)
			continue;

		writel(CE_ISR_CHAN_MASK(flow), ce->base + CE_ISR);
		ce->chanlist[flow].status = status;
		complete(&ce->chanlist[flow].complete);
	}

	return IRQ_HANDLED;
}

static struct sun60i_ce_alg_template sun60i_ce_algs[] = {
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_AES,
	.ce_blockmode = CE_ID_OP_CBC,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "cbc(aes)",
			.cra_driver_name = "cbc-aes-sun60i-ce",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun60i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun60i_ce_cipher_init,
			.cra_exit = sun60i_ce_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= sun60i_ce_aes_setkey,
		.encrypt	= sun60i_ce_skencrypt,
		.decrypt	= sun60i_ce_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun60i_ce_cipher_do_one,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_AES,
	.ce_blockmode = CE_ID_OP_ECB,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(aes)",
			.cra_driver_name = "ecb-aes-sun60i-ce",
			.cra_priority = 400,
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun60i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun60i_ce_cipher_init,
			.cra_exit = sun60i_ce_cipher_exit,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= sun60i_ce_aes_setkey,
		.encrypt	= sun60i_ce_skencrypt,
		.decrypt	= sun60i_ce_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun60i_ce_cipher_do_one,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_DES3,
	.ce_blockmode = CE_ID_OP_CBC,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "cbc(des3_ede)",
			.cra_driver_name = "cbc-des3-sun60i-ce",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun60i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun60i_ce_cipher_init,
			.cra_exit = sun60i_ce_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.ivsize		= DES3_EDE_BLOCK_SIZE,
		.setkey		= sun60i_ce_des3_setkey,
		.encrypt	= sun60i_ce_skencrypt,
		.decrypt	= sun60i_ce_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun60i_ce_cipher_do_one,
	},
},
{
	.type = CRYPTO_ALG_TYPE_SKCIPHER,
	.ce_algo_id = CE_ID_CIPHER_DES3,
	.ce_blockmode = CE_ID_OP_ECB,
	.alg.skcipher.base = {
		.base = {
			.cra_name = "ecb(des3_ede)",
			.cra_driver_name = "ecb-des3-sun60i-ce",
			.cra_priority = 400,
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_ctxsize = sizeof(struct sun60i_cipher_tfm_ctx),
			.cra_module = THIS_MODULE,
			.cra_alignmask = 0xf,
			.cra_init = sun60i_ce_cipher_init,
			.cra_exit = sun60i_ce_cipher_exit,
		},
		.min_keysize	= DES3_EDE_KEY_SIZE,
		.max_keysize	= DES3_EDE_KEY_SIZE,
		.setkey		= sun60i_ce_des3_setkey,
		.encrypt	= sun60i_ce_skencrypt,
		.decrypt	= sun60i_ce_skdecrypt,
	},
	.alg.skcipher.op = {
		.do_one_request = sun60i_ce_cipher_do_one,
	},
},
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_HASH
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_MD5,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct md5_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},

},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA1,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct sha1_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA224,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA256,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct sha256_state),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA384,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = SHA384_DIGEST_SIZE,
			.statesize = sizeof(struct sha512_state),
			.base = {
				.cra_name = "sha384",
				.cra_driver_name = "sha384-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},
},
{	.type = CRYPTO_ALG_TYPE_AHASH,
	.ce_algo_id = CE_ID_HASH_SHA512,
	.alg.hash.base = {
		.init = sun60i_ce_hash_init,
		.update = sun60i_ce_hash_update,
		.final = sun60i_ce_hash_final,
		.finup = sun60i_ce_hash_finup,
		.digest = sun60i_ce_hash_digest,
		.export = sun60i_ce_hash_export,
		.import = sun60i_ce_hash_import,
		.init_tfm = sun60i_ce_hash_init_tfm,
		.exit_tfm = sun60i_ce_hash_exit_tfm,
		.halg = {
			.digestsize = SHA512_DIGEST_SIZE,
			.statesize = sizeof(struct sha512_state),
			.base = {
				.cra_name = "sha512",
				.cra_driver_name = "sha512-sun60i-ce",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH |
					CRYPTO_ALG_ASYNC |
					CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct sun60i_ce_hash_tfm_ctx),
				.cra_module = THIS_MODULE,
			}
		}
	},
	.alg.hash.op = {
		.do_one_request = sun60i_ce_hash_run,
	},
},
#endif
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_PRNG
{
	.type = CRYPTO_ALG_TYPE_RNG,
	.alg.rng = {
		.base = {
			.cra_name		= "stdrng",
			.cra_driver_name	= "sun60i-ce-prng",
			.cra_priority		= 300,
			.cra_ctxsize		= sizeof(struct sun60i_ce_rng_tfm_ctx),
			.cra_module		= THIS_MODULE,
			.cra_init		= sun60i_ce_prng_init,
			.cra_exit		= sun60i_ce_prng_exit,
		},
		.generate               = sun60i_ce_prng_generate,
		.seed                   = sun60i_ce_prng_seed,
		.seedsize               = PRNG_SEED_SIZE,
	}
},
#endif
};

static int sun60i_ce_debugfs_show(struct seq_file *seq, void *v)
{
	struct sun60i_ce_dev *ce __maybe_unused = seq->private;
	unsigned int i;

	for (i = 0; i < MAXFLOW; i++)
		seq_printf(seq, "Channel %d: nreq %lu\n", i,
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
			   ce->chanlist[i].stat_req);
#else
			   0ul);
#endif

	for (i = 0; i < ARRAY_SIZE(sun60i_ce_algs); i++) {
		if (!sun60i_ce_algs[i].ce)
			continue;
		switch (sun60i_ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   sun60i_ce_algs[i].alg.skcipher.base.base.cra_driver_name,
				   sun60i_ce_algs[i].alg.skcipher.base.base.cra_name,
				   sun60i_ce_algs[i].stat_req, sun60i_ce_algs[i].stat_fb);
			seq_printf(seq, "\tLast fallback is: %s\n",
				   sun60i_ce_algs[i].fbname);
			seq_printf(seq, "\tFallback due to 0 length: %lu\n",
				   sun60i_ce_algs[i].stat_fb_len0);
			seq_printf(seq, "\tFallback due to length !mod16: %lu\n",
				   sun60i_ce_algs[i].stat_fb_mod16);
			seq_printf(seq, "\tFallback due to length < IV: %lu\n",
				   sun60i_ce_algs[i].stat_fb_leniv);
			seq_printf(seq, "\tFallback due to source alignment: %lu\n",
				   sun60i_ce_algs[i].stat_fb_srcali);
			seq_printf(seq, "\tFallback due to dest alignment: %lu\n",
				   sun60i_ce_algs[i].stat_fb_dstali);
			seq_printf(seq, "\tFallback due to source length: %lu\n",
				   sun60i_ce_algs[i].stat_fb_srclen);
			seq_printf(seq, "\tFallback due to dest length: %lu\n",
				   sun60i_ce_algs[i].stat_fb_dstlen);
			seq_printf(seq, "\tFallback due to SG numbers: %lu\n",
				   sun60i_ce_algs[i].stat_fb_maxsg);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			seq_printf(seq, "%s %s reqs=%lu fallback=%lu\n",
				   sun60i_ce_algs[i].alg.hash.base.halg.base.cra_driver_name,
				   sun60i_ce_algs[i].alg.hash.base.halg.base.cra_name,
				   sun60i_ce_algs[i].stat_req, sun60i_ce_algs[i].stat_fb);
			seq_printf(seq, "\tLast fallback is: %s\n",
				   sun60i_ce_algs[i].fbname);
			seq_printf(seq, "\tFallback due to 0 length: %lu\n",
				   sun60i_ce_algs[i].stat_fb_len0);
			seq_printf(seq, "\tFallback due to length: %lu\n",
				   sun60i_ce_algs[i].stat_fb_srclen);
			seq_printf(seq, "\tFallback due to alignment: %lu\n",
				   sun60i_ce_algs[i].stat_fb_srcali);
			seq_printf(seq, "\tFallback due to SG numbers: %lu\n",
				   sun60i_ce_algs[i].stat_fb_maxsg);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			seq_printf(seq, "%s %s reqs=%lu bytes=%lu\n",
				   sun60i_ce_algs[i].alg.rng.base.cra_driver_name,
				   sun60i_ce_algs[i].alg.rng.base.cra_name,
				   sun60i_ce_algs[i].stat_req, sun60i_ce_algs[i].stat_bytes);
			break;
		}
	}
#if defined(CONFIG_CRYPTO_DEV_SUN60I_CE_TRNG) && \
	defined(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG)
	seq_printf(seq, "HWRNG %lu %lu\n",
		   ce->hwrng_stat_req, ce->hwrng_stat_bytes);
#endif
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sun60i_ce_debugfs);

static void sun60i_ce_free_chanlist(struct sun60i_ce_dev *ce, int i)
{
	while (i >= 0) {
		crypto_engine_exit(ce->chanlist[i].engine);
		if (ce->chanlist[i].tl)
			dma_free_coherent(ce->dev, sizeof(union ce_task),
					  ce->chanlist[i].tl,
					  ce->chanlist[i].t_phy);
		i--;
	}
}

/*
 * Allocate the channel list structure
 */
static int sun60i_ce_allocate_chanlist(struct sun60i_ce_dev *ce)
{
	int i, err;

	ce->chanlist = devm_kcalloc(ce->dev, MAXFLOW,
				    sizeof(struct sun60i_ce_flow), GFP_KERNEL);
	if (!ce->chanlist)
		return -ENOMEM;

	for (i = 0; i < MAXFLOW; i++) {
		init_completion(&ce->chanlist[i].complete);

		ce->chanlist[i].engine = crypto_engine_alloc_init(ce->dev, true);
		if (!ce->chanlist[i].engine) {
			dev_err(ce->dev, "Cannot allocate engine\n");
			i--;
			err = -ENOMEM;
			goto error_engine;
		}
		err = crypto_engine_start(ce->chanlist[i].engine);
		if (err) {
			dev_err(ce->dev, "Cannot start engine\n");
			goto error_engine;
		}
		ce->chanlist[i].tl = dma_alloc_coherent(ce->dev,
							sizeof(union ce_task),
							&ce->chanlist[i].t_phy,
							GFP_KERNEL);
		if (!ce->chanlist[i].tl) {
			dev_err(ce->dev, "Cannot get DMA memory for task %d\n",
				i);
			err = -ENOMEM;
			goto error_engine;
		}
	}
	return 0;
error_engine:
	sun60i_ce_free_chanlist(ce, i);
	return err;
}

/*
 * Power management strategy: The device is suspended unless a TFM exists for
 * one of the algorithms proposed by this driver.
 */
static int sun60i_ce_pm_suspend(struct device *dev)
{
	struct sun60i_ce_dev *ce = dev_get_drvdata(dev);
	int i;

	reset_control_assert(ce->reset);
	for (i = 0; i < CE_MAX_CLOCKS; i++)
		clk_disable_unprepare(ce->ceclks[i]);
	return 0;
}

static int sun60i_ce_pm_resume(struct device *dev)
{
	struct sun60i_ce_dev *ce = dev_get_drvdata(dev);
	int err, i;

	for (i = 0; i < CE_MAX_CLOCKS; i++) {
		if (!ce->variant->ce_clks[i].name)
			continue;
		err = clk_prepare_enable(ce->ceclks[i]);
		if (err) {
			dev_err(ce->dev, "Cannot prepare_enable %s\n",
				ce->variant->ce_clks[i].name);
			goto error;
		}
	}
	err = reset_control_deassert(ce->reset);
	if (err) {
		dev_err(ce->dev, "Cannot deassert reset control\n");
		goto error;
	}
	return 0;
error:
	while (--i >= 0)
		clk_disable_unprepare(ce->ceclks[i]);
	return err;
}

static const struct dev_pm_ops sun60i_ce_pm_ops = {
	SET_RUNTIME_PM_OPS(sun60i_ce_pm_suspend, sun60i_ce_pm_resume, NULL)
};

static int sun60i_ce_pm_init(struct sun60i_ce_dev *ce)
{
	int err;

	pm_runtime_use_autosuspend(ce->dev);
	pm_runtime_set_autosuspend_delay(ce->dev, 2000);

	err = pm_runtime_set_suspended(ce->dev);
	if (err)
		return err;

	err = devm_pm_runtime_enable(ce->dev);
	if (err)
		return err;

	return 0;
}

static int sun60i_ce_get_clks(struct sun60i_ce_dev *ce)
{
	unsigned long cr;
	int err, i;

	for (i = 0; i < CE_MAX_CLOCKS; i++) {
		if (!ce->variant->ce_clks[i].name)
			continue;
		ce->ceclks[i] = devm_clk_get(ce->dev, ce->variant->ce_clks[i].name);
		if (IS_ERR(ce->ceclks[i])) {
			err = PTR_ERR(ce->ceclks[i]);
			dev_err(ce->dev, "Cannot get %s CE clock err=%d\n",
				ce->variant->ce_clks[i].name, err);
			return err;
		}
		cr = clk_get_rate(ce->ceclks[i]);
		if (!cr)
			return -EINVAL;
		if (ce->variant->ce_clks[i].freq > 0 &&
		    cr != ce->variant->ce_clks[i].freq) {
			dev_dbg(ce->dev, "Set %s clock to %lu (%lu MHz) from %lu (%lu MHz)\n",
				 ce->variant->ce_clks[i].name,
				 ce->variant->ce_clks[i].freq,
				 ce->variant->ce_clks[i].freq / 1000000,
				 cr, cr / 1000000);
			err = clk_set_rate(ce->ceclks[i], ce->variant->ce_clks[i].freq);
			if (err)
				return dev_err_probe(ce->dev, err,
						     "Failed to set %s clock to %lu Hz\n",
						     ce->variant->ce_clks[i].name,
						     ce->variant->ce_clks[i].freq);
			cr = clk_get_rate(ce->ceclks[i]);
		}
		if (ce->variant->ce_clks[i].max_freq > 0 &&
		    cr > ce->variant->ce_clks[i].max_freq)
			dev_warn(ce->dev, "Clock %s rate %lu Hz exceeds recommended %lu Hz\n",
				 ce->variant->ce_clks[i].name, cr,
				 ce->variant->ce_clks[i].max_freq);
	}
	return 0;
}

static int sun60i_ce_register_algs(struct sun60i_ce_dev *ce)
{
	int ce_method, err, id;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun60i_ce_algs); i++) {
		sun60i_ce_algs[i].ce = ce;
		switch (sun60i_ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			id = sun60i_ce_algs[i].ce_algo_id;
			ce_method = ce->variant->alg_cipher[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_dbg(ce->dev,
					"Algo of %s not supported\n",
					sun60i_ce_algs[i].alg.skcipher.base.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
				break;
			}
			id = sun60i_ce_algs[i].ce_blockmode;
			ce_method = ce->variant->op_mode[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_dbg(ce->dev, "Blockmode of %s not supported\n",
					sun60i_ce_algs[i].alg.skcipher.base.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
				break;
			}
			dev_dbg(ce->dev, "Register %s\n",
				 sun60i_ce_algs[i].alg.skcipher.base.base.cra_name);
			err = crypto_engine_register_skcipher(&sun60i_ce_algs[i].alg.skcipher);
			if (err) {
				dev_err(ce->dev, "Fail to register %s\n",
					sun60i_ce_algs[i].alg.skcipher.base.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
				return err;
			}
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			id = sun60i_ce_algs[i].ce_algo_id;
			ce_method = ce->variant->alg_hash[id];
			if (ce_method == CE_ID_NOTSUPP) {
				dev_dbg(ce->dev,
					 "Algo of %s not supported\n",
					 sun60i_ce_algs[i].alg.hash.base.halg.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
				break;
			}
			dev_dbg(ce->dev, "Register %s\n",
				 sun60i_ce_algs[i].alg.hash.base.halg.base.cra_name);
			err = crypto_engine_register_ahash(&sun60i_ce_algs[i].alg.hash);
			if (err) {
				dev_err(ce->dev, "Fail to register %s\n",
					sun60i_ce_algs[i].alg.hash.base.halg.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
				return err;
			}
			break;
		case CRYPTO_ALG_TYPE_RNG:
			dev_dbg(ce->dev, "Register %s\n",
				 sun60i_ce_algs[i].alg.rng.base.cra_name);
			err = crypto_register_rng(&sun60i_ce_algs[i].alg.rng);
			if (err) {
				dev_err(ce->dev, "Fail to register %s\n",
					sun60i_ce_algs[i].alg.rng.base.cra_name);
				sun60i_ce_algs[i].ce = NULL;
			}
			break;
		default:
			sun60i_ce_algs[i].ce = NULL;
			dev_err(ce->dev, "tried to register an unknown algo\n");
		}
	}
	return 0;
}

static void sun60i_ce_unregister_algs(struct sun60i_ce_dev *ce)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun60i_ce_algs); i++) {
		if (!sun60i_ce_algs[i].ce)
			continue;
		switch (sun60i_ce_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			dev_dbg(ce->dev, "Unregister %d %s\n", i,
				 sun60i_ce_algs[i].alg.skcipher.base.base.cra_name);
			crypto_engine_unregister_skcipher(&sun60i_ce_algs[i].alg.skcipher);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			dev_dbg(ce->dev, "Unregister %d %s\n", i,
				 sun60i_ce_algs[i].alg.hash.base.halg.base.cra_name);
			crypto_engine_unregister_ahash(&sun60i_ce_algs[i].alg.hash);
			break;
		case CRYPTO_ALG_TYPE_RNG:
			dev_dbg(ce->dev, "Unregister %d %s\n", i,
				 sun60i_ce_algs[i].alg.rng.base.cra_name);
			crypto_unregister_rng(&sun60i_ce_algs[i].alg.rng);
			break;
		}
	}
}

static int sun60i_ce_probe(struct platform_device *pdev)
{
	struct sun60i_ce_dev *ce;
	int err, irq;

	ce = devm_kzalloc(&pdev->dev, sizeof(*ce), GFP_KERNEL);
	if (!ce)
		return -ENOMEM;

	ce->dev = &pdev->dev;
	platform_set_drvdata(pdev, ce);

	ce->variant = of_device_get_match_data(&pdev->dev);
	if (!ce->variant) {
		dev_err(&pdev->dev, "Missing Crypto Engine variant\n");
		return -EINVAL;
	}

	ce->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ce->base))
		return PTR_ERR(ce->base);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "failed to set 40-bit DMA mask\n");

	err = sun60i_ce_get_clks(ce);
	if (err)
		return err;

	/* Get Non Secure IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ce->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(ce->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(ce->reset),
				     "No reset control found\n");

	mutex_init(&ce->mlock);
	mutex_init(&ce->rnglock);

	err = sun60i_ce_allocate_chanlist(ce);
	if (err)
		return err;

	err = sun60i_ce_pm_init(ce);
	if (err)
		goto error_pm;

	err = devm_request_irq(&pdev->dev, irq, sun60i_ce_irq_handler, 0,
			       "sun60i-ce-ns", ce);
	if (err) {
		dev_err(ce->dev, "Cannot request CryptoEngine Non-secure IRQ (err=%d)\n", err);
		goto error_pm;
	}

	err = sun60i_ce_register_algs(ce);
	if (err)
		goto error_alg;

	err = pm_runtime_resume_and_get(ce->dev);
	if (err < 0)
		goto error_alg;

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_TRNG
	err = sun60i_ce_hwrng_register(ce);
	if (err) {
		pm_runtime_put_sync(ce->dev);
		goto error_alg;
	}
#endif

	pm_runtime_put_sync(ce->dev);

	if (IS_ENABLED(CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG)) {
		struct dentry *dbgfs_dir;
		struct dentry *dbgfs_stats __maybe_unused;

		/* Ignore error of debugfs */
		dbgfs_dir = debugfs_create_dir("sun60i-ce", NULL);
		dbgfs_stats = debugfs_create_file("stats", 0444,
						  dbgfs_dir, ce,
						  &sun60i_ce_debugfs_fops);

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
		ce->dbgfs_dir = dbgfs_dir;
		ce->dbgfs_stats = dbgfs_stats;
#endif
	}

	return 0;
error_alg:
	sun60i_ce_unregister_algs(ce);
error_pm:
	sun60i_ce_free_chanlist(ce, MAXFLOW - 1);
	return err;
}

static void sun60i_ce_remove(struct platform_device *pdev)
{
	struct sun60i_ce_dev *ce = platform_get_drvdata(pdev);

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_TRNG
	sun60i_ce_hwrng_unregister(ce);
#endif

	sun60i_ce_unregister_algs(ce);

#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	debugfs_remove_recursive(ce->dbgfs_dir);
#endif

	sun60i_ce_free_chanlist(ce, MAXFLOW - 1);
}

static const struct of_device_id sun60i_ce_crypto_of_match_table[] = {
	{ .compatible = "allwinner,sun60i-a733-crypto",
	  .data = &sun60i_a733_ce_variant },
	{}
};
MODULE_DEVICE_TABLE(of, sun60i_ce_crypto_of_match_table);

static struct platform_driver sun60i_ce_driver = {
	.probe		 = sun60i_ce_probe,
	.remove		 = sun60i_ce_remove,
	.driver		 = {
		.name		= "sun60i-ce",
		.pm		= &sun60i_ce_pm_ops,
		.of_match_table	= sun60i_ce_crypto_of_match_table,
	},
};

module_platform_driver(sun60i_ce_driver);

MODULE_DESCRIPTION("Allwinner A733 Crypto Engine driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Junqiang Wang <wangjunqiang.geass@gmail.com>");
