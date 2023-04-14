// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2018-2022 Oplus. All rights reserved.
*/

#if defined (__cplusplus)
extern "C" {
#endif

#pragma once

#include <linux/types.h>
#include <linux/string.h>


/* Tunables */
#ifndef NAN_SHAN_COMPRESS_HASH_BITS
#define NAN_SHAN_COMPRESS_HASH_BITS 10
#endif

#ifndef NAN_SHAN_COMPRESS_HASH_ENTRIES
#define NAN_SHAN_COMPRESS_HASH_ENTRIES (1 << NAN_SHAN_COMPRESS_HASH_BITS)
#endif

#ifndef NAN_SHAN_COMPRESS_HASH_MULTIPLY
#define NAN_SHAN_COMPRESS_HASH_MULTIPLY 2654435761U
#endif

#ifndef NAN_SHAN_COMPRESS_HASH_SHIFT
#define NAN_SHAN_COMPRESS_HASH_SHIFT (32 - NAN_SHAN_COMPRESS_HASH_BITS)
#endif

#ifndef DEBUG_NAN_SHAN_DECODE_ERRORS
#define DEBUG_NAN_SHAN_DECODE_ERRORS 0
#endif

/* Not tunables */
#ifndef NAN_SHAN_GOFAST_SAFETY_MARGIN
#define NAN_SHAN_GOFAST_SAFETY_MARGIN 128
#endif

#ifndef NAN_SHAN_DISTANCE_BOUND
#define NAN_SHAN_DISTANCE_BOUND 65536
#endif

#pragma mark - Building blocks

/* Represents a position in the input stream */
typedef struct {
	uint32_t offset;
	uint32_t word;
} nanshan_hash_entry_t;
static const size_t nanshan_hash_table_size = NAN_SHAN_COMPRESS_HASH_ENTRIES * sizeof(nanshan_hash_entry_t);

/* Worker function for nanshan encode.  Underlies both the buffer and stream encode operations.
 * Performs nanshan encoding of up to 2gb of data, updates dst_ptr and src_ptr to point to the
 * first byte of output and input that couldn't be completely processed, respectively.
 *
 * If skip_final_literals is 0, the entire src buffer is encoded, by emitting a final sequence of literals
 * at the end of the compressed payload.
 *
 * If skip_final_literals is not 0, this final literal sequence is not emitted, and the src buffer is
 * partially encoded (the length of this literal sequence varies).
 */
void nanshan_encode_2gb(uint8_t **dst_ptr, size_t dst_size,
    const uint8_t **src_ptr, const uint8_t *src_begin, size_t src_size,
    nanshan_hash_entry_t hash_table[NAN_SHAN_COMPRESS_HASH_ENTRIES], int skip_final_literals);

extern int nanshan_decode(uint8_t **dst_ptr, uint8_t *dst_begin, uint8_t *dst_end,
    const uint8_t **src_ptr, const uint8_t *src_end);

int nanshan_decode_asm(uint8_t **dst_ptr, uint8_t *dst_begin, uint8_t *dst_end,
    const uint8_t **src_ptr, const uint8_t *src_end);

#pragma mark - Buffer interfaces

static const size_t nanshan_encode_scratch_size = nanshan_hash_table_size;
static const size_t nanshan_decode_scratch_size = 0;

#pragma mark - Buffer interfaces(NAN_SHAN RAW)

size_t nanshan_raw_encode_buffer(uint8_t * __restrict dst_buffer, size_t dst_size,
    const uint8_t * __restrict src_buffer, size_t src_size,
    nanshan_hash_entry_t hash_table[NAN_SHAN_COMPRESS_HASH_ENTRIES]);

size_t nanshan_raw_decode_buffer(uint8_t * __restrict dst_buffer, size_t dst_size,
    const uint8_t * __restrict src_buffer, size_t src_size,
    void * __restrict work __attribute__((unused)));

typedef __attribute__((__ext_vector_type__(8))) uint8_t vector_uchar8;
typedef __attribute__((__ext_vector_type__(16))) uint8_t vector_uchar16;
typedef __attribute__((__ext_vector_type__(32))) uint8_t vector_uchar32;
typedef __attribute__((__ext_vector_type__(64))) uint8_t vector_uchar64;
typedef __attribute__((__ext_vector_type__(16), __aligned__(1))) uint8_t packed_uchar16;
typedef __attribute__((__ext_vector_type__(32), __aligned__(1))) uint8_t packed_uchar32;
typedef __attribute__((__ext_vector_type__(64), __aligned__(1))) uint8_t packed_uchar64;

typedef __attribute__((__ext_vector_type__(4))) uint16_t vector_ushort4;
typedef __attribute__((__ext_vector_type__(4), __aligned__(2))) uint16_t packed_ushort4;

typedef __attribute__((__ext_vector_type__(2))) int32_t vector_int2;
typedef __attribute__((__ext_vector_type__(4))) int32_t vector_int4;
typedef __attribute__((__ext_vector_type__(8))) int32_t vector_int8;

typedef __attribute__((__ext_vector_type__(4))) uint32_t vector_uint4;

#define UTIL_FUNCTION static inline __attribute__((__always_inline__)) __attribute__((__overloadable__))

/* Load N bytes from unaligned location PTR */
UTIL_FUNCTION uint16_t
load2(const void * ptr)
{
	uint16_t data;
	memcpy(&data, ptr, sizeof(data));
	return data;
}
UTIL_FUNCTION uint32_t
load4(const void * ptr)
{
	uint32_t data;
	memcpy(&data, ptr, sizeof(data));
	return data;
}
UTIL_FUNCTION uint64_t
load8(const void * ptr)
{
	uint64_t data;
	memcpy(&data, ptr, sizeof(data));
	return data;
}
UTIL_FUNCTION vector_uchar16
load16(const void * ptr)
{
	return (const vector_uchar16)*(const packed_uchar16 *)ptr;
}
UTIL_FUNCTION vector_uchar32
load32(const void * ptr)
{
	return (const vector_uchar32)*(const packed_uchar32 *)ptr;
}
UTIL_FUNCTION vector_uchar64
load64(const void * ptr)
{
	return (const vector_uchar64)*(const packed_uchar64 *)ptr;
}

/* Store N bytes to unaligned location PTR */
UTIL_FUNCTION void
store2(void * ptr, uint16_t data)
{
	memcpy(ptr, &data, sizeof(data));
}
UTIL_FUNCTION void
store4(void * ptr, uint32_t data)
{
	memcpy(ptr, &data, sizeof(data));
}
UTIL_FUNCTION void
store8(void * ptr, uint64_t data)
{
	memcpy(ptr, &data, sizeof(data));
}
UTIL_FUNCTION void
store16(void * ptr, vector_uchar16 data)
{
	*(packed_uchar16 *)ptr = (packed_uchar16)data;
}
UTIL_FUNCTION void
store32(void * ptr, vector_uchar32 data)
{
	*(packed_uchar32 *)ptr = (packed_uchar32)data;
}
UTIL_FUNCTION void
store64(void * ptr, vector_uchar64 data)
{
	*(packed_uchar64 *)ptr = (packed_uchar64)data;
}

/* Load+Store N bytes from unaligned locations SRC to DST. No overlap allowed */
UTIL_FUNCTION void
copy8(void * dst, const void * src)
{
	store8(dst, load8(src));
}
UTIL_FUNCTION void
copy16(void * dst, const void * src)
{
	*(packed_uchar16 *)dst = *(const packed_uchar16 *)src;
}
UTIL_FUNCTION void
copy32(void * dst, const void * src)
{
	*(packed_uchar32 *)dst = *(const packed_uchar32 *)src;
}

#undef memcpy

#if defined (__cplusplus)
}
#endif
