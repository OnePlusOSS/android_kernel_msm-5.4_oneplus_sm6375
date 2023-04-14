// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2018-2022 Oplus. All rights reserved.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/vmalloc.h>
#include <linux/nanshan.h>
#include <asm/neon.h>
#include <crypto/internal/scompress.h>

/* #define memcpy __builtin_memcpy */
#define __improbable(x) __builtin_expect(!!(x), 0)

size_t
nanshan_raw_decode_buffer(uint8_t * __restrict dst_buffer, size_t dst_size,
    const uint8_t * __restrict src_buffer, size_t src_size,
    void * __restrict work __attribute__((unused)))
{
	const uint8_t * src;
	uint8_t * dst;
	src = src_buffer;
	dst = dst_buffer;

/* Go fast if we can, keeping away from the end of buffers */

	kernel_neon_begin();
	if (nanshan_decode_asm(&dst, dst_buffer, dst_buffer + dst_size, &src, src_buffer + src_size)) {
		kernel_neon_end();
		return 0;
	}
	kernel_neon_end();
	return (size_t)(dst - dst_buffer);
}

size_t
nanshan_raw_encode_buffer(uint8_t * __restrict dst_buffer, size_t dst_size,
    const uint8_t * __restrict src_buffer, size_t src_size,
    nanshan_hash_entry_t hash_table[NAN_SHAN_COMPRESS_HASH_ENTRIES])
{
	/* Initialize hash table */
	const nanshan_hash_entry_t HASH_FILL = { .offset = 0x80000000, .word = 0x0 };
	const uint8_t * src = src_buffer;
	uint8_t * dst = dst_buffer;
	/* We need several blocks because our base function is limited to 2GB input */
	const size_t BLOCK_SIZE = 0x7ffff000;
	int i;
	while (src_size > 0) {
		/* Bytes to encode in this block */
		const size_t src_to_encode = src_size > BLOCK_SIZE ? BLOCK_SIZE : src_size;
		/* Run the encoder, only the last block emits final literals. Allows concatenation of encoded payloads.
		 * Blocks are encoded independently, so src_begin is set to each block origin instead of src_buffer
		 */
		uint8_t * dst_start = dst;
		const uint8_t * src_start = src;
		size_t dst_used;
		size_t src_used;
		for (i = 0; i < NAN_SHAN_COMPRESS_HASH_ENTRIES;) {
			hash_table[i++] = HASH_FILL;
			hash_table[i++] = HASH_FILL;
			hash_table[i++] = HASH_FILL;
			hash_table[i++] = HASH_FILL;
		}
		kernel_neon_begin();
		nanshan_encode_2gb(&dst, dst_size, &src, src, src_to_encode, hash_table, src_to_encode < src_size);
		kernel_neon_end();
		/* Check progress */
		dst_used = dst - dst_start;
		src_used = src - src_start;
		if (src_to_encode == src_size && src_used < src_to_encode) {
			return 0;
		}
		/* Note that there is a potential problem here in case of non compressible data requiring more blocks.
		 * We may end up here with src_used very small, or even 0, and will not be able to make progress during
		 * compression. We FAIL unless the length of literals remaining at the end is small enough.
		 */
		if (src_to_encode < src_size && src_to_encode - src_used >= (1 << 16)) {
			return 0;
		}
		/* Update counters (SRC and DST already have been updated) */
		src_size -= src_used;
		dst_size -= dst_used;
	}

	return (size_t)(dst - dst_buffer);
}

typedef uint32_t nanshan_uint128 __attribute__((ext_vector_type(4))) __attribute__((__aligned__(1)));

int
nanshan_decode(uint8_t ** dst_ptr,
    uint8_t * dst_begin,
    uint8_t * dst_end,
    const uint8_t ** src_ptr,
    const uint8_t * src_end)
{
	uint8_t * dst = *dst_ptr;
	const uint8_t * src = *src_ptr;
	uint8_t cmd;
	uint32_t literalLength;
	uint32_t matchLength;
	/* Require dst_end > dst */
	if (dst_end <= dst) {
		goto OUT_FULL;
	}

	while (src < src_end) {
		uint64_t matchDistance;
		uint8_t * ref;
		uint64_t i;
		/* Keep last good position */
		*src_ptr = src;
		*dst_ptr = dst;
		cmd = *src++;
		literalLength = (cmd >> 4) & 15;
		matchLength = 4 + (cmd & 15);
		/* extra bytes for literalLength */
		if (__improbable(literalLength == 15)) {
			uint8_t s;
			do {
#if DEBUG_NAN_SHAN_DECODE_ERRORS
				if (__improbable(src >= src_end)) {
					printf("Truncated SRC literal length\n");
				}
#endif
				if (__improbable(src >= src_end)) {
					goto IN_FAIL;
				}
				s = *src++;
				literalLength += s;
			} while (__improbable(s == 255));
		}

		/* copy literal */
#if DEBUG_NAN_SHAN_DECODE_ERRORS
		if (__improbable(literalLength > (size_t)(src_end - src))) {
			printf("Truncated SRC literal\n");
		}
#endif
		if (__improbable(literalLength > (size_t)(src_end - src))) {
			goto IN_FAIL;
		}
		if (__improbable(literalLength > (size_t)(dst_end - dst))) {
			literalLength = (uint32_t)(dst_end - dst);
			memcpy(dst, src, literalLength);
			dst += literalLength;
			goto OUT_FULL;
		}
		memcpy(dst, src, literalLength);
		src += literalLength;
		dst += literalLength;
		if (__improbable(src >= src_end)) {
			goto OUT_FULL;
		}
#if DEBUG_NAN_SHAN_DECODE_ERRORS
		if (__improbable(2 > (size_t)(src_end - src))) {
			printf("Truncated SRC distance\n");
		}
#endif
		if (__improbable(2 > (size_t)(src_end - src))) {
			goto IN_FAIL;
		}
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wcast-align"
		/* match distance */
		matchDistance = *(const uint16_t *)src;
	#pragma clang diagnostic pop
		src += 2;
#if DEBUG_NAN_SHAN_DECODE_ERRORS
		if (matchDistance == 0) {
			printf("Invalid match distance D = 0\n");
		}
#endif
		if (__improbable(matchDistance == 0)) {
			goto IN_FAIL;
		}

		ref = dst - matchDistance;
#if DEBUG_NAN_SHAN_DECODE_ERRORS
		if (__improbable(ref < dst_begin)) {
			printf("Invalid reference D=0x%llx dst_begin=%p dst=%p dst_end=%p\n", matchDistance, dst_begin, dst, dst_end);
		}
#endif
		if (__improbable(ref < dst_begin)) {
			goto OUT_FAIL;
		}
		/* extra bytes for matchLength */
		if (__improbable(matchLength == 19)) {
			uint8_t s;
			do {
#if DEBUG_NAN_SHAN_DECODE_ERRORS
				if (__improbable(src >= src_end)) {
					printf("Truncated SRC match length\n");
				}
#endif
				if (__improbable(src >= src_end)) {
					goto IN_FAIL;
				}
				s = *src++;
				matchLength += s;
			} while (__improbable(s == 255));
		}
		/* copy match (may overlap) */
		if (__improbable(matchLength > (size_t)(dst_end - dst))) {
			uint32_t i;
			matchLength = (uint32_t)(dst_end - dst);
			for (i = 0; i < matchLength; ++i) {
				dst[i] = ref[i];
			}
			dst += matchLength;
			goto OUT_FULL;
		}
		for (i = 0; i < matchLength; i++) {
			dst[i] = ref[i];
		}
		dst += matchLength;
	}

	/*  We reached the end of the input buffer after a full instruction */
OUT_FULL:
	/*  Or we reached the end of the output buffer */
	*dst_ptr = dst;
	*src_ptr = src;
	return 0;
OUT_FAIL:
IN_FAIL:
	return 1;
}

struct nanshan_ctx {
	void *nanshan_comp_mem;
};

static void *nanshan_alloc_ctx(struct crypto_scomp *tfm)
{
	void *ctx;

	ctx = vmalloc(nanshan_hash_table_size);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	return ctx;
}

static int nanshan_init(struct crypto_tfm *tfm)
{
	struct nanshan_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->nanshan_comp_mem = nanshan_alloc_ctx(NULL);
	if (IS_ERR(ctx->nanshan_comp_mem))
		return -ENOMEM;

	return 0;
}

static void nanshan_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	vfree(ctx);
}

static void nanshan_exit(struct crypto_tfm *tfm)
{
	struct nanshan_ctx *ctx = crypto_tfm_ctx(tfm);

	nanshan_free_ctx(NULL, ctx->nanshan_comp_mem);
}

static int __nanshan_compress_crypto(const u8 *src, unsigned int slen,
				 u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = nanshan_raw_encode_buffer(dst,
		*dlen, src, slen, ctx);

	if (!out_len)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int nanshan_scompress(struct crypto_scomp *tfm, const u8 *src,
			 unsigned int slen, u8 *dst, unsigned int *dlen,
			 void *ctx)
{
	return __nanshan_compress_crypto(src, slen, dst, dlen, ctx);
}

static int nanshan_compress_crypto(struct crypto_tfm *tfm, const u8 *src,
			       unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct nanshan_ctx *ctx = crypto_tfm_ctx(tfm);

	return __nanshan_compress_crypto(src, slen, dst, dlen, ctx->nanshan_comp_mem);
}

static int __nanshan_decompress_crypto(const u8 *src, unsigned int slen,
				   u8 *dst, unsigned int *dlen, void *ctx)
{
	int out_len = nanshan_raw_decode_buffer(dst, *dlen, src, slen, ctx);

	if (out_len < 0)
		return -EINVAL;

	*dlen = out_len;
	return 0;
}

static int nanshan_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			   unsigned int slen, u8 *dst, unsigned int *dlen,
			   void *ctx)
{
	return __nanshan_decompress_crypto(src, slen, dst, dlen, NULL);
}

static int nanshan_decompress_crypto(struct crypto_tfm *tfm, const u8 *src,
				 unsigned int slen, u8 *dst,
				 unsigned int *dlen)
{
	return __nanshan_decompress_crypto(src, slen, dst, dlen, NULL);
}


static struct crypto_alg alg = {
	.cra_name		 = "nanshan",
	.cra_driver_name = "nanshan-generic",
	.cra_flags		 = CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize	 = sizeof(struct nanshan_ctx),
	.cra_module		 = THIS_MODULE,
	.cra_init		 = nanshan_init,
	.cra_exit		 = nanshan_exit,
	.cra_u			 = { .compress = {
	.coa_compress		= nanshan_compress_crypto,
	.coa_decompress		= nanshan_decompress_crypto } }
};

static struct scomp_alg scomp = {
	.alloc_ctx		= nanshan_alloc_ctx,
	.free_ctx		= nanshan_free_ctx,
	.compress		= nanshan_scompress,
	.decompress		= nanshan_sdecompress,
	.base			= {
		.cra_name	= "nanshan",
		.cra_driver_name = "nanshan-scomp",
		.cra_module	 = THIS_MODULE,
	}
};

static int __init nanshan_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;

	ret = crypto_register_scomp(&scomp);
	if (ret) {
		crypto_unregister_alg(&alg);
		return ret;
	}

	return ret;
}

static void __exit nanshan_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomp(&scomp);
}

module_init(nanshan_mod_init);
module_exit(nanshan_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NAN_SHAN Compression Algorithm");
MODULE_ALIAS_CRYPTO("nanshan");
