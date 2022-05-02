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

ATTRS
#ifdef ALL_STATIC
static
#endif
void F(iop_hash)(const struct iop_struct_t * nonnull st,
                 const void * nonnull v, iop_hash_f * nonnull hfun,
                 void * nonnull ctx, unsigned flags);

#define HASH(pfx, ...)  \
    ({  pfx##_ctx ctx;                                                       \
        F(pfx##_starts)(&ctx, ##__VA_ARGS__);                                \
        F(iop_hash)(st, v, (iop_hash_f *)&F(pfx##_update), (void *)&ctx,     \
                    flags);                                                  \
        F(pfx##_finish)(&ctx, buf); })

#define HMAC(pfx, ...)  \
    ({  pfx##_ctx ctx;                                                       \
        F(pfx##_hmac_starts)(&ctx, k.s, k.len, ##__VA_ARGS__);               \
        F(iop_hash)(st, v, (iop_hash_f *)&F(pfx##_hmac_update), (void *)&ctx,\
                    flags);                                                  \
        F(pfx##_hmac_finish)(&ctx, buf); })

#ifdef __cplusplus
#define HASH_ARGS(sz)                                                        \
    const struct iop_struct_t * nonnull st, const void * nonnull v,          \
    uint8_t buf[sz], unsigned flags
#define HMAC_ARGS(sz)  \
    const struct iop_struct_t * nonnull st, const void * nonnull v, lstr_t k,\
    uint8_t buf[sz], unsigned flags
#else
#define HASH_ARGS(sz)  \
    const struct iop_struct_t * nonnull st, const void * nonnull v,          \
    uint8_t buf[static sz], unsigned flags
#define HMAC_ARGS(sz)  \
    const struct iop_struct_t * nonnull st, const void * nonnull v, lstr_t k,\
    uint8_t buf[static sz], unsigned flags
#endif

#ifndef ONLY_HMAC_SHA256

ATTRS static inline void F(iop_hash_jenkins)(HASH_ARGS(4)) { HASH(jenkins); }

ATTRS static inline void F(iop_hash_murmur_hash3_x86_32)(HASH_ARGS(4),
                                                         uint32_t seed)
{ HASH(murmur_hash3_x86_32, seed); }

ATTRS static inline void F(iop_hash32)(HASH_ARGS(4))       { HASH(hash32); }

ATTRS static inline void F(iop_hash_md5)(HASH_ARGS(16))    { HASH(md5); }
ATTRS static inline void F(iop_hmac_md5)(HMAC_ARGS(16))    { HMAC(md5); }

ATTRS static inline void F(iop_hash_sha1)(HASH_ARGS(20))   { HASH(sha1); }
ATTRS static inline void F(iop_hmac_sha1)(HMAC_ARGS(20))   { HMAC(sha1); }

ATTRS
static inline void F(iop_hash_sha224)(HASH_ARGS(28)) { HASH(sha2, true); }
ATTRS
static inline void F(iop_hmac_sha224)(HMAC_ARGS(28)) { HMAC(sha2, true); }

ATTRS
static inline void F(iop_hash_sha256)(HASH_ARGS(32)) { HASH(sha2, false); }

#endif

ATTRS
static inline void F(iop_hmac_sha256)(HMAC_ARGS(32)) { HMAC(sha2, false); }

#ifndef ONLY_HMAC_SHA256

ATTRS
static inline void F(iop_hash_sha384)(HASH_ARGS(48)) { HASH(sha4, true); }
ATTRS
static inline void F(iop_hmac_sha384)(HMAC_ARGS(48)) { HMAC(sha4, true); }

ATTRS
static inline void F(iop_hash_sha512)(HASH_ARGS(64)) { HASH(sha4, false); }
ATTRS
static inline void F(iop_hmac_sha512)(HMAC_ARGS(64)) { HMAC(sha4, false); }

#endif

#undef HMAC_ARGS
#undef HASH_ARGS
#undef HMAC
#undef HASH
