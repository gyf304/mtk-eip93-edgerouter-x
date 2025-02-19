// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 - 2021
 *
 * Richard van Schagen <vschagen@icloud.com>
 */

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "eip93-main.h"
#include "eip93-regs.h"
#include "eip93-common.h"
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_SKCIPHER)
#include "eip93-cipher.h"
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AES)
#include "eip93-aes.h"
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_DES)
#include "eip93-des.h"
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AEAD)
#include "eip93-aead.h"
#endif

static struct mtk_alg_template *mtk_algs[] = {
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_DES)
	&mtk_alg_ecb_des,
	&mtk_alg_cbc_des,
	&mtk_alg_ecb_des3_ede,
	&mtk_alg_cbc_des3_ede,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AES)
	&mtk_alg_ecb_aes,
	&mtk_alg_cbc_aes,
	&mtk_alg_ctr_aes,
	&mtk_alg_rfc3686_aes,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AEAD)
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_DES)
	&mtk_alg_authenc_hmac_md5_cbc_des,
	&mtk_alg_authenc_hmac_sha1_cbc_des,
	&mtk_alg_authenc_hmac_sha224_cbc_des,
	&mtk_alg_authenc_hmac_sha256_cbc_des,
	&mtk_alg_authenc_hmac_md5_cbc_des3_ede,
	&mtk_alg_authenc_hmac_sha1_cbc_des3_ede,
	&mtk_alg_authenc_hmac_sha224_cbc_des3_ede,
	&mtk_alg_authenc_hmac_sha256_cbc_des3_ede,
#endif
	&mtk_alg_authenc_hmac_md5_cbc_aes,
	&mtk_alg_authenc_hmac_sha1_cbc_aes,
	&mtk_alg_authenc_hmac_sha224_cbc_aes,
	&mtk_alg_authenc_hmac_sha256_cbc_aes,
	&mtk_alg_authenc_hmac_md5_rfc3686_aes,
	&mtk_alg_authenc_hmac_sha1_rfc3686_aes,
	&mtk_alg_authenc_hmac_sha224_rfc3686_aes,
	&mtk_alg_authenc_hmac_sha256_rfc3686_aes,
#endif
};

inline void mtk_irq_disable(struct mtk_device *mtk, u32 mask)
{
	__raw_writel(mask, mtk->base + EIP93_REG_MASK_DISABLE);
}

inline void mtk_irq_enable(struct mtk_device *mtk, u32 mask)
{
	__raw_writel(mask, mtk->base + EIP93_REG_MASK_ENABLE);
}

inline void mtk_irq_clear(struct mtk_device *mtk, u32 mask)
{
	__raw_writel(mask, mtk->base + EIP93_REG_INT_CLR);
}

static void mtk_unregister_algs(unsigned int i)
{
	unsigned int j;

	for (j = 0; j < i; j++) {
		switch (mtk_algs[j]->type) {
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_SKCIPHER)
		case MTK_ALG_TYPE_SKCIPHER:
			crypto_unregister_skcipher(&mtk_algs[j]->alg.skcipher);
			break;
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AEAD)
		case MTK_ALG_TYPE_AEAD:
			crypto_unregister_aead(&mtk_algs[j]->alg.aead);
			break;
#endif
		default:
			break;
		}
	}
}

static int mtk_register_algs(struct mtk_device *mtk)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(mtk_algs); i++) {
		mtk_algs[i]->mtk = mtk;

		switch (mtk_algs[i]->type) {
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_SKCIPHER)
		case MTK_ALG_TYPE_SKCIPHER:
			err = crypto_register_skcipher(&mtk_algs[i]->alg.skcipher);
			break;
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AEAD)
		case MTK_ALG_TYPE_AEAD:
			err = crypto_register_aead(&mtk_algs[i]->alg.aead);
			break;
#endif
		default:
			return -EINVAL;
		}
		if (err)
			goto fail;
	}

	return 0;

fail:
	mtk_unregister_algs(i);

	return err;
}

static void mtk_handle_result_descriptor(struct mtk_device *mtk)
{
	struct crypto_async_request *async;
	struct eip93_descriptor_s *rdesc;
	bool last_entry;
	u32 flags;
	int handled, ready, err;
	union peCrtlStat_w done1;
	union peLength_w done2;

get_more:
	handled = 0;

	ready = readl(mtk->base + EIP93_REG_PE_RD_COUNT) & GENMASK(10, 0);

	if (!ready) {
		mtk_irq_clear(mtk, EIP93_INT_PE_RDRTHRESH_REQ);
		mtk_irq_enable(mtk, EIP93_INT_PE_RDRTHRESH_REQ);
		return;
	}

	last_entry = false;

	while (ready) {
		rdesc = mtk_get_descriptor(mtk);
		if (IS_ERR(rdesc)) {
			dev_err(mtk->dev, "Ndesc: %d nreq: %d\n",
				handled, ready);
			err = -EIO;
			break;
		}
		/* make sure DMA is finished writing */
		do {
			done1.word = READ_ONCE(rdesc->peCrtlStat.word);
			done2.word = READ_ONCE(rdesc->peLength.word);
		} while ((!done1.bits.peReady) || (!done2.bits.peReady));

		err = rdesc->peCrtlStat.bits.errStatus;

		flags = rdesc->userId;
		async = (struct crypto_async_request *)rdesc->arc4Addr;

		writel(1, mtk->base + EIP93_REG_PE_RD_COUNT);
		mtk_irq_clear(mtk, EIP93_INT_PE_RDRTHRESH_REQ);

		handled++;
		ready--;

		if (flags & MTK_DESC_LAST) {
			last_entry = true;
			break;
		}
	}

	if (!last_entry)
		goto get_more;
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_SKCIPHER)
	if (flags & MTK_DESC_SKCIPHER)
		mtk_skcipher_handle_result(async, err);
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AEAD)
	if (flags & MTK_DESC_AEAD)
		mtk_aead_handle_result(async, err);
#endif
	goto get_more;
}

static void mtk_done_task(unsigned long data)
{
	struct mtk_device *mtk = (struct mtk_device *)data;

	mtk_handle_result_descriptor(mtk);
}

static irqreturn_t mtk_irq_handler(int irq, void *dev_id)
{
	struct mtk_device *mtk = (struct mtk_device *)dev_id;
	u32 irq_status;

	irq_status = readl(mtk->base + EIP93_REG_INT_MASK_STAT);

	if (irq_status & EIP93_INT_PE_RDRTHRESH_REQ) {
		mtk_irq_disable(mtk, EIP93_INT_PE_RDRTHRESH_REQ);
		tasklet_schedule(&mtk->ring->done_task);
		return IRQ_HANDLED;
	}

	mtk_irq_clear(mtk, irq_status);
	if (irq_status)
		mtk_irq_disable(mtk, irq_status);

	return IRQ_NONE;
}

static void mtk_initialize(struct mtk_device *mtk)
{
	union peConfig_w peConfig;
	union peEndianCfg_w peEndianCfg;
	union peIntCfg_w peIntCfg;
	union peClockCfg_w peClockCfg;
	union peBufThresh_w peBufThresh;
	union peRingThresh_w peRingThresh;

	/* Reset Engine and setup Mode */
	peConfig.word = 0;
	peConfig.bits.resetPE = 1;
	peConfig.bits.resetRing = 1;
	peConfig.bits.peMode = 3;
	peConfig.bits.enCDRupdate = 1;

	writel(peConfig.word, mtk->base + EIP93_REG_PE_CONFIG);

	udelay(10);

	peConfig.bits.resetPE = 0;
	peConfig.bits.resetRing = 0;

	writel(peConfig.word, mtk->base + EIP93_REG_PE_CONFIG);

	/* Initialize the BYTE_ORDER_CFG register */
	peEndianCfg.word = 0;
	writel(peEndianCfg.word, mtk->base + EIP93_REG_PE_ENDIAN_CONFIG);

	/* Initialize the INT_CFG register */
	peIntCfg.word = 0;
	writel(peIntCfg.word, mtk->base + EIP93_REG_INT_CFG);

	/* Config Clocks */
	peClockCfg.word = 0;
	peClockCfg.bits.enPEclk = 1;
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_DES)
	peClockCfg.bits.enDESclk = 1;
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_AES)
	peClockCfg.bits.enAESclk = 1;
#endif
#if IS_ENABLED(CONFIG_CRYPTO_DEV_EIP93_HMAC)
	peClockCfg.bits.enHASHclk = 1;
#endif
	writel(peClockCfg.word, mtk->base + EIP93_REG_PE_CLOCK_CTRL);

	/* Config DMA thresholds */
	peBufThresh.word = 0;
	peBufThresh.bits.inputBuffer  = 128;
	peBufThresh.bits.outputBuffer = 128;

	writel(peBufThresh.word, mtk->base + EIP93_REG_PE_BUF_THRESH);

	/* Clear/ack all interrupts before disable all */
	mtk_irq_clear(mtk, 0xFFFFFFFF);
	mtk_irq_disable(mtk, 0xFFFFFFFF);

	/* Config Ring Threshold */
	peRingThresh.word = 0;
	peRingThresh.bits.CDRThresh = MTK_RING_SIZE - MTK_RING_BUSY;
	peRingThresh.bits.RDRThresh = 0;
	peRingThresh.bits.RDTimeout = 5;
	peRingThresh.bits.enTimeout = 1;

	writel(peRingThresh.word, mtk->base + EIP93_REG_PE_RING_THRESH);
}

static void mtk_desc_free(struct mtk_device *mtk)
{
	writel(0, mtk->base + EIP93_REG_PE_RING_CONFIG);
	writel(0, mtk->base + EIP93_REG_PE_CDR_BASE);
	writel(0, mtk->base + EIP93_REG_PE_RDR_BASE);
}

static int mtk_set_ring(struct mtk_device *mtk, struct mtk_desc_ring *ring,
			int Offset)
{
	ring->offset = Offset;
	ring->base = dmam_alloc_coherent(mtk->dev, Offset * MTK_RING_SIZE,
					&ring->base_dma, GFP_KERNEL);
	if (!ring->base)
		return -ENOMEM;

	ring->write = ring->base;
	ring->base_end = ring->base + Offset * (MTK_RING_SIZE - 1);
	ring->read  = ring->base;

	return 0;
}

static int mtk_desc_init(struct mtk_device *mtk)
{
	struct mtk_state_pool *saState_pool;
	struct mtk_desc_ring *cdr = &mtk->ring->cdr;
	struct mtk_desc_ring *rdr = &mtk->ring->rdr;
	union peRingCfg_w peRingCfg;
	int RingOffset, err, i;

	RingOffset = sizeof(struct eip93_descriptor_s);

	err = mtk_set_ring(mtk, cdr, RingOffset);
	if (err)
		return err;

	err = mtk_set_ring(mtk, rdr, RingOffset);
	if (err)
		return err;

	writel((u32)cdr->base_dma, mtk->base + EIP93_REG_PE_CDR_BASE);
	writel((u32)rdr->base_dma, mtk->base + EIP93_REG_PE_RDR_BASE);

	peRingCfg.word = 0;
	peRingCfg.bits.ringSize = MTK_RING_SIZE - 1;
	peRingCfg.bits.ringOffset =  RingOffset / 4;

	writel(peRingCfg.word, mtk->base + EIP93_REG_PE_RING_CONFIG);

	atomic_set(&mtk->ring->free, MTK_RING_SIZE - 1);
	/* Create State record DMA pool */
	RingOffset = sizeof(struct saState_s);
	mtk->ring->saState = dmam_alloc_coherent(mtk->dev,
					RingOffset * MTK_RING_SIZE,
					&mtk->ring->saState_dma, GFP_KERNEL);
	if (!mtk->ring->saState)
		return -ENOMEM;

	mtk->ring->saState_pool = devm_kcalloc(mtk->dev, 1,
				sizeof(struct mtk_state_pool) * MTK_RING_SIZE,
				GFP_KERNEL);

	for (i = 0; i < MTK_RING_SIZE; i++) {
		saState_pool = &mtk->ring->saState_pool[i];
		saState_pool->base = mtk->ring->saState + (i * RingOffset);
		saState_pool->base_dma = mtk->ring->saState_dma + (i * RingOffset);
		saState_pool->in_use = false;
	}

	return 0;
}

static void mtk_cleanup(struct mtk_device *mtk)
{
	tasklet_kill(&mtk->ring->done_task);

	/* Clear/ack all interrupts before disable all */
	mtk_irq_clear(mtk, 0xFFFFFFFF);
	mtk_irq_disable(mtk, 0xFFFFFFFF);

	writel(0, mtk->base + EIP93_REG_PE_CLOCK_CTRL);

	mtk_desc_free(mtk);
}

static int mtk_crypto_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_device *mtk;
	struct resource *res;
	int err;

	mtk = devm_kzalloc(dev, sizeof(*mtk), GFP_KERNEL);
	if (!mtk)
		return -ENOMEM;

	mtk->dev = dev;
	platform_set_drvdata(pdev, mtk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mtk->base = devm_ioremap_resource(&pdev->dev, res);

	if (IS_ERR(mtk->base))
		return PTR_ERR(mtk->base);

	mtk->irq = platform_get_irq(pdev, 0);

	if (mtk->irq < 0)
		return mtk->irq;

	err = devm_request_threaded_irq(mtk->dev, mtk->irq, mtk_irq_handler,
					NULL, IRQF_ONESHOT,
					dev_name(mtk->dev), mtk);

	mtk->ring = devm_kcalloc(mtk->dev, 1, sizeof(*mtk->ring), GFP_KERNEL);

	if (!mtk->ring)
		return -ENOMEM;

	err = mtk_desc_init(mtk);
	if (err)
		return err;

	tasklet_init(&mtk->ring->done_task, mtk_done_task, (unsigned long)mtk);

	spin_lock_init(&mtk->ring->read_lock);
	spin_lock_init(&mtk->ring->write_lock);

	mtk_initialize(mtk);

	/* Init. finished, enable RDR interupt */
	mtk_irq_enable(mtk, EIP93_INT_PE_RDRTHRESH_REQ);

	err = mtk_register_algs(mtk);
	if (err) {
		mtk_cleanup(mtk);
		return err;
	}

	dev_info(mtk->dev, "EIP93 Crypto Engine Initialized.");

	return 0;
}

static int mtk_crypto_remove(struct platform_device *pdev)
{
	struct mtk_device *mtk = platform_get_drvdata(pdev);

	mtk_unregister_algs(ARRAY_SIZE(mtk_algs));
	mtk_cleanup(mtk);
	dev_info(mtk->dev, "EIP93 removed.\n");

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id mtk_crypto_of_match[] = {
	{ .compatible = "mediatek,mtk-eip93", },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_crypto_of_match);
#endif

static struct platform_driver mtk_crypto_driver = {
	.probe = mtk_crypto_probe,
	.remove = mtk_crypto_remove,
	.driver = {
		.name = "mtk-eip93",
		.of_match_table = of_match_ptr(mtk_crypto_of_match),
	},
};
module_platform_driver(mtk_crypto_driver);

MODULE_AUTHOR("Richard van Schagen <vschagen@cs.com>");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_DESCRIPTION("Mediatek EIP-93 crypto engine driver");
MODULE_LICENSE("GPL v2");
