/**
 * \file md5.h
 */
#ifndef XYSSL_MD5_H
#define XYSSL_MD5_H

/**
 * \brief          MD5 context structure
 */
typedef struct {
    uint32_t total[2]; /*!< number of bytes processed  */
    uint32_t state[4]; /*!< intermediate digest state  */
    byte buffer[64];   /*!< data block being processed */

    byte ipad[64];     /*!< HMAC: inner padding        */
    byte opad[64];     /*!< HMAC: outer padding        */
} md5_ctx;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief          MD5 context setup
 *
 * \param ctx      context to be initialized
 */
void md5_starts(md5_ctx * nonnull ctx) __leaf;

/**
 * \brief          MD5 process buffer
 *
 * \param ctx      MD5 context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void md5_update(md5_ctx * nonnull ctx, const void * nonnull input, ssize_t ilen)
    __leaf;

/**
 * \brief          MD5 final digest
 *
 * \param ctx      MD5 context
 * \param output   MD5 checksum result
 */
void md5_finish(md5_ctx * nonnull ctx, byte output[16]) __leaf;

/**
 * \brief          MD5 final digest
 *
 * \param ctx      MD5 context
 * \param output   MD5 checksum result
 */
void md5_finish_hex(md5_ctx * nonnull ctx, char output[33]) __leaf;

/**
 * \brief          Output = MD5(input buffer)
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   MD5 checksum result
 */
void md5(const void * nonnull input, ssize_t ilen, byte output[16]) __leaf;

/* \brief          64-bit output = MD5(input buffer)
 *
 * \param data     buffer holding the data
 * \param len      length of the input data
 */
uint64_t md5_hash_64(const void * nonnull data, ssize_t len) __leaf;

/**
 * \brief          Output = MD5(input buffer)
 *
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   MD5 checksum result
 */
void md5_hex(const void * nonnull input, ssize_t ilen, char output[33]) __leaf;

/**
 * \brief          Output = MD5(file contents)
 *
 * \param path     input file name
 * \param output   MD5 checksum result
 *
 * \return         0 if successful, 1 if fopen failed,
 *                 or 2 if fread failed
 */
int md5_file(char * nonnull path, byte output[16]) __leaf;

/**
 * \brief          MD5 HMAC context setup
 *
 * \param ctx      HMAC context to be initialized
 * \param key      HMAC secret key
 * \param keylen   length of the HMAC key
 */
void md5_hmac_starts(md5_ctx * nonnull ctx, const void * nonnull key,
                     int keylen) __leaf;

/**
 * \brief          MD5 HMAC process buffer
 *
 * \param ctx      HMAC context
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 */
void md5_hmac_update(md5_ctx * nonnull ctx, const void * nonnull input,
                     ssize_t ilen) __leaf;

/**
 * \brief          MD5 HMAC final digest
 *
 * \param ctx      HMAC context
 * \param output   MD5 HMAC checksum result
 */
void md5_hmac_finish(md5_ctx * nonnull ctx, byte output[16]) __leaf;

/**
 * \brief          Output = HMAC-MD5(hmac key, input buffer)
 *
 * \param key      HMAC secret key
 * \param keylen   length of the HMAC key
 * \param input    buffer holding the  data
 * \param ilen     length of the input data
 * \param output   HMAC-MD5 result
 */
void md5_hmac(const void * nonnull key, int keylen,
              const void * nonnull input, ssize_t ilen,
              byte output[16]) __leaf;

#ifdef __cplusplus
}
#endif

#endif /* md5.h */
