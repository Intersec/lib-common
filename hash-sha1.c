/*
 *  FIPS-180-1 compliant SHA-1 implementation
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
 *  The SHA-1 standard was published by NIST in 1993.
 *
 *  http://www.itl.nist.gov/fipspubs/fip180-1.htm
 */

#include "z.h"
#include "hash.h"

/*
 * SHA-1 context setup
 */
void sha1_starts( sha1_ctx *ctx )
{
    ctx->total[0] = 0;
    ctx->total[1] = 0;

    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

static void sha1_process( sha1_ctx *ctx, const byte data[64] )
{
    uint32_t temp, W[16], A, B, C, D, E;

    GET_U32_BE( W[ 0], data,  0 );
    GET_U32_BE( W[ 1], data,  4 );
    GET_U32_BE( W[ 2], data,  8 );
    GET_U32_BE( W[ 3], data, 12 );
    GET_U32_BE( W[ 4], data, 16 );
    GET_U32_BE( W[ 5], data, 20 );
    GET_U32_BE( W[ 6], data, 24 );
    GET_U32_BE( W[ 7], data, 28 );
    GET_U32_BE( W[ 8], data, 32 );
    GET_U32_BE( W[ 9], data, 36 );
    GET_U32_BE( W[10], data, 40 );
    GET_U32_BE( W[11], data, 44 );
    GET_U32_BE( W[12], data, 48 );
    GET_U32_BE( W[13], data, 52 );
    GET_U32_BE( W[14], data, 56 );
    GET_U32_BE( W[15], data, 60 );

#define S(x,n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define R(t)                                            \
(                                                       \
    temp = W[(t -  3) & 0x0F] ^ W[(t - 8) & 0x0F] ^     \
           W[(t - 14) & 0x0F] ^ W[ t      & 0x0F],      \
    ( W[t & 0x0F] = S(temp,1) )                         \
)

#define P(a,b,c,d,e,x)                                  \
{                                                       \
    e += S(a,5) + F(b,c,d) + K + x; b = S(b,30);        \
}

    A = ctx->state[0];
    B = ctx->state[1];
    C = ctx->state[2];
    D = ctx->state[3];
    E = ctx->state[4];

#define F(x,y,z) (z ^ (x & (y ^ z)))
#define K 0x5A827999

    P( A, B, C, D, E, W[0]  );
    P( E, A, B, C, D, W[1]  );
    P( D, E, A, B, C, W[2]  );
    P( C, D, E, A, B, W[3]  );
    P( B, C, D, E, A, W[4]  );
    P( A, B, C, D, E, W[5]  );
    P( E, A, B, C, D, W[6]  );
    P( D, E, A, B, C, W[7]  );
    P( C, D, E, A, B, W[8]  );
    P( B, C, D, E, A, W[9]  );
    P( A, B, C, D, E, W[10] );
    P( E, A, B, C, D, W[11] );
    P( D, E, A, B, C, W[12] );
    P( C, D, E, A, B, W[13] );
    P( B, C, D, E, A, W[14] );
    P( A, B, C, D, E, W[15] );
    P( E, A, B, C, D, R(16) );
    P( D, E, A, B, C, R(17) );
    P( C, D, E, A, B, R(18) );
    P( B, C, D, E, A, R(19) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0x6ED9EBA1

    P( A, B, C, D, E, R(20) );
    P( E, A, B, C, D, R(21) );
    P( D, E, A, B, C, R(22) );
    P( C, D, E, A, B, R(23) );
    P( B, C, D, E, A, R(24) );
    P( A, B, C, D, E, R(25) );
    P( E, A, B, C, D, R(26) );
    P( D, E, A, B, C, R(27) );
    P( C, D, E, A, B, R(28) );
    P( B, C, D, E, A, R(29) );
    P( A, B, C, D, E, R(30) );
    P( E, A, B, C, D, R(31) );
    P( D, E, A, B, C, R(32) );
    P( C, D, E, A, B, R(33) );
    P( B, C, D, E, A, R(34) );
    P( A, B, C, D, E, R(35) );
    P( E, A, B, C, D, R(36) );
    P( D, E, A, B, C, R(37) );
    P( C, D, E, A, B, R(38) );
    P( B, C, D, E, A, R(39) );

#undef K
#undef F

#define F(x,y,z) ((x & y) | (z & (x | y)))
#define K 0x8F1BBCDC

    P( A, B, C, D, E, R(40) );
    P( E, A, B, C, D, R(41) );
    P( D, E, A, B, C, R(42) );
    P( C, D, E, A, B, R(43) );
    P( B, C, D, E, A, R(44) );
    P( A, B, C, D, E, R(45) );
    P( E, A, B, C, D, R(46) );
    P( D, E, A, B, C, R(47) );
    P( C, D, E, A, B, R(48) );
    P( B, C, D, E, A, R(49) );
    P( A, B, C, D, E, R(50) );
    P( E, A, B, C, D, R(51) );
    P( D, E, A, B, C, R(52) );
    P( C, D, E, A, B, R(53) );
    P( B, C, D, E, A, R(54) );
    P( A, B, C, D, E, R(55) );
    P( E, A, B, C, D, R(56) );
    P( D, E, A, B, C, R(57) );
    P( C, D, E, A, B, R(58) );
    P( B, C, D, E, A, R(59) );

#undef K
#undef F

#define F(x,y,z) (x ^ y ^ z)
#define K 0xCA62C1D6

    P( A, B, C, D, E, R(60) );
    P( E, A, B, C, D, R(61) );
    P( D, E, A, B, C, R(62) );
    P( C, D, E, A, B, R(63) );
    P( B, C, D, E, A, R(64) );
    P( A, B, C, D, E, R(65) );
    P( E, A, B, C, D, R(66) );
    P( D, E, A, B, C, R(67) );
    P( C, D, E, A, B, R(68) );
    P( B, C, D, E, A, R(69) );
    P( A, B, C, D, E, R(70) );
    P( E, A, B, C, D, R(71) );
    P( D, E, A, B, C, R(72) );
    P( C, D, E, A, B, R(73) );
    P( B, C, D, E, A, R(74) );
    P( A, B, C, D, E, R(75) );
    P( E, A, B, C, D, R(76) );
    P( D, E, A, B, C, R(77) );
    P( C, D, E, A, B, R(78) );
    P( B, C, D, E, A, R(79) );

#undef K
#undef F

    ctx->state[0] += A;
    ctx->state[1] += B;
    ctx->state[2] += C;
    ctx->state[3] += D;
    ctx->state[4] += E;
}

/*
 * SHA-1 process buffer
 */
void sha1_update( sha1_ctx *ctx, const void *_input, ssize_t ilen )
{
    const byte *input = _input;
    int fill;
    uint32_t left;

    if( ilen <= 0 )
        return;

    left = ctx->total[0] & 0x3F;
    fill = 64 - left;

    ctx->total[0] += ilen;
    ctx->total[0] &= 0xFFFFFFFF;

    if( ctx->total[0] < (uint32_t) ilen )
        ctx->total[1]++;

    if( left && ilen >= fill )
    {
        memcpy( (void *) (ctx->buffer + left),
                (void *) input, fill );
        sha1_process( ctx, ctx->buffer );
        input += fill;
        ilen  -= fill;
        left = 0;
    }

    while( ilen >= 64 )
    {
        sha1_process( ctx, input );
        input += 64;
        ilen  -= 64;
    }

    if( ilen > 0 )
    {
        memcpy( (void *) (ctx->buffer + left),
                (void *) input, ilen );
    }
}

static const byte sha1_padding[64] =
{
 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * SHA-1 final digest
 */
void sha1_finish( sha1_ctx *ctx, byte output[20] )
{
    uint32_t last, padn;
    uint32_t high, low;
    byte msglen[8];

    high = ( ctx->total[0] >> 29 )
         | ( ctx->total[1] <<  3 );
    low  = ( ctx->total[0] <<  3 );

    PUT_U32_BE( high, msglen, 0 );
    PUT_U32_BE( low,  msglen, 4 );

    last = ctx->total[0] & 0x3F;
    padn = ( last < 56 ) ? ( 56 - last ) : ( 120 - last );

    sha1_update( ctx, (byte *) sha1_padding, padn );
    sha1_update( ctx, msglen, 8 );

    PUT_U32_BE( ctx->state[0], output,  0 );
    PUT_U32_BE( ctx->state[1], output,  4 );
    PUT_U32_BE( ctx->state[2], output,  8 );
    PUT_U32_BE( ctx->state[3], output, 12 );
    PUT_U32_BE( ctx->state[4], output, 16 );
}

void sha1_finish_hex( sha1_ctx *ctx, char output[41] )
{
    byte digest[20];
    sha1_finish(ctx, digest);
    strconv_hexencode(output, 41, digest, SHA1_DIGEST_SIZE);
}

/*
 * output = SHA-1( input buffer )
 */
void sha1( const void *input, ssize_t ilen, byte output[20] )
{
    sha1_ctx ctx;

    sha1_starts( &ctx );
    sha1_update( &ctx, input, ilen );
    sha1_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha1_ctx ) );
}

uint64_t sha1_hash_64(const void *data, ssize_t len)
{
    union {
        byte b[20];
        uint64_t u[2];
    } res;

    sha1(data, len, res.b);
    return res.u[0] ^ res.u[1];
}

/*
 * output = SHA-1( input buffer )
 */
void sha1_hex(const void *input, ssize_t ilen, char output[41])
{
    sha1_ctx ctx;

    sha1_starts(&ctx);
    sha1_update(&ctx, input, ilen);
    sha1_finish_hex(&ctx, output);

    memset(&ctx, 0, sizeof(sha1_ctx));
}

/*
 * output = SHA-1( file contents )
 */
int sha1_file( char *path, byte output[20] )
{
    FILE *f;
    size_t n;
    sha1_ctx ctx;
    byte buf[1024];

    if( ( f = fopen( path, "rb" ) ) == NULL )
        return( 1 );

    sha1_starts( &ctx );

    while( ( n = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
        sha1_update( &ctx, buf, (int) n );

    sha1_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha1_ctx ) );

    if( ferror( f ) != 0 )
    {
        fclose( f );
        return( 2 );
    }

    fclose( f );
    return( 0 );
}

/*
 * SHA-1 HMAC context setup
 */
void sha1_hmac_starts( sha1_ctx *ctx, const void *_key, int keylen )
{
    const byte *key = _key;
    int i;
    byte sum[20];

    if( keylen > 64 )
    {
        sha1( key, keylen, sum );
        keylen = 20;
        key = sum;
    }

    memset( ctx->ipad, 0x36, 64 );
    memset( ctx->opad, 0x5C, 64 );

    for( i = 0; i < keylen; i++ )
    {
        ctx->ipad[i] = (byte)( ctx->ipad[i] ^ key[i] );
        ctx->opad[i] = (byte)( ctx->opad[i] ^ key[i] );
    }

    sha1_starts( ctx );
    sha1_update( ctx, ctx->ipad, 64 );

    memset( sum, 0, sizeof( sum ) );
}

/*
 * SHA-1 HMAC process buffer
 */
void sha1_hmac_update( sha1_ctx *ctx, const void *input, ssize_t ilen )
{
    sha1_update( ctx, input, ilen );
}

/*
 * SHA-1 HMAC final digest
 */
void sha1_hmac_finish( sha1_ctx *ctx, byte output[20] )
{
    byte tmpbuf[20];

    sha1_finish( ctx, tmpbuf );
    sha1_starts( ctx );
    sha1_update( ctx, ctx->opad, 64 );
    sha1_update( ctx, tmpbuf, 20 );
    sha1_finish( ctx, output );

    memset( tmpbuf, 0, sizeof( tmpbuf ) );
}

/*
 * output = HMAC-SHA-1( hmac key, input buffer )
 */
void sha1_hmac( const void *key, int keylen,
                const void *input, ssize_t ilen,
                byte output[20] )
{
    sha1_ctx ctx;

    sha1_hmac_starts( &ctx, key, keylen );
    sha1_hmac_update( &ctx, input, ilen );
    sha1_hmac_finish( &ctx, output );

    memset( &ctx, 0, sizeof( sha1_ctx ) );
}

/*
 * FIPS-180-1 test vectors
 */
static byte sha1_test_buf[3][57] =
{
    { "abc" },
    { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" },
    { "" }
};

static const int sha1_test_buflen[3] =
{
    3, 56, 1000
};

static const byte sha1_test_sum[3][20] =
{
    { 0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
      0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D },
    { 0x84, 0x98, 0x3E, 0x44, 0x1C, 0x3B, 0xD2, 0x6E, 0xBA, 0xAE,
      0x4A, 0xA1, 0xF9, 0x51, 0x29, 0xE5, 0xE5, 0x46, 0x70, 0xF1 },
    { 0x34, 0xAA, 0x97, 0x3C, 0xD4, 0xC4, 0xDA, 0xA4, 0xF6, 0x1E,
      0xEB, 0x2B, 0xDB, 0xAD, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6F }
};

/*
 * RFC 2202 test vectors
 */
static byte sha1_hmac_test_key[7][26] =
{
    {(
      "\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B\x0B"
      "\x0B\x0B\x0B\x0B"
    )},
    { "Jefe" },
    {(
      "\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA\xAA"
      "\xAA\xAA\xAA\xAA"
    )},
    {(
      "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10"
      "\x11\x12\x13\x14\x15\x16\x17\x18\x19"
    )},
    {(
      "\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C\x0C"
      "\x0C\x0C\x0C\x0C"
    )},
    { "" }, /* 0xAA 80 times */
    { "" }
};

static const int sha1_hmac_test_keylen[7] =
{
    20, 4, 20, 25, 20, 80, 80
};

static byte sha1_hmac_test_buf[7][74] =
{
    { "Hi There" },
    { "what do ya want for nothing?" },
    {(
      "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
      "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
      "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
      "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
      "\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD\xDD"
    )},
    {(
      "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
      "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
      "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
      "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
      "\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD"
    )},
    { "Test With Truncation" },
    { "Test Using Larger Than Block-Size Key - Hash Key First" },
    {(
      "Test Using Larger Than Block-Size Key and Larger"
      " Than One Block-Size Data"
    )}
};

static const int sha1_hmac_test_buflen[7] =
{
    8, 28, 50, 50, 20, 54, 73
};

static const byte sha1_hmac_test_sum[7][20] =
{
    { 0xB6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xE2, 0x8B,
      0xC0, 0xB6, 0xFB, 0x37, 0x8C, 0x8E, 0xF1, 0x46, 0xBE, 0x00 },
    { 0xEF, 0xFC, 0xDF, 0x6A, 0xE5, 0xEB, 0x2F, 0xA2, 0xD2, 0x74,
      0x16, 0xD5, 0xF1, 0x84, 0xDF, 0x9C, 0x25, 0x9A, 0x7C, 0x79 },
    { 0x12, 0x5D, 0x73, 0x42, 0xB9, 0xAC, 0x11, 0xCD, 0x91, 0xA3,
      0x9A, 0xF4, 0x8A, 0xA1, 0x7B, 0x4F, 0x63, 0xF1, 0x75, 0xD3 },
    { 0x4C, 0x90, 0x07, 0xF4, 0x02, 0x62, 0x50, 0xC6, 0xBC, 0x84,
      0x14, 0xF9, 0xBF, 0x50, 0xC8, 0x6C, 0x2D, 0x72, 0x35, 0xDA },
    { 0x4C, 0x1A, 0x03, 0x42, 0x4B, 0x55, 0xE0, 0x7F, 0xE7, 0xF2,
      0x7B, 0xE1 },
    { 0xAA, 0x4A, 0xE5, 0xE1, 0x52, 0x72, 0xD0, 0x0E, 0x95, 0x70,
      0x56, 0x37, 0xCE, 0x8A, 0x3B, 0x55, 0xED, 0x40, 0x21, 0x12 },
    { 0xE8, 0xE9, 0x9D, 0x0F, 0x45, 0x23, 0x7D, 0x78, 0x6D, 0x6B,
      0xBA, 0xA7, 0x96, 0x5C, 0x78, 0x08, 0xBB, 0xFF, 0x1A, 0x91 }
};

/*
 * Checkup routine
 */
Z_GROUP_EXPORT(sha1)
{
    Z_TEST(hash, "") {
        byte buf[1024];
        byte sha1sum[20];
        sha1_ctx ctx;

        for (int i = 0; i < 3; i++) {
            sha1_starts( &ctx );
            if (i == 2) {
                memset(buf, 'a', 1000);
                for (int j = 0; j < 1000; j++)
                    sha1_update(&ctx, buf, 1000);
            } else {
                sha1_update(&ctx, sha1_test_buf[i], sha1_test_buflen[i]);
            }
            sha1_finish( &ctx, sha1sum );

            Z_ASSERT_EQUAL(sha1sum, 20, sha1_test_sum[i], 20);
        }
    } Z_TEST_END;

    Z_TEST(hmac, "") {
        byte buf[1024];
        byte sha1sum[20];
        sha1_ctx ctx;

        for (int i = 0; i < 7; i++) {
            if (i == 5 || i == 6) {
                memset(buf, '\xAA', 80);
                sha1_hmac_starts(&ctx, buf, 80);
            } else {
                sha1_hmac_starts(&ctx, sha1_hmac_test_key[i],
                                 sha1_hmac_test_keylen[i]);
            }
            sha1_hmac_update(&ctx, sha1_hmac_test_buf[i],
                             sha1_hmac_test_buflen[i]);
            sha1_hmac_finish(&ctx, sha1sum);

            if (i == 4) {
                Z_ASSERT_EQUAL(sha1sum, 12, sha1_hmac_test_sum[i], 12);
            } else {
                Z_ASSERT_EQUAL(sha1sum, 20, sha1_hmac_test_sum[i], 20);
            }
        }
    } Z_TEST_END;
} Z_GROUP_END
