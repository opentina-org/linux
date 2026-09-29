/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Allwinner A733 Crypto Engine driver
 *
 * Copyright (C) 2016-2019 Corentin LABBE <clabbe.montjoie@gmail.com>
 * Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved.
 */
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/engine.h>
#include <crypto/skcipher.h>
#include <linux/atomic.h>
#include <linux/build_bug.h>
#include <linux/debugfs.h>
#include <linux/crypto.h>
#include <linux/hw_random.h>
#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <crypto/rng.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>

/* Non-secure register bank. */
#define CE_TDA_LO		0x00
#define CE_TDA_HI		0x04
#define CE_ICR			0x08
#define CE_ISR			0x0c
#define CE_TLR			0x10
#define CE_TSR			0x14
#define CE_ESR			0x18
#define CE_LPC			0xd0

#define CE_TDA_HI_MASK		GENMASK(7, 0)
#define CE_ISR_CHAN_MASK(flow)	(0x3 << ((flow) * 2))
#define CE_ISR_CHAN_STATUS(v, flow) (((v) >> ((flow) * 2)) & 0x3)
#define CE_ISR_DONE		0x1
#define CE_ISR_ERROR		0x2

#define CE_TLR_SYMM		BIT(0)
#define CE_TLR_ASYMM		BIT(5)
#define CE_TLR_HASH_RBG		BIT(10)
#define CE_TLR_RAES		BIT(15)

#define CE_ERR_ALGO_NOTSUP	0x01
#define CE_ERR_KEYSRAM		0x11
#define CE_ERR_KEYLADDER	0x21
#define CE_ERR_DATALEN		0x31

/* Symmetric task common control word. */
#define CE_ENCRYPTION		0
#define CE_DECRYPTION		BIT(8)
#define CE_COMM_INT		BIT(31)

#define CE_AES_128BITS		0
#define CE_AES_192BITS		1
#define CE_AES_256BITS		2

#define CE_OP_ECB		0
#define CE_OP_CBC		BIT(8)

#define CE_ALG_AES		0
#define CE_ALG_DES		1
#define CE_ALG_3DES		2

/* HASH/RBG descriptor control words. */
#define CE_HASH_CTRL_LAST	BIT(12)
#define CE_HASH_CTRL_INT	BIT(16)
#define CE_HASH_CMD_PRNG	BIT(8)
#define CE_HASH_CMD_TRNG	BIT(9)
#define CE_HASH_PRNG_RELOAD	BIT(31)

/* Used in ce_variant */
#define CE_ID_NOTSUPP		0xFF

#define CE_ID_CIPHER_AES	0
#define CE_ID_CIPHER_DES	1
#define CE_ID_CIPHER_DES3	2
#define CE_ID_CIPHER_MAX	3

#define CE_ID_HASH_MD5		0
#define CE_ID_HASH_SHA1		1
#define CE_ID_HASH_SHA224	2
#define CE_ID_HASH_SHA256	3
#define CE_ID_HASH_SHA384	4
#define CE_ID_HASH_SHA512	5
#define CE_ID_HASH_MAX		6

#define CE_ID_OP_ECB	0
#define CE_ID_OP_CBC	1
#define CE_ID_OP_MAX	2

#define PRNG_DATA_SIZE (160 / 8)
#define PRNG_SEED_SIZE DIV_ROUND_UP(175, 8)

#define MAX_SG 8
#define CE_MAX_CLOCKS 5
#define CE_DMA_TIMEOUT_MS	3000
#define MAXFLOW 4

#define CE_MAX_HASH_DIGEST_SIZE		SHA512_DIGEST_SIZE

/*
 * struct ce_clock - Describe clocks used by sun60i-ce
 * @name:	Name of clock needed by this variant
 * @freq:	Frequency to set for each clock
 * @max_freq:	Maximum frequency for each clock (generally given by datasheet)
 */
struct ce_clock {
	const char *name;
	unsigned long freq;
	unsigned long max_freq;
};

/*
 * struct ce_variant - SoC capabilities and clock requirements
 */
struct ce_variant {
	u8 alg_cipher[CE_ID_CIPHER_MAX];
	u8 alg_hash[CE_ID_HASH_MAX];
	u32 op_mode[CE_ID_OP_MAX];
	struct ce_clock ce_clks[CE_MAX_CLOCKS];
};

struct ce_scatter {
	u8 src_addr[5];
	u8 dst_addr[5];
	u8 reserved[2];
	__le32 src_len;
	__le32 dst_len;
} __packed;

struct ce_cipher_task {
	__le32 t_id;
	__le32 t_common_ctl;
	__le32 t_sym_ctl;
	__le32 t_asym_ctl;
	u8 t_key[5];
	u8 t_iv[5];
	u8 t_ctr[5];
	u8 reserved0;
	__le32 t_dlen;
	struct ce_scatter sg[MAX_SG];
	u8 next_sg[5];
	u8 next_task[5];
	u8 reserved1[2];
	__le32 reserved[3];
} __packed;

struct ce_hash_task {
	__le32 t_common_ctl;
	__le32 t_cmd;
	u8 t_dlen[5];
	u8 t_key[5];
	u8 t_iv[5];
	u8 reserved0;
	struct ce_scatter sg[MAX_SG];
	u8 next_sg[5];
	u8 next_task[5];
	u8 total_len_111_96[2];
	__le32 total_len_95_64;
	__le32 total_len_63_32;
	__le32 total_len_31_0;
} __packed;

union ce_task {
	struct ce_cipher_task cipher;
	struct ce_hash_task hash;
} __aligned(8);

static_assert(sizeof(struct ce_scatter) == 20);
static_assert(sizeof(struct ce_cipher_task) == 220);
static_assert(sizeof(struct ce_hash_task) == 208);

static inline void ce_set_addr(u8 dst[5], dma_addr_t addr)
{
	dst[0] = addr;
	dst[1] = addr >> 8;
	dst[2] = addr >> 16;
	dst[3] = addr >> 24;
	dst[4] = addr >> 32;
}

static inline void ce_set_dlen(u8 dst[5], size_t len)
{
	dst[0] = len;
	dst[1] = len >> 8;
	dst[2] = len >> 16;
	dst[3] = len >> 24;
	dst[4] = 0;
}

/*
 * struct sun60i_ce_flow - Information used by each flow
 * @engine:	ptr to the crypto_engine for this flow
 * @complete:	completion for the current task on this flow
 * @status:	set to 1 by interrupt if task is done
 * @t_phy:	Physical address of task
 * @tl:		pointer to the current ce_task for this flow
 * @stat_req:	number of request done by this flow
 */
struct sun60i_ce_flow {
	struct crypto_engine *engine;
	struct completion complete;
	int status;
	dma_addr_t t_phy;
	union ce_task *tl;
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	unsigned long stat_req;
#endif
};

/*
 * struct sun60i_ce_dev - main container for all this driver information
 * @base:	base address of CE
 * @ceclks:	clocks used by CE
 * @reset:	pointer to reset controller
 * @dev:	the platform device
 * @mlock:	Control access to device registers
 * @rnglock:	Control access to the RNG (dedicated channel 3)
 * @chanlist:	array of all flow
 * @flow:	flow to use in next request
 * @variant:	pointer to variant specific data
 * @dbgfs_dir:	Debugfs dentry for statistic directory
 * @dbgfs_stats: Debugfs dentry for statistic counters
 */
struct sun60i_ce_dev {
	void __iomem *base;
	struct clk *ceclks[CE_MAX_CLOCKS];
	struct reset_control *reset;
	struct device *dev;
	struct mutex mlock;
	struct mutex rnglock;
	struct sun60i_ce_flow *chanlist;
	atomic_t flow;
	const struct ce_variant *variant;
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	struct dentry *dbgfs_dir;
	struct dentry *dbgfs_stats;
#endif
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_TRNG
	struct hwrng trng;
#ifdef CONFIG_CRYPTO_DEV_SUN60I_CE_DEBUG
	unsigned long hwrng_stat_req;
	unsigned long hwrng_stat_bytes;
#endif
#endif
};

/*
 * struct sun60i_cipher_req_ctx - context for a skcipher request
 * @op_dir:		direction (encrypt vs decrypt) for this request
 * @flow:		the flow to use for this request
 * @nr_sgs:		The number of source SG (as given by dma_map_sg())
 * @nr_sgd:		The number of destination SG (as given by dma_map_sg())
 * @addr_iv:		The IV addr returned by dma_map_single, need to unmap later
 * @addr_key:		The key addr returned by dma_map_single, need to unmap later
 * @bounce_iv:		Current IV buffer
 * @backup_iv:		Next IV buffer
 * @fallback_req:	request struct for invoking the fallback skcipher TFM
 */
struct sun60i_cipher_req_ctx {
	u32 op_dir;
	int flow;
	int nr_sgs;
	int nr_sgd;
	dma_addr_t addr_iv;
	dma_addr_t addr_key;
	u8 bounce_iv[AES_BLOCK_SIZE] __aligned(sizeof(u32));
	u8 backup_iv[AES_BLOCK_SIZE];
	struct skcipher_request fallback_req;   // keep at the end
};

/*
 * struct sun60i_cipher_tfm_ctx - context for a skcipher TFM
 * @key:		pointer to key data
 * @keylen:		len of the key
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun60i_cipher_tfm_ctx {
	u32 *key;
	u32 keylen;
	struct sun60i_ce_dev *ce;
	struct crypto_skcipher *fallback_tfm;
};

/*
 * struct sun60i_ce_hash_tfm_ctx - context for an ahash TFM
 * @ce:			pointer to the private data of driver handling this TFM
 * @fallback_tfm:	pointer to the fallback TFM
 */
struct sun60i_ce_hash_tfm_ctx {
	struct sun60i_ce_dev *ce;
	struct crypto_ahash *fallback_tfm;
};

/*
 * struct sun60i_ce_hash_reqctx - context for an ahash request
 * @fallback_req:	pre-allocated fallback request
 * @flow:	the flow to use for this request
 * @nr_sgs: number of entries in the source scatterlist
 * @result_len: result length in bytes
 * @addr_res: DMA address of the result buffer, returned by dma_map_single()
 * @result: per-request result buffer
 */
struct sun60i_ce_hash_reqctx {
	int flow;
	int nr_sgs;
	size_t result_len;
	dma_addr_t addr_res;
	u8 result[CE_MAX_HASH_DIGEST_SIZE] __aligned(CRYPTO_DMA_ALIGN);
	struct ahash_request fallback_req; // keep at the end
};

/*
 * struct sun60i_ce_prng_ctx - context for PRNG TFM
 * @seed:	The seed to use
 * @slen:	The size of the seed
 */
struct sun60i_ce_rng_tfm_ctx {
	void *seed;
	unsigned int slen;
};

/*
 * struct sun60i_ce_alg_template - crypto_alg template
 * @type:		the CRYPTO_ALG_TYPE for this template
 * @ce_algo_id:		the CE_ID for this template
 * @ce_blockmode:	the type of block operation CE_ID
 * @ce:			pointer to the sun60i_ce_dev structure associated with
 *			this template
 * @alg:		one of sub struct must be used
 * @stat_req:		number of request done on this template
 * @stat_fb:		number of request which has fallbacked
 * @stat_bytes:		total data size done by this template
 */
struct sun60i_ce_alg_template {
	u32 type;
	u32 ce_algo_id;
	u32 ce_blockmode;
	struct sun60i_ce_dev *ce;
	union {
		struct skcipher_engine_alg skcipher;
		struct ahash_engine_alg hash;
		struct rng_alg rng;
	} alg;
	unsigned long stat_req;
	unsigned long stat_fb;
	unsigned long stat_bytes;
	unsigned long stat_fb_maxsg;
	unsigned long stat_fb_leniv;
	unsigned long stat_fb_len0;
	unsigned long stat_fb_mod16;
	unsigned long stat_fb_srcali;
	unsigned long stat_fb_srclen;
	unsigned long stat_fb_dstali;
	unsigned long stat_fb_dstlen;
	char fbname[CRYPTO_MAX_ALG_NAME];
};

int sun60i_ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
			unsigned int keylen);
int sun60i_ce_des3_setkey(struct crypto_skcipher *tfm, const u8 *key,
			 unsigned int keylen);
int sun60i_ce_cipher_init(struct crypto_tfm *tfm);
void sun60i_ce_cipher_exit(struct crypto_tfm *tfm);
int sun60i_ce_cipher_do_one(struct crypto_engine *engine, void *areq);
int sun60i_ce_skdecrypt(struct skcipher_request *areq);
int sun60i_ce_skencrypt(struct skcipher_request *areq);

int sun60i_ce_get_engine_number(struct sun60i_ce_dev *ce);

int sun60i_ce_run_task(struct sun60i_ce_dev *ce, int flow, u32 load,
			      const char *name);

int sun60i_ce_hash_init_tfm(struct crypto_ahash *tfm);
void sun60i_ce_hash_exit_tfm(struct crypto_ahash *tfm);
int sun60i_ce_hash_init(struct ahash_request *areq);
int sun60i_ce_hash_export(struct ahash_request *areq, void *out);
int sun60i_ce_hash_import(struct ahash_request *areq, const void *in);
int sun60i_ce_hash_final(struct ahash_request *areq);
int sun60i_ce_hash_update(struct ahash_request *areq);
int sun60i_ce_hash_finup(struct ahash_request *areq);
int sun60i_ce_hash_digest(struct ahash_request *areq);
int sun60i_ce_hash_run(struct crypto_engine *engine, void *breq);

int sun60i_ce_prng_generate(struct crypto_rng *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int dlen);
int sun60i_ce_prng_seed(struct crypto_rng *tfm, const u8 *seed, unsigned int slen);
void sun60i_ce_prng_exit(struct crypto_tfm *tfm);
int sun60i_ce_prng_init(struct crypto_tfm *tfm);

int sun60i_ce_hwrng_register(struct sun60i_ce_dev *ce);
void sun60i_ce_hwrng_unregister(struct sun60i_ce_dev *ce);
