/**
 * \file sha4.h
 */
#ifndef XYSSL_SHA4_H
#define XYSSL_SHA4_H

#if defined(_MSC_VER) || defined(__WATCOMC__)
  #define UL64(x) x##ui64
#else
  #define UL64(x) x##ULL
#endif

/**
 * \brief          SHA-512 context structure
 */
typedef struct {
    uint64_t total[2]; /*!< number of bytes processed  */
    uint64_t state[8]; /*!< intermediate digest state  */
    byte buffer[128];  /*!< data block being processed */

    byte ipad[128];    /*!< HMAC: inner padding        */
    byte opad[128];    /*!< HMAC: outer padding        */
    int is384;         /*!< 0 => SHA-512, else SHA-384 */
} sha4_ctx;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief          SHA-512 context setup
 *
 * \param ctx      context to be initialized
 * \param is384    0 = use SHA512, 1 = use SHA384
 */
void sha4_starts(sha4_ctx * nonnull ctx, int is384) __leaf;

/**
 * \brief          SHA-512 process buffer
 *
 * \param ctx      SHA-512 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void sha4_update(sha4_ctx * nonnull ctx, const void * nonnull input,
                 ssize_t ilen) __leaf;

/**
 * \brief          SHA-512 final digest
 *
 * \param ctx      SHA-512 context
 * \param output   SHA-384/512 checksum result
 */
void sha4_finish(sha4_ctx * nonnull ctx, byte output[64]) __leaf;

/**
 * \brief          Output = SHA-512(input buffer)
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   SHA-384/512 checksum result
 * \param is384    0 = use SHA512, 1 = use SHA384
 */
void sha4(const void *nonnull input, ssize_t ilen, byte output[64],
          int is384) __leaf;

/**
 * \brief          Output = SHA-512(file contents)
 *
 * \param path     input file name
 * \param output   SHA-384/512 checksum result
 * \param is384    0 = use SHA512, 1 = use SHA384
 *
 * \return         0 if successful, 1 if fopen failed,
 *                 or 2 if fread failed
 */
int sha4_file(const char * nonnull path, byte output[64], int is384) __leaf;

/**
 * \brief          SHA-512 HMAC context setup
 *
 * \param ctx      HMAC context to be initialized
 * \param is384    0 = use SHA512, 1 = use SHA384
 * \param key      HMAC secret key
 * \param keylen   length of the HMAC key
 */
void sha4_hmac_starts(sha4_ctx * nonnull ctx, const void * nonnull key,
                      int keylen, int is384) __leaf;

/**
 * \brief          SHA-512 HMAC process buffer
 *
 * \param ctx      HMAC context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void sha4_hmac_update(sha4_ctx * nonnull ctx, const void * nonnull input,
                      ssize_t ilen) __leaf;

/**
 * \brief          SHA-512 HMAC final digest
 *
 * \param ctx      HMAC context
 * \param output   SHA-384/512 HMAC checksum result
 */
void sha4_hmac_finish(sha4_ctx * nonnull ctx, byte output[64]) __leaf;

/**
 * \brief          Output = HMAC-SHA-512(hmac key, input buffer)
 *
 * \param key      HMAC secret key
 * \param keylen   length of the HMAC key
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   HMAC-SHA-384/512 result
 * \param is384    0 = use SHA512, 1 = use SHA384
 */
void sha4_hmac(const void * nonnull key, int keylen,
               const void * nonnull input, ssize_t ilen,
               byte output[64], int is384) __leaf;

#ifdef __cplusplus
}
#endif

#endif /* sha4.h */
