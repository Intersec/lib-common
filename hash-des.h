/**
 * \file des.h
 */
#ifndef XYSSL_DES_H
#define XYSSL_DES_H

#define DES_ENCRYPT     1
#define DES_DECRYPT     0

/**
 * \brief          DES context structure
 */
typedef struct {
    int mode;              /*!<  encrypt/decrypt   */
    uint32_t sk[32];       /*!<  DES subkeys       */
} des_ctx;

/**
 * \brief          Triple-DES context structure
 */
typedef struct {
    int mode;              /*!<  encrypt/decrypt   */
    uint32_t sk[96];       /*!<  3DES subkeys      */
} des3_ctx;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief          DES key schedule (56-bit, encryption)
 *
 * \param ctx      DES context to be initialized
 * \param key      8-byte secret key
 */
void des_setkey_enc(des_ctx * nonnull ctx, const byte key[8])
    __leaf;

/**
 * \brief          DES key schedule (56-bit, decryption)
 *
 * \param ctx      DES context to be initialized
 * \param key      8-byte secret key
 */
void des_setkey_dec(des_ctx * nonnull ctx, const byte key[8])
    __leaf;

/**
 * \brief          Triple-DES key schedule (112-bit, encryption)
 *
 * \param ctx      3DES context to be initialized
 * \param key      16-byte secret key
 */
void des3_set2key_enc(des3_ctx * nonnull ctx, const byte key[16])
    __leaf;

/**
 * \brief          Triple-DES key schedule (112-bit, decryption)
 *
 * \param ctx      3DES context to be initialized
 * \param key      16-byte secret key
 */
void des3_set2key_dec(des3_ctx * nonnull ctx, const byte key[16])
    __leaf;

/**
 * \brief          Triple-DES key schedule (168-bit, encryption)
 *
 * \param ctx      3DES context to be initialized
 * \param key      24-byte secret key
 */
void des3_set3key_enc(des3_ctx * nonnull ctx, const byte key[24])
    __leaf;

/**
 * \brief          Triple-DES key schedule (168-bit, decryption)
 *
 * \param ctx      3DES context to be initialized
 * \param key      24-byte secret key
 */
void des3_set3key_dec(des3_ctx * nonnull ctx, const byte key[24])
    __leaf;

/**
 * \brief          DES-ECB block encryption/decryption
 *
 * \param ctx      DES context
 * \param input    64-bit input block
 * \param output   64-bit output block
 */
void des_crypt_ecb(des_ctx * nonnull ctx, const byte input[8], byte output[8])
    __leaf;

/**
 * \brief          DES-CBC buffer encryption/decryption
 *
 * \param ctx      DES context
 * \param mode     DES_ENCRYPT or DES_DECRYPT
 * \param length   length of the input data
 * \param iv       initialization vector (updated after use)
 * \param input    buffer holding the input data
 * \param output   buffer holding the output data
 */
void des_crypt_cbc(des_ctx * nonnull ctx, int mode, int length,
                   byte iv[8], const byte * nonnull input,
                   byte * nonnull output)
    __leaf;

/**
 * \brief          3DES-ECB block encryption/decryption
 *
 * \param ctx      3DES context
 * \param input    64-bit input block
 * \param output   64-bit output block
 */
void des3_crypt_ecb(des3_ctx * nonnull ctx, const byte input[8],
                    byte output[8])
    __leaf;

/**
 * \brief          3DES-CBC buffer encryption/decryption
 *
 * \param ctx      3DES context
 * \param mode     DES_ENCRYPT or DES_DECRYPT
 * \param length   length of the input data; must be a multiple of 8 -- use
 *                 PKCS#7 padding if needed, see sb_add_pkcs7_8_bytes_padding
 * \param iv       initialization vector (updated after use)
 * \param input    buffer holding the input data
 * \param output   buffer holding the output data
 */
void des3_crypt_cbc(des3_ctx * nonnull ctx, int mode, int length,
                    byte iv[8], const byte * nonnull input,
                    byte * nonnull output)
    __leaf;

#ifdef __cplusplus
}
#endif

#endif /* des.h */
