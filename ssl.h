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

#ifndef IS_LIB_COMMON_SSL_H
#define IS_LIB_COMMON_SSL_H

/**
 * This file offers an interface which wrap the openssl AES-CBC & PKEY
 * interfaces.
 *
 * \section AES-CBS
 *
 * The generic way to performs encrypt is to get an SSL context using one of
 * the initializer, then update() the encrypted data as much as you want and
 * finally call the finish() function to close the flow. Using the reset()
 * operation you could repeat these operations several times. Example:
 *
 * // Get the SSL context
 * init_aes256();
 *
 * // Encrypt some data
 * update();
 * update();
 * update();
 *
 * // Prepare to encrypt some others data
 * reset();
 *
 * // Encrypt the others data
 * update();
 * update();
 *
 * // Close the flow and wipe the SSL context
 * finish();
 * wipe();
 *
 * Decryption works in the same way.
 *
 * \section PKEY
 *
 * \subsection encryption
 *
 * The generic way to performs encryption is to init the SSL context with your
 * public key (or private if you have access to it, although it's not required
 * for encryption), call encrypt() on your data, and wipe the context.
 *
 * Example:
 *
 * ssl_ctx_t ctx;
 * if (!ssl_ctx_init_pkey(&ctx, LSTR_NULL_V, pub_key, LSTR_NULL_V)) {
 *     return logger_error(… "key loading error: %s", ssl_get_error());
 * }
 * if (ssl_encrypt(&ctx, msg, &msg_encrypted) < 0) {
 *     ssl_ctx_wipe(&ctx);
 *     return logger_error(… "decrypt error: %s", ssl_get_error());
 * }
 * ssl_ctx_wipe(&ctx);
 *
 * \subsection decryption
 *
 * Decryption works in the same way, except that you need the private key and
 * the passphrase if this key have been created with one.
 *
 * Example:
 *
 * ssl_ctx_t ctx;
 * if (!ssl_ctx_init_pkey(&ctx, priv_key, LSTR_NULL_V, pass)) {
 *     return logger_error(… "key loading error: %s", ssl_get_error());
 * }
 * if (ssl_decrypt(&ctx, msg_encrypted, &msg_clear) < 0) {
 *     ssl_ctx_wipe(&ctx);
 *     return logger_error(… "decrypt error: %s", ssl_get_error());
 * }
 * ssl_ctx_wipe(&ctx);
 *
 */

#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#include "core.h"

#define OPENSSL_VERSION_IS(op, maj1, maj2, min)                              \
    (((OPENSSL_VERSION_NUMBER >> 12) & 0xFFFFF) op (((maj1) << 16)           \
                                                    | ((maj2) << 8)          \
                                                    | (min)))
#if OPENSSL_VERSION_IS(<,1,1,0)
#define TLS_method()  SSLv23_method()
#define TLS_server_method()  SSLv23_server_method()
#define TLS_client_method()  SSLv23_client_method()
#endif
#if OPENSSL_VERSION_IS(<,1,0,2)
/* Note: this function is also removed after 1.1.0, so please remove all its
 * occurrences if you remove this compatibility code. */
#define SSL_CTX_set_ecdh_auto(ctx, onoff)  do {} while(0)
#endif

#ifndef RSA_OAEP_PADDING_SIZE
/* RSA can just encrypt a message as large as the key, i.e. 256 bytes for a
 * 2048 key. But the algorithm used some random padding to increase security.
 * Thus, RSA encrypt message up to key_size - PADDING bytes.
 *
 * Sadly enough, if there is a RSA_PKCS1_PADDING_SIZE constant defined for the
 * old PKCS1 method, I didn't found an equivalent way to get the maximum
 * padding size used with OAEP -- which seems to be the current standard. The
 * RSA_public_encrypt(3) man page says it's 41 bytes.
 */
# define RSA_OAEP_PADDING_SIZE  41
#endif

/* Encryption {{{ */

enum ssl_ctx_state {
    SSL_CTX_NONE,
    SSL_CTX_INIT,
    SSL_CTX_UPDATE,
    SSL_CTX_FINISH,
};

/**
 * SSL context used to encrypt and decrypt data.
 */
typedef struct ssl_ctx_t {
    EVP_CIPHER_CTX     *encrypt;
    EVP_CIPHER_CTX     *decrypt;

    /* PKEY data */
    EVP_PKEY          *pkey;
    EVP_PKEY_CTX      *pkey_encrypt;
    EVP_PKEY_CTX      *pkey_decrypt;

    /* common data */
    enum ssl_ctx_state encrypt_state;
    enum ssl_ctx_state decrypt_state;
} ssl_ctx_t;

void ssl_ctx_wipe(ssl_ctx_t *ctx);
GENERIC_DELETE(ssl_ctx_t, ssl_ctx);

/**
 * Init the SSL context with a given password and an optional salt. This
 * initializer will use AES 256 with SHA256.
 *
 * The password, salt and nb_rounds arguments are used to derive the AES key
 * and initialisation vector.
 *
 * \param ctx       The SSL context.
 * \param password  The password.
 * \param salt      The salt to use when encrypting.
 * \param nb_rounds The iteration count to use, changing this value will break
 *                  encryption/decryption compatibility (a value of 1024
 *                  should be good in most situations).
 *
 * \return The initialized AES context or NULL in case of error.
 */
ssl_ctx_t *ssl_ctx_init_aes256(ssl_ctx_t *ctx, lstr_t password, uint64_t salt,
                               int nb_rounds);

/**
 * Same as ssl_ctx_init_aes() but allocate the ssl_ctx_t for you.
 */
static inline ssl_ctx_t *
ssl_ctx_new_aes256(lstr_t password, uint64_t salt, int nb_rounds)
{
    ssl_ctx_t *ctx = p_new_raw(ssl_ctx_t, 1);

    if (unlikely(!ssl_ctx_init_aes256(ctx, password, salt, nb_rounds))) {
        p_delete(&ctx);
    }

    return ctx;
}

/** Init AES 256 context with the symmetric key.
 *
 * \param  ctx  The SSL context.
 * \param  key  The AES 256bits key.
 * \param  iv  The initialisation vector.
 * \return The initialiased AES context or NULL in case of error.
 */
ssl_ctx_t *ssl_ctx_init_aes256_by_key(ssl_ctx_t *ctx, lstr_t key, lstr_t iv);

/** Init AES 128 ECB context with the symmetric key.
 *
 * \param  ctx  The SSL context.
 * \param  key  The AES 128bits key.
 * \return The initialiased AES context or NULL in case of error.
 */
ssl_ctx_t *
ssl_ctx_init_aes128_ecb_by_key(ssl_ctx_t *ctx, lstr_t key);

/**
 * Reset the whole SSL context and change the AES key and IV.
 *
 * The key and the initialisation vector are derived from the given
 * password, salt and nb_rounds parameters.
 *
 * The context is not wiped on error.
 *
 * \param  ctx        The SSL context.
 * \param  password   The password.
 * \param  salt       The salt to use when encrypting.
 * \param  nb_rounds  The iteration count to use, changing this value will
 *                    break encryption/decryption compatibility (a value of
 *                    1024 should be good in most situations).
 * \return 0 on success and -1 on error.
 */
int ssl_ctx_reset(ssl_ctx_t *ctx, lstr_t password, uint64_t salt,
                  int nb_rounds);

/**
 * Init the given SSL context with the given key. You can use private or
 * public key to init the context, depending on what you need.
 *
 * \param ctx       The SSL context.
 * \param priv_key  The private key.
 * \param pub_key   The public key.
 * \param pass      The passphrase.
 *
 * \return The initialized context or NULL in case of error.
 */
__attr_nonnull__((1))
ssl_ctx_t *ssl_ctx_init_pkey(ssl_ctx_t *ctx,
                             lstr_t priv_key, lstr_t pub_key,
                             lstr_t pass);

/**
 * Init the given SSL context with the public key.
 *
 * \warning You won't be able to decrypt data using this context.
 *
 * \param ctx       The SSL context.
 * \param pub_key   The public key.
 *
 * \return The initialized context or NULL in case of error.
 */
__attr_nonnull__((1)) static inline
ssl_ctx_t *ssl_ctx_init_pkey_pub(ssl_ctx_t *ctx, lstr_t pub_key)
{
    return ssl_ctx_init_pkey(ctx, LSTR_EMPTY_V, pub_key, LSTR_EMPTY_V);

}

/**
 * Init the given SSL context with the private key and the passphrase.
 *
 * You don't need to call ssl_ctx_init() it will be done for you.
 *
 * \param ctx       The SSL context.
 * \param priv_key  The private key.
 * \param pass      The passphrase.
 *
 * \return The initialized context or NULL in case of error.
 */
__attr_nonnull__((1)) static inline
ssl_ctx_t *ssl_ctx_init_pkey_priv(ssl_ctx_t *ctx,
                                  lstr_t priv_key, lstr_t pass)
{
    return ssl_ctx_init_pkey(ctx, priv_key, LSTR_EMPTY_V, pass);
}

/**
 * Retrieve the last SSL error in the current thread.
 */
const char *ssl_get_error(void);

/**
 * Encrypt the given data and put the result in out.
 */
__must_check__ int ssl_encrypt_update(ssl_ctx_t *ctx, lstr_t data, sb_t *out);

/**
 * Finalize the encrypted buffer.
 */
__must_check__ int ssl_encrypt_finish(ssl_ctx_t *ctx, sb_t *out);

/**
 * Reset the SSL context for the next data to encrypt using the same salt as
 * preceding. This function will call ssl_encrypt_finish() if needed. The out
 * parameter is mandatory in this case.
 */
__must_check__ int ssl_encrypt_reset(ssl_ctx_t *ctx, sb_t *out);

/**
 * Encrypt an arbitrarily long lstr_t.
 *
 * Encrypt the data with a RSA key. If the data is too large to fit in a
 * single RSA message, we use hybrid cryptography (RSA + AES). The format is
 * described in the code.
 *
 * \param[in]  ctx  An ssl context initialized with an RSA key.
 * \param[in]  data  The data to encrypt.
 * \param[out]  out  The buffer where encrypted data are accumulated.
 * \return 0 on success, -1 on error. You can call ssl_get_error() on error to
 *         have stringified informations.
 */
__must_check__ int
ssl_encrypt_pkey_sb(ssl_ctx_t *ctx, lstr_t data, sb_t *out);

/**
 * Encrypt an arbitrarily long lstr_t.
 *
 * \param[in]  ctx  An ssl context initialized with an RSA key.
 * \param[in]  data  The data to encrypt.
 * \return the encrypted data as a t-stack allocated lstr_t (or LSTR_NULL_V on
 *         error). You can call ssl_get_error() on error to have stringified
 *         informations.
 */
static inline lstr_t t_ssl_encrypt_pkey_lstr(ssl_ctx_t *ctx, lstr_t data)
{
    SB_1k(out);

    /* grow(data + AES key + AES IV + AES padding + RSA padding). */
    sb_grow(&out, data.len + 32 + 16 + 16 + RSA_OAEP_PADDING_SIZE);
    THROW_IF(ssl_encrypt_pkey_sb(ctx, data, &out) < 0, LSTR_NULL_V);
    return t_lstr_dup(LSTR_SB_V(&out));
}

/**
 * Encrypt a bunch of data in one operation. The SSL context will be ready to
 * be updated again.
 */
__must_check__ static inline int
ssl_encrypt(ssl_ctx_t *ctx, lstr_t data, sb_t *out)
{
    if (ctx->pkey) {
        RETHROW(ssl_encrypt_pkey_sb(ctx, data, out));
        return 0;
    }
    RETHROW(ssl_encrypt_update(ctx, data, out));
    RETHROW(ssl_encrypt_reset(ctx, out));
    return 0;
}


/**
 * Decrypt the given data and put the result in out.
 */
__must_check__ int ssl_decrypt_update(ssl_ctx_t *ctx, lstr_t data, sb_t *out);

/**
 * Finalize the decrypted buffer.
 */
__must_check__ int ssl_decrypt_finish(ssl_ctx_t *ctx, sb_t *out);

/**
 * Reset the SSL context for the next data to decrypt using the same salt as
 * preceding. This function will call ssl_decrypt_finish() if needed. The out
 * parameter is mandatory in this case.
 */
__must_check__ int ssl_decrypt_reset(ssl_ctx_t *ctx, sb_t *out);

/**
 * Decrypt arbitrarily long lstr_t.
 *
 * \param[in]  ctx  An ssl context initialized with an RSA private key.
 * \param[in]  data  The data to decrypt.
 * \param[out]  out  The buffer where decrypted data are accumulated.
 * \return 0 on success, -1 on error. You can call ssl_get_error() on error to
 *         have stringified informations.
 */
__must_check__ int
ssl_decrypt_pkey_sb(ssl_ctx_t *ctx, lstr_t data, sb_t *out);

/**
 * Decrypt an arbitrarily long lstr_t.
 *
 * \param[in]  ctx  An ssl context initialized with an RSA private key.
 * \param[in]  data  The data to decrypt.
 * \return the decrypted data as a t-stack allocated lstr_t (or LSTR_NULL_V on
 *         error). You can call ssl_get_error() on error to have stringified
 *         informations.
 */
static inline lstr_t t_ssl_decrypt_pkey_lstr(ssl_ctx_t *ctx, lstr_t data)
{
    int min_aes_data = (data.len == 256 ? 0 : 32 + 16);
    t_SB(out, data.len + 1 - RSA_OAEP_PADDING_SIZE - min_aes_data);

    THROW_IF(ssl_decrypt_pkey_sb(ctx, data, &out) < 0, LSTR_NULL_V);
    return LSTR_SB_V(&out);
}

/**
 * Decrypt a bunch of data in one operation. The SSL context will be ready to
 * be updated again.
 */
__must_check__ static inline int
ssl_decrypt(ssl_ctx_t *ctx, lstr_t data, sb_t *out)
{
    if (ctx->pkey) {
        RETHROW(ssl_decrypt_pkey_sb(ctx, data, out));
        return 0;
    }
    RETHROW(ssl_decrypt_update(ctx, data, out));
    RETHROW(ssl_decrypt_reset(ctx, out));
    return 0;
}

/* }}} */
/* {{{ Signature */

#ifdef __has_blocks
typedef int (BLOCK_CARET pem_password_b)(char *buf, int size, int rwflag);
#else
typedef void *pem_password_b;
#endif

typedef enum rsa_hash_algo_t {
    RSA_HASH_SHA256,
} rsa_hash_algo_t;

typedef struct rsa_sign_t rsa_sign_t;

rsa_sign_t * nullable rsa_sign_new(lstr_t priv_key, rsa_hash_algo_t algo,
                                   pem_password_b nullable pass_cb);

void rsa_sign_update(rsa_sign_t * nonnull ctx, const void * nonnull input,
                     ssize_t ilen);

__must_check__
int rsa_sign_finish(rsa_sign_t * nonnull * nonnull ctx, sb_t *out);

__must_check__
int rsa_sign_finish_hex(rsa_sign_t * nonnull * nonnull ctx, sb_t *out);


typedef struct rsa_verif_t rsa_verif_t;

__must_check__
rsa_verif_t * nullable rsa_verif_new(lstr_t pub_key, rsa_hash_algo_t algo,
                                     lstr_t bin_sig,
                                     pem_password_b nullable pass_cb);

__must_check__
rsa_verif_t * nullable rsa_verif_hex_new(lstr_t pub_key, rsa_hash_algo_t algo,
                                         lstr_t hex_sig,
                                         pem_password_b nullable pass_cb);

void rsa_verif_update(rsa_verif_t * nonnull ctx, const void * nonnull input,
                      ssize_t ilen);

__must_check__
int rsa_verif_finish(rsa_verif_t * nonnull * nonnull ctx);

/* }}} */
/* {{{ TLS */

/** Load a certificate into the SSL_CTX.
 *
 * A wrapper of SSL_CTX_use_certificate_file for lstr.
 *
 * \param[in]  ctx  The SSL_CTX to enrich.
 * \param[in]  key  The certificate in PEM format.
 * \return 0 on success and -1 on error.
 */
int ssl_ctx_use_certificate_lstr(SSL_CTX *ctx, lstr_t cert);

/** Load a private key into the SSL_CTX.
 *
 * A wrapper of SSL_CTX_use_PrivateKey for lstr.
 *
 * \param[in]  ctx  The SSL_CTX to enrich.
 * \param[in]  key  The private key in PEM format.
 * \return 0 on success and -1 on error.
 */
int ssl_ctx_use_privatekey_lstr(SSL_CTX *ctx, lstr_t key);

/** Wrapper to SSL_read that mimic read(2).
 *
 * \param[in]  ssl  The ssl context for which data must be received.
 * \param[in]  buf  The buffer into which data are received.
 * \param[in]  len  The maximum number of bytes to receive into `buf`.
 * \return the number of bytes sent.
 */
ssize_t ssl_read(SSL *ssl, void *buf, size_t len);

/** Wrapper to SSL_write that mimic write(2).
 *
 * \param[in]  ssl  The ssl context for which data must be sent. It must be
 *                  configured to allow partial write. (See
 *                  SSL_MODE_ENABLE_PARTIAL_WRITE and
 *                  SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER options).
 * \param[in]  buf  The buffer to write.
 * \param[in]  len  The number of bytes to send from `buf`.
 * \return the number of bytes sent.
 */
ssize_t ssl_write(SSL *ssl, const void *buf, size_t len);

/** A writev-like callback using SSL_write.
 *
 * The priv argument must be the corresponding SSL* structure.
 *
 * This function assumes that the ssl context (i.e. the `priv` argument) is
 * configured to allow partial write (see SSL_MODE_ENABLE_PARTIAL_WRITE and
 * SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER).
 *
 * \param[in]  fd  The socket from which data are sent; this argument is
 *                 unused: the ssl context (`priv`) is used instead.
 * \param[in]  iov  The buffers to write.
 * \param[in]  iovcnt  The number of iov buffers.
 * \param[in]  priv  A pointer to the corresponding SSL structure.
 * \return the number of bytes sent.
 */
ssize_t ssl_writev(int fd, const struct iovec *iov, int iovcnt, void *priv);

/* A sb_read function for reading TLS connections.
 *
 * \param[in]  sb  The string buffer for receiving data.
 * \param[in]  ssl  The SSL context associated to the connection.
 * \param[in]  hint  Expected number of bytes received. Defaults to BUFSIZ
 *                   when hint is 0.
 * \return the number of bytes read on success, and -1 on error. If an error
 *         because the socket would block, errno is set to EAGAIN.
 */
ssize_t ssl_sb_read(sb_t *sb, SSL *ssl, int hint);

/* }}} */
/* Module {{{ */

MODULE_DECLARE(ssl);

/* }}} */
#endif
