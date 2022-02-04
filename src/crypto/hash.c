/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#include <lib-common/hash.h>

uint32_t hsieh_hash(const void *_data, ssize_t len)
{
    const byte *data = _data;
    uint32_t hash, tmp;
    size_t rem;

    if (len < 0)
        len = strlen((const char *)data);

    hash  = len;
    rem   = len & 3;
    len >>= 2;

    /* Main loop */
    for (; len > 0; len--, data += 4) {
        hash  += cpu_to_le16pu(data);
        tmp    = (cpu_to_le16pu(data + 2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        hash  += hash >> 11;
    }

    /* Handle end cases */
    switch (rem) {
      case 3:
        hash += cpu_to_le16pu(data);
        hash ^= hash << 16;
        hash ^= data[2] << 18;
        hash += hash >> 11;
        break;
      case 2:
        hash += cpu_to_le16pu(data);
        hash ^= hash << 11;
        hash += hash >> 17;
        break;
      case 1:
        hash += *data;
        hash ^= hash << 10;
        hash += hash >> 1;
        break;
    }

    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}

uint32_t jenkins_hash(const void *_s, ssize_t len)
{
    const byte *s = _s;
    byte output[4];
    jenkins_ctx ctx;

    if (len < 0) {
        len = strlen((const char *)s);
    }

    jenkins_starts(&ctx);
    jenkins_update(&ctx, _s, len);
    jenkins_finish(&ctx, output);

    return get_unaligned_cpu32(output);
}

uint32_t jenkins_hash_ascii_lower(const void *_s, ssize_t len)
{
    const byte *s = _s;
    byte output[4];
    jenkins_ctx ctx;

    if (len < 0) {
        len = strlen((const char *)s);
    }

    jenkins_starts(&ctx);
    jenkins_update_ascii_lower(&ctx, _s, len);
    jenkins_finish(&ctx, output);

    return get_unaligned_cpu32(output);
}

void jenkins_starts(jenkins_ctx *ctx)
{
    ctx->hash = 0;
}

void jenkins_update(jenkins_ctx *ctx, const void *input, ssize_t len)
{
    const byte *s = input;
    uint32_t hash = ctx->hash;

    if (len <= 0) {
        return;
    }

    while (len-- > 0) {
        hash += *s++;
        hash += hash << 10;
        hash ^= hash >> 6;
    }

    ctx->hash = hash;
}

void jenkins_update_ascii_lower(jenkins_ctx *ctx, const void *input,
                                ssize_t len)
{
    const byte *s = input;
    uint32_t hash = ctx->hash;

    if (len <= 0) {
        return;
    }

    while (len-- > 0) {
        hash += tolower(*s++);
        hash += hash << 10;
        hash ^= hash >> 6;
    }

    ctx->hash = hash;
}

void jenkins_finish(jenkins_ctx *ctx, byte output[4])
{
    uint32_t hash = ctx->hash;

    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;

    put_unaligned_cpu32(output, hash);
}

/*
 * murmur_hash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 *
 * Note - The x86 and x64 versions do _not_ produce the same results, as the
 * algorithms are optimized for their respective platforms. You can still
 * compile and run any of them on any platform, but your performance with the
 * non-native version will be less than optimal.
 *
 * From http://code.google.com/p/smhasher/ rev-136
 */

static ALWAYS_INLINE uint32_t rotl32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

static ALWAYS_INLINE uint64_t rotl64(uint64_t x, int8_t r)
{
    return (x << r) | (x >> (64 - r));
}

static ALWAYS_INLINE uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;

    return h;
}

static ALWAYS_INLINE uint64_t fmix64(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdLLU;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53LLU;
    k ^= k >> 33;

    return k;
}

void murmur_hash3_x86_32_starts(murmur_hash3_x86_32_ctx *ctx, uint32_t seed)
{
    ctx->len = 0;
    ctx->tail_len = 0;
    ctx->h1 = seed;
}

#define MURMUR_HASH3_X86_32_C1  0xcc9e2d51
#define MURMUR_HASH3_X86_32_C2  0x1b873593

static ALWAYS_INLINE uint32_t
__murmur_hash3_x86_32_process_block(uint32_t h1, uint32_t block)
{
    uint32_t k1 = block;

    k1 *= MURMUR_HASH3_X86_32_C1;
    k1 = rotl32(k1, 15);
    k1 *= MURMUR_HASH3_X86_32_C2;

    h1 ^= k1;
    h1 = rotl32(h1, 13);
    h1 = h1 * 5 + 0xe6546b64;

    return h1;
}

/** Push bytes of data into a 32bits integers.
 *
 * \note The use of this function is reserved to the management of the tail in
 *       murmur_hash3_x86_32 algorithm.
 */
static ALWAYS_INLINE uint32_t
__murmur_hash3_x86_32_push_block(uint32_t block, uint8_t block_len,
                                 const byte *data, size_t len)
{
    uint32_t data_block = 0;

    /* XXX Not expected to happen in that case: get_unaligned_cpu32() is more
     * appropriate. */
    assert (block_len || len < 4);

    switch (len) {
      default:
      case 3:
        data_block |= data[2] << 16;
        /* FALLTHROUGH */
      case 2:
        data_block |= data[1] << 8;
        /* FALLTHROUGH */
      case 1:
        data_block |= data[0];
        /* FALLTHROUGH */
      case 0:
        break;
    }

    return block | (data_block << (8 * block_len));
}

static ALWAYS_INLINE uint32_t
__murmur_hash3_x86_32_process_tail(uint32_t h1,
                                   uint32_t tail, uint8_t tail_len)
{
    uint32_t k1 = tail;

    if (tail_len) {
        k1 *= MURMUR_HASH3_X86_32_C1;
        k1  = rotl32(k1, 15);
        k1 *= MURMUR_HASH3_X86_32_C2;
        h1 ^= k1;
    };

    return h1;
}

void murmur_hash3_x86_32_update(murmur_hash3_x86_32_ctx *ctx,
                                const void *key, size_t len)
{
    const uint8_t *data = (const byte *)key;
    size_t nblocks;
    uint32_t h1 = ctx->h1;

    ctx->len += len;

    /* Head */
    if (ctx->tail_len) {
        size_t head_len = 4 - ctx->tail_len;

        ctx->tail = __murmur_hash3_x86_32_push_block(ctx->tail, ctx->tail_len,
                                                     data, len);
        if (len < head_len) {
            ctx->tail_len += len;
            return;
        }

        data += head_len;
        len -= head_len;

        h1 = __murmur_hash3_x86_32_process_block(h1, ctx->tail);
    }

    nblocks = len / 4;

    /* Body */
    {
        const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);

        for (ssize_t i = -nblocks; i; i++) {
            h1 = __murmur_hash3_x86_32_process_block(h1, get_unaligned_cpu32(blocks + i));
        }
    }

    ctx->h1 = h1;

    /* Save tail */
    ctx->tail_len = len & 3;
    if (ctx->tail_len) {
        const uint8_t *tail = &data[nblocks * 4];

        ctx->tail = __murmur_hash3_x86_32_push_block(0, 0,
                                                     tail, ctx->tail_len);
    }
}

void murmur_hash3_x86_32_finish(murmur_hash3_x86_32_ctx *ctx, byte output[4])
{
    uint32_t h1 = ctx->h1;

    /* Tail */
    h1 = __murmur_hash3_x86_32_process_tail(h1, ctx->tail, ctx->tail_len);

    /* Finalization */
    h1 ^= ctx->len;
    h1  = fmix32(h1);

    put_unaligned_cpu32(output, h1);
}

uint32_t murmur_hash3_x86_32(const void *key, size_t len, uint32_t seed)
{
    const byte *data = (const byte *)key;
    const size_t nblocks = len / 4;
    uint32_t h1 = seed;

    //----------
    // body
    const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);

    for (ssize_t i = -nblocks; i; i++) {
        uint32_t block = get_unaligned_cpu32(&blocks[i]);

        h1 = __murmur_hash3_x86_32_process_block(h1, block);
    }

    //----------
    // tail
    {
        const byte *tail = data + nblocks * 4;
        uint32_t block;

        block = __murmur_hash3_x86_32_push_block(0, 0, tail, len & 3);
        h1 = __murmur_hash3_x86_32_process_tail(h1, block, len & 3);
    }

    //----------
    // finalization
    h1 ^= len;
    h1  = fmix32(h1);

    return h1;
}

void murmur_hash3_x86_128(const void *key, size_t len,
                          uint32_t seed, char out[static 16])
{
    const uint8_t *data = key;
    const size_t nblocks = len / 16;

    uint32_t h1 = seed;
    uint32_t h2 = seed;
    uint32_t h3 = seed;
    uint32_t h4 = seed;

    uint32_t c1 = 0x239b961b;
    uint32_t c2 = 0xab0e9789;
    uint32_t c3 = 0x38b34ae5;
    uint32_t c4 = 0xa1e38b93;

    //----------
    // body

    const uint32_t *blocks = (const uint32_t *)(data + nblocks*16);

    for (ssize_t i = -nblocks; i; i++) {
        uint32_t k1 = get_unaligned_cpu32(blocks + i * 4 + 0);
        uint32_t k2 = get_unaligned_cpu32(blocks + i * 4 + 1);
        uint32_t k3 = get_unaligned_cpu32(blocks + i * 4 + 2);
        uint32_t k4 = get_unaligned_cpu32(blocks + i * 4 + 3);

        k1 *= c1; k1  = rotl32(k1, 15); k1 *= c2; h1 ^= k1;

        h1 = rotl32(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1b;

        k2 *= c2; k2  = rotl32(k2, 16); k2 *= c3; h2 ^= k2;

        h2 = rotl32(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747;

        k3 *= c3; k3  = rotl32(k3, 17); k3 *= c4; h3 ^= k3;

        h3 = rotl32(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35;

        k4 *= c4; k4  = rotl32(k4, 18); k4 *= c1; h4 ^= k4;

        h4 = rotl32(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17;
    }

    //----------
    // tail
    {
        const uint8_t *tail = (const uint8_t*)(data + nblocks*16);

        uint32_t k1 = 0;
        uint32_t k2 = 0;
        uint32_t k3 = 0;
        uint32_t k4 = 0;

        switch (len & 15) {
          case 15: k4 ^= tail[14] << 16;
                   /* FALLTHROUGH */
          case 14: k4 ^= tail[13] << 8;
                   /* FALLTHROUGH */
          case 13: k4 ^= tail[12] << 0;
                   k4 *= c4; k4  = rotl32(k4, 18); k4 *= c1; h4 ^= k4;
                   /* FALLTHROUGH */

          case 12: k3 ^= tail[11] << 24;
                   /* FALLTHROUGH */
          case 11: k3 ^= tail[10] << 16;
                   /* FALLTHROUGH */
          case 10: k3 ^= tail[ 9] << 8;
                   /* FALLTHROUGH */
          case  9: k3 ^= tail[ 8] << 0;
                   k3 *= c3; k3  = rotl32(k3, 17); k3 *= c4; h3 ^= k3;
                   /* FALLTHROUGH */

          case  8: k2 ^= tail[ 7] << 24;
                   /* FALLTHROUGH */
          case  7: k2 ^= tail[ 6] << 16;
                   /* FALLTHROUGH */
          case  6: k2 ^= tail[ 5] << 8;
                   /* FALLTHROUGH */
          case  5: k2 ^= tail[ 4] << 0;
                   k2 *= c2; k2  = rotl32(k2, 16); k2 *= c3; h2 ^= k2;
                   /* FALLTHROUGH */

          case  4: k1 ^= tail[ 3] << 24;
                   /* FALLTHROUGH */
          case  3: k1 ^= tail[ 2] << 16;
                   /* FALLTHROUGH */
          case  2: k1 ^= tail[ 1] << 8;
                   /* FALLTHROUGH */
          case  1: k1 ^= tail[ 0] << 0;
                   k1 *= c1; k1  = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
                   /* FALLTHROUGH */
        };
    }

    //----------
    // finalization

    h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    h1 = fmix32(h1);
    h2 = fmix32(h2);
    h3 = fmix32(h3);
    h4 = fmix32(h4);

    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;

    ((uint32_t *)out)[0] = h1;
    ((uint32_t *)out)[1] = h2;
    ((uint32_t *)out)[2] = h3;
    ((uint32_t *)out)[3] = h4;
}

void murmur_hash3_x64_128(const void *key, size_t len,
                          uint32_t seed, char out[static 16])
{
    const uint8_t *data = (const uint8_t*)key;
    const size_t nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    uint64_t c1 = 0x87c37b91114253d5LLU;
    uint64_t c2 = 0x4cf5ad432745937fLLU;

    //----------
    // body

    const uint64_t *blocks = (const uint64_t *)(data);

    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k1 = get_unaligned_cpu64(blocks + i * 2 + 0);
        uint64_t k2 = get_unaligned_cpu64(blocks + i * 2 + 1);

        k1 *= c1; k1  = rotl64(k1, 31); k1 *= c2; h1 ^= k1;

        h1 = rotl64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;

        k2 *= c2; k2  = rotl64(k2, 33); k2 *= c1; h2 ^= k2;

        h2 = rotl64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
    }

    //----------
    // tail
    {
        const uint8_t *tail = (const uint8_t*)(data + nblocks*16);

        uint64_t k1 = 0;
        uint64_t k2 = 0;

        switch (len & 15) {
          case 15: k2 ^= (uint64_t)tail[14] << 48;
                   /* FALLTHROUGH */
          case 14: k2 ^= (uint64_t)tail[13] << 40;
                   /* FALLTHROUGH */
          case 13: k2 ^= (uint64_t)tail[12] << 32;
                   /* FALLTHROUGH */
          case 12: k2 ^= (uint64_t)tail[11] << 24;
                   /* FALLTHROUGH */
          case 11: k2 ^= (uint64_t)tail[10] << 16;
                   /* FALLTHROUGH */
          case 10: k2 ^= (uint64_t)tail[ 9] << 8;
                   /* FALLTHROUGH */
          case  9: k2 ^= (uint64_t)tail[ 8] << 0;
                   k2 *= c2; k2  = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
                   /* FALLTHROUGH */

          case  8: k1 ^= (uint64_t)tail[ 7] << 56;
                   /* FALLTHROUGH */
          case  7: k1 ^= (uint64_t)tail[ 6] << 48;
                   /* FALLTHROUGH */
          case  6: k1 ^= (uint64_t)tail[ 5] << 40;
                   /* FALLTHROUGH */
          case  5: k1 ^= (uint64_t)tail[ 4] << 32;
                   /* FALLTHROUGH */
          case  4: k1 ^= (uint64_t)tail[ 3] << 24;
                   /* FALLTHROUGH */
          case  3: k1 ^= (uint64_t)tail[ 2] << 16;
                   /* FALLTHROUGH */
          case  2: k1 ^= (uint64_t)tail[ 1] << 8;
                   /* FALLTHROUGH */
          case  1: k1 ^= (uint64_t)tail[ 0] << 0;
                   k1 *= c1; k1  = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
                   /* FALLTHROUGH */
        };
    }

    //----------
    // finalization

    h1 ^= len; h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    ((uint64_t*)out)[0] = h1;
    ((uint64_t*)out)[1] = h2;
}

/* {{{ Hashers */

uint64_t identity_hash_64(const void *data, ssize_t len)
{
    uint64_t v = 0;

    if (len >= ssizeof(uint64_t)) {
        return *(uint64_t *)data;
    } else {
        memcpy(&v, data, len);
        return v;
    }
}

uint64_t murmur3_128_hash_64(const void *data, ssize_t len)
{
    union {
        char b[16];
        uint64_t u[2];
    } res;

    murmur_hash3_x64_128(data, len, 0, res.b);
    return res.u[0] ^ res.u[1];
}

/* }}} */
