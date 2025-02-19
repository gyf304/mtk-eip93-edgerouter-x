/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 - 2021
 *
 * Richard van Schagen <vschagen@icloud.com>
 */
#ifndef _EIP93_MAIN_H_
#define _EIP93_MAIN_H_

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
#define aes_expandkey crypto_aes_expand_key
#endif

#define MTK_RING_SIZE			512
#define MTK_RING_BUSY			32
#define MTK_CRA_PRIORITY		1500

/* cipher algorithms */
#define MTK_ALG_DES			BIT(0)
#define MTK_ALG_3DES			BIT(1)
#define MTK_ALG_AES			BIT(2)
#define MTK_ALG_MASK			GENMASK(2, 0)
/* hash and hmac algorithms */
#define MTK_HASH_MD5			BIT(3)
#define MTK_HASH_SHA1			BIT(4)
#define MTK_HASH_SHA224			BIT(5)
#define MTK_HASH_SHA256			BIT(6)
#define MTK_HASH_HMAC			BIT(7)
#define MTK_HASH_MASK			GENMASK(6, 3)
/* cipher modes */
#define MTK_MODE_CBC			BIT(8)
#define MTK_MODE_ECB			BIT(9)
#define MTK_MODE_CTR			BIT(10)
#define MTK_MODE_RFC3686		BIT(11)
#define MTK_MODE_MASK			GENMASK(10, 8)

/* cipher encryption/decryption operations */
#define MTK_ENCRYPT			BIT(12)
#define MTK_DECRYPT			BIT(13)

#define MTK_BUSY			BIT(14)

/* descriptor flags */
#define MTK_DESC_ASYNC			BIT(31)
#define MTK_DESC_SKCIPHER		BIT(30)
#define MTK_DESC_AEAD			BIT(29)
#define MTK_DESC_AHASH			BIT(28)
#define MTK_DESC_PRNG			BIT(27)
#define MTK_DESC_FAKE_HMAC		BIT(26)
#define MTK_DESC_LAST			BIT(25)
#define MTK_DESC_FINISH			BIT(24)
#define MTK_DESC_IPSEC			BIT(23)
#define MTK_DESC_DMA_IV			BIT(22)

#define IS_DES(flags)			(flags & MTK_ALG_DES)
#define IS_3DES(flags)			(flags & MTK_ALG_3DES)
#define IS_AES(flags)			(flags & MTK_ALG_AES)

#define IS_HASH_MD5(flags)		(flags & MTK_HASH_MD5)
#define IS_HASH_SHA1(flags)		(flags & MTK_HASH_SHA1)
#define IS_HASH_SHA224(flags)		(flags & MTK_HASH_SHA224)
#define IS_HASH_SHA256(flags)		(flags & MTK_HASH_SHA256)
#define IS_HMAC(flags)			(flags & MTK_HASH_HMAC)

#define IS_CBC(mode)			(mode & MTK_MODE_CBC)
#define IS_ECB(mode)			(mode & MTK_MODE_ECB)
#define IS_CTR(mode)			(mode & MTK_MODE_CTR)
#define IS_RFC3686(mode)		(mode & MTK_MODE_RFC3686)

#define IS_BUSY(flags)			(flags & MTK_BUSY)
#define IS_DMA_IV(flags)		(flags & MTK_DESC_DMA_IV)

#define IS_ENCRYPT(dir)			(dir & MTK_ENCRYPT)
#define IS_DECRYPT(dir)			(dir & MTK_DECRYPT)

#define IS_CIPHER(flags)		(flags & (MTK_ALG_DES || \
						MTK_ALG_3DES ||  \
						MTK_ALG_AES))

#define IS_HASH(flags)			(flags & (MTK_HASH_MD5 ||  \
						MTK_HASH_SHA1 ||   \
						MTK_HASH_SHA224 || \
						MTK_HASH_SHA256))

/**
 * struct mtk_device - crypto engine device structure
 */
struct mtk_device {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*clk;
	int			irq;
	struct mtk_ring		*ring;
	struct mtk_state_pool	*saState_pool;
};

struct mtk_desc_ring {
	void			*base;
	void			*base_end;
	dma_addr_t		base_dma;
	/* write and read pointers */
	void			*read;
	void			*write;
	/* descriptor element offset */
	u32			offset;
};

struct mtk_state_pool {
	void			*base;
	dma_addr_t		base_dma;
	bool			in_use;
};

struct mtk_ring {
	struct tasklet_struct		done_task;
	/* command/result rings */
	struct mtk_desc_ring		cdr;
	struct mtk_desc_ring		rdr;
	spinlock_t			write_lock;
	spinlock_t			read_lock;
	atomic_t			free;
	/* saState */
	struct mtk_state_pool		*saState_pool;
	void				*saState;
	dma_addr_t			saState_dma;
};

enum mtk_alg_type {
	MTK_ALG_TYPE_AEAD,
	MTK_ALG_TYPE_SKCIPHER,
};

struct mtk_alg_template {
	struct mtk_device	*mtk;
	enum mtk_alg_type	type;
	u32			flags;
	union {
		struct aead_alg		aead;
		struct skcipher_alg	skcipher;
	} alg;
};

#endif /* _EIP93_MAIN_H_ */
