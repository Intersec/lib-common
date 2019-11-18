/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

#ifndef IS_LIB_COMMON_HASH_H
#define IS_LIB_COMMON_HASH_H

#include "core.h"
#include "arith.h"

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#define SHA1_DIGEST_SIZE    (160 / 8)
#define SHA224_DIGEST_SIZE  (224 / 8)
#define SHA256_DIGEST_SIZE  (256 / 8)
#define SHA384_DIGEST_SIZE  (384 / 8)
#define SHA512_DIGEST_SIZE  (512 / 8)
#define MD5_DIGEST_SIZE     (128 / 8)

#define MD5_HEX_DIGEST_SIZE     (MD5_DIGEST_SIZE    * 2 + 1)
#define SHA1_HEX_DIGEST_SIZE    (SHA1_DIGEST_SIZE   * 2 + 1)
#define SHA224_HEX_DIGEST_SIZE  (SHA224_DIGEST_SIZE * 2 + 1)
#define SHA256_HEX_DIGEST_SIZE  (SHA256_DIGEST_SIZE * 2 + 1)
#define SHA384_HEX_DIGEST_SIZE  (SHA384_DIGEST_SIZE * 2 + 1)
#define SHA512_HEX_DIGEST_SIZE  (SHA512_DIGEST_SIZE * 2 + 1)

#define SHA1_BLOCK_SIZE     ( 512 / 8)
#define SHA256_BLOCK_SIZE   ( 512 / 8)
#define SHA512_BLOCK_SIZE   (1024 / 8)
#define SHA384_BLOCK_SIZE   SHA512_BLOCK_SIZE
#define SHA224_BLOCK_SIZE   SHA256_BLOCK_SIZE

#define DES3_BLOCK_SIZE  (64 / 8)

#define GET_U32_LE(n,b,i)    ((n) = cpu_to_le32pu((b) + (i)))
#define PUT_U32_LE(n,b,i)    (*acast(le32_t, (b) + (i)) = cpu_to_le32(n))
#define GET_U32_BE(n,b,i)    ((n) = cpu_to_be32pu((b) + (i)))
#define PUT_U32_BE(n,b,i)    (*acast(be32_t, (b) + (i)) = cpu_to_be32(n))

#include "src/hash/aes.h"
#include "src/hash/des.h"
#include "src/hash/md5.h"
#include "src/hash/padlock.h"
#include "src/hash/sha1.h"
#include "src/hash/sha2.h"
#include "src/hash/sha4.h"

typedef struct jenkins_ctx {
    uint32_t hash;
} jenkins_ctx;

void jenkins_starts(jenkins_ctx * nonnull ctx) __leaf;
void jenkins_update(jenkins_ctx * nonnull ctx, const void * nonnull input,
                    ssize_t len) __leaf;
void jenkins_finish(jenkins_ctx * nonnull ctx, byte output[4]) __leaf;

typedef struct murmur_hash3_x86_32_ctx {
    uint32_t h1;
    uint32_t tail;
    size_t   len;
    uint8_t  tail_len;
} murmur_hash3_x86_32_ctx;

void murmur_hash3_x86_32_starts(murmur_hash3_x86_32_ctx * nonnull ctx,
                                uint32_t seed) __leaf;
void murmur_hash3_x86_32_update(murmur_hash3_x86_32_ctx * nonnull ctx,
                                const void * nonnull key, size_t len) __leaf;
void murmur_hash3_x86_32_finish(murmur_hash3_x86_32_ctx * nonnull ctx,
                                byte output[4]) __leaf;

#define MEM_HASH32_MURMUR_SEED  0xdeadc0de

#define HASH32_IMPL(method, ...)                                             \
typedef struct hash32_ctx {                                                  \
    method##_ctx ctx;                                                        \
} hash32_ctx;                                                                \
static inline void hash32_starts(hash32_ctx *nonnull ctx)                    \
{                                                                            \
    method##_starts(&ctx->ctx, ##__VA_ARGS__);                               \
}                                                                            \
static inline void hash32_update(hash32_ctx *nonnull ctx,                    \
                                 const void *nonnull input, ssize_t len)     \
{                                                                            \
    method##_update(&ctx->ctx, input, len);                                  \
}                                                                            \
static inline void hash32_finish(hash32_ctx *nonnull ctx, byte output[4])    \
{                                                                            \
    method##_finish(&ctx->ctx, output);                                      \
}

#if defined(__x86_64__) || defined(__i386__)
    HASH32_IMPL(murmur_hash3_x86_32, MEM_HASH32_MURMUR_SEED);
#else
    HASH32_IMPL(jenkins);
#endif

#undef HASH32_IMPL

#include "src/hash/iop.h"

uint32_t icrc32(uint32_t crc, const void * nonnull data, ssize_t len) __leaf;
uint64_t icrc64(uint64_t crc, const void * nonnull data, ssize_t len) __leaf;

uint32_t hsieh_hash(const void * nonnull s, ssize_t len) __leaf;
uint32_t jenkins_hash(const void * nonnull s, ssize_t len) __leaf;

#ifdef __cplusplus
#define murmur_128bits_buf char out[]
#else
#define murmur_128bits_buf char out[static 16]
#endif

uint32_t murmur_hash3_x86_32(const void * nonnull key, size_t len,
                             uint32_t seed) __leaf;
void     murmur_hash3_x86_128(const void * nonnull key, size_t len,
                              uint32_t seed, murmur_128bits_buf) __leaf;
void     murmur_hash3_x64_128(const void * nonnull key, size_t len,
                              uint32_t seed, murmur_128bits_buf) __leaf;

static inline uint32_t mem_hash32(const void * nonnull data, ssize_t len)
{
    if (unlikely(len < 0))
        len = strlen((const char *)data);
#if defined(__x86_64__) || defined(__i386__)
    return murmur_hash3_x86_32(data, len, MEM_HASH32_MURMUR_SEED);
#else
    return jenkins_hash(data, len);
#endif
}

static inline uint32_t u64_hash32(uint64_t u64)
{
    return (uint32_t)(u64) ^ (uint32_t)(u64 >> 32);
}

uint64_t identity_hash_64(const void * nonnull data, ssize_t len);

uint64_t murmur3_128_hash_64(const void * nonnull data, ssize_t len);

static inline uint64_t crc64_hash_64(const void * nonnull data, ssize_t len)
{
    return icrc64(0, data, len);
}

static inline uint64_t hsieh_hash_64(const void * nonnull data, ssize_t len)
{
    return hsieh_hash(data, len);
}

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
