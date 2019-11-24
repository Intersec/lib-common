/*
 *  FIPS-180-2 compliant SHA-256 implementation
 *
 *  Copyright (C) 2006-2007  Christophe Devine
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of XySSL nor the names of its contributors may be
 *      used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  The SHA-256 Secure Hash Standard was published by NIST in 2002.
 *
 *  http://csrc.nist.gov/publications/fips/fips180-2/fips180-2.pdf
 */

#include <lib-common/z.h>
#include <lib-common/hash.h>

#define ATTRS
#define F(x)  x

#include "sha2.in.c"

#undef F
#undef ATTRS

void sha2_finish_hex( sha2_ctx *ctx, char output[65] )
{
    byte digest[32];
    sha2_finish(ctx, digest);
    strconv_hexencode(output, 65, digest,
                      ctx->is224 ? SHA224_DIGEST_SIZE : SHA256_DIGEST_SIZE);
}

uint64_t sha2_hash_64(const void *data, ssize_t len)
{
    union {
        byte b[32];
        uint64_t u[4];
    } res;

    sha2(data, len, res.b, false);
    return res.u[0] ^ res.u[1] ^ res.u[2] ^ res.u[3];
}

/*
 * output = SHA-256( input buffer )
 */
void sha2_hex( const void *input, ssize_t ilen, char output[65], int is224 )
{
    sha2_ctx ctx;

    sha2_starts( &ctx, is224 );
    sha2_update( &ctx, input, ilen );
    sha2_finish_hex( &ctx, output );

    memset( &ctx, 0, sizeof( sha2_ctx ) );
}

/*
 * output = SHA-256( file contents )
 */
int sha2_file(const char *path, byte output[32], int is224 )
{
    FILE *f;
    size_t n;
    sha2_ctx ctx;
    byte buf[1024];

    if( ( f = fopen( path, "rb" ) ) == NULL )
        return( 1 );

    sha2_starts( &ctx, is224 );

    while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
        sha2_update( &ctx, buf, (int) n );

    sha2_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha2_ctx ) );

    if( ferror( f ) != 0 )
    {
        fclose( f );
        return( 2 );
    }

    fclose( f );
    return( 0 );
}

/*
 * output = HMAC-SHA-256( hmac key, input buffer )
 */
void sha2_hmac( const void *key, int keylen,
                const void *input, ssize_t ilen,
                byte output[32], int is224 )
{
    sha2_ctx ctx;

    sha2_hmac_starts( &ctx, key, keylen, is224 );
    sha2_hmac_update( &ctx, input, ilen );
    sha2_hmac_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha2_ctx ) );
}

/* {{{ SHA-256 Crypt */

/* Based on Ulrich Drepper's Unix crypt with SHA256, version 0.4 2008-4-3,
 * released by Ulrich Drepper in public domain */

/* Table used for base64 transformation */
static const char sha2_crypt_base64char_g[64] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void sha2_crypt(const void *input, size_t ilen, const void *salt,
                size_t slen, uint32_t rounds, sb_t *output)
{
    uint32_t i;
    unsigned char alt_result[32];
    unsigned char tmp_result[32];
    sha2_ctx ctx;
    sha2_ctx alt_ctx;
    SB_1k(buf);
    SB_1k(alt_buf);

    slen = MIN(slen, SHA256_CRYPT_SALT_LEN_MAX);
    rounds = (rounds > 0) ? rounds : SHA256_CRYPT_DEFAULT_ROUNDS;
    rounds = MAX(rounds, SHA256_CRYPT_MIN_ROUNDS);
    rounds = MIN(rounds, SHA256_CRYPT_MAX_ROUNDS);

    /* Let's begin */
    sha2_starts(&ctx, false);

    /* Add the input string */
    sha2_update(&ctx, input, ilen);

    /* Add the salt */
    sha2_update(&ctx, salt, slen);

    /* Alternate hash with INPUT-SALT-INPUT */
    sha2_starts(&alt_ctx, false);
    sha2_update(&alt_ctx, input, ilen);
    sha2_update(&alt_ctx, salt, slen);
    sha2_update(&alt_ctx, input, ilen);
    sha2_finish(&alt_ctx, alt_result);

    for (i = ilen; i > 32; i -= 32) {
        sha2_update(&ctx, alt_result, 32);
    }
    sha2_update(&ctx, alt_result, i);

    /* For every 1 in the binary representation of ilen add alt,
     * for every 0 add the input
     */
    for (i = ilen; i > 0; i >>= 1) {
        if ((i & 1) != 0) {
            sha2_update(&ctx, alt_result, 32);
        } else {
            sha2_update(&ctx, input, ilen);
        }
    }

    /* Intermediate result */
    sha2_finish(&ctx, alt_result);

    /* New alternate hash */
    sha2_starts(&alt_ctx, false);

    /* For every character in the input add the entire input */
    for (i = 0; i < ilen; i++) {
        sha2_update(&alt_ctx, input, ilen);
    }

    /* Get temp result */
    sha2_finish(&alt_ctx, tmp_result);

    for (i = ilen; i >= 32; i -= 32) {
        sb_add(&buf, tmp_result, 32);
    }
    sb_add(&buf, tmp_result, i);

    sha2_starts(&alt_ctx, false);

    /* For each character in the input, add input */
    for (i = 0; i < 16U + alt_result[0]; i++) {
        sha2_update(&alt_ctx, salt, slen);
    }

    sha2_finish(&alt_ctx, tmp_result);

    for (i = slen; i >= 32; i -= 32) {
        sb_add(&alt_buf, tmp_result, 32);
    }
    sb_add(&alt_buf, tmp_result, i);

    /* The loop */
    for (i = 0; i < rounds; i++) {
        sha2_starts(&ctx, false);

        /* Add input or last result */
        if ((i & 1) != 0) {
            sha2_update(&ctx, buf.data, buf.len);
        } else {
            sha2_update(&ctx, alt_result, 32);
        }

        /* Add salt for numbers not divisible by 3 */
        if (i % 3 != 0) {
            sha2_update(&ctx, alt_buf.data, alt_buf.len);
        }

        /* Add input for numbers not divisible by 7 */
        if (i % 7 != 0) {
            sha2_update(&ctx, buf.data, buf.len);
        }

        /* Add intput or last result */
        if ((i & 1) != 0) {
            sha2_update(&ctx, alt_result, 32);
        } else {
            sha2_update(&ctx, buf.data, buf.len);
        }

        /* Create intermediate result */
        sha2_finish(&ctx, alt_result);
    }

    /* Construction of the result */
    sb_reset(output);
    sb_addf(output, SHA256_PREFIX "$");

    /* Here, we ALWAYS put the round prefix and round number in the result
     * string, not only if round number != default number.
     * This seems very less dangerous for compatibility between
     * encryption version used in our products. It is still compliant with
     * the specifications, even if the implementation given as example by
     * Drepper does not follow this precept.
     */
    sb_addf(output, SHA256_ROUNDS_PREFIX);
    sb_addf(output, "%d$", rounds);
    sb_add(output, salt, slen);
    sb_addf(output, "$");

#define b64_from_24bits(b2_, b1_, b0_, n_)                                  \
    do {                                                                    \
        unsigned int w = cpu_to_le32(((b2_) << 16) | ((b1_) << 8) | (b0_)); \
        for (int i_ = n_; i_-- > 0; w >>= 6) {                              \
            sb_add(output, &sha2_crypt_base64char_g[w & 0x3f], 1);          \
        }                                                                   \
    } while (0)

    b64_from_24bits(alt_result[0],   alt_result[10],  alt_result[20], 4);
    b64_from_24bits(alt_result[21],  alt_result[1],   alt_result[11], 4);
    b64_from_24bits(alt_result[12],  alt_result[22],  alt_result[2],  4);
    b64_from_24bits(alt_result[3],   alt_result[13],  alt_result[23], 4);
    b64_from_24bits(alt_result[24],  alt_result[4],   alt_result[14], 4);
    b64_from_24bits(alt_result[15],  alt_result[25],  alt_result[5],  4);
    b64_from_24bits(alt_result[6],   alt_result[16],  alt_result[26], 4);
    b64_from_24bits(alt_result[27],  alt_result[7],   alt_result[17], 4);
    b64_from_24bits(alt_result[18],  alt_result[28],  alt_result[8],  4);
    b64_from_24bits(alt_result[9],   alt_result[19],  alt_result[29], 4);
    b64_from_24bits(0,               alt_result[31],  alt_result[30], 3);

#undef b64_from_24bits

    /* Clearing datas to avoid core dump attacks */
    sha2_starts(&ctx, false);
    sha2_finish(&ctx, alt_result);

    p_clear(&tmp_result, 1);
    p_clear(&ctx, 1);
    p_clear(&alt_ctx, 1);
}

int sha2_crypt_parse(lstr_t input, int *out_rounds, pstream_t *out_salt,
                     pstream_t *out_hash)
{
    /* Correct format for SHA256-crypt is
     * $5$rounds=2000$salt$hash */

    pstream_t ps = ps_initlstr(&input);
    pstream_t salt;
    pstream_t hash;
    int rounds;
    static bool first_call = true;
    static ctype_desc_t sha2_crypt_ctype;

    out_salt = out_salt ?: &salt;
    out_hash = out_hash ?: &hash;
    out_rounds = out_rounds ?: &rounds;

    /* Check prefix */
    RETHROW(ps_skipstr(&ps, "$5$rounds="));

    /* Check rounds */
    errno = 0;
    *out_rounds = ps_geti(&ps);
    THROW_ERR_IF(errno != 0);
    THROW_ERR_IF(*out_rounds < SHA256_CRYPT_MIN_ROUNDS);
    THROW_ERR_IF(*out_rounds > SHA256_CRYPT_MAX_ROUNDS);

    /* Check salt */
    RETHROW(ps_skipc(&ps, '$'));
    RETHROW(ps_get_ps_chr_and_skip(&ps, '$', out_salt));
    THROW_ERR_IF(ps_done(out_salt));
    THROW_ERR_IF(ps_len(out_salt) > SHA256_CRYPT_SALT_LEN_MAX);

    /* Check hash-part */
    if (unlikely(first_call)) {
        first_call = false;
        ctype_desc_build2(&sha2_crypt_ctype, sha2_crypt_base64char_g,
                          sizeof(sha2_crypt_base64char_g));
    }
    THROW_ERR_IF(ps_len(&ps) != SHA256_CRYPT_DIGEST_SIZE);
    *out_hash = ps;
    ps_skip_span(&ps, &sha2_crypt_ctype);
    THROW_ERR_IF(!ps_done(&ps));

    return 0;
}

/* }}} */
