/**
 * \file padlock.h
 */
#ifndef XYSSL_PADLOCK_H
#define XYSSL_PADLOCK_H

#if (defined(__GNUC__) && defined(__i386__))

#ifndef XYSSL_HAVE_X86
#define XYSSL_HAVE_X86
#endif

#define PADLOCK_RNG 0x000C
#define PADLOCK_ACE 0x00C0
#define PADLOCK_PHE 0x0C00
#define PADLOCK_PMM 0x3000

#define PADLOCK_ALIGN16(x) (uint32_t *) (16 + ((long) x & ~15))

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief          PadLock detection routine
 *
 * \return         1 if CPU has support for the feature, 0 otherwise
 */
int padlock_supports(int feature) __leaf;

/**
 * \brief          PadLock AES-ECB block en(de)cryption
 *
 * \param ctx      AES context
 * \param mode     AES_ENCRYPT or AES_DECRYPT
 * \param input    16-byte input block
 * \param output   16-byte output block
 *
 * \return         0 if success, 1 if operation failed
 */
int padlock_xcryptecb(aes_ctx * nonnull ctx, int mode, const byte input[16],
                      byte output[16]) __leaf;

/**
 * \brief          PadLock AES-CBC buffer en(de)cryption
 *
 * \param ctx      AES context
 * \param mode     AES_ENCRYPT or AES_DECRYPT
 * \param length   length of the input data
 * \param iv       initialization vector (updated after use)
 * \param input    buffer holding the input data
 * \param output   buffer holding the output data
 *
 * \return         0 if success, 1 if operation failed
 */
int padlock_xcryptcbc(aes_ctx * nonnull ctx, int mode, int length,
                      byte iv[16], const byte * nonnull input, 
                      byte * nonnull output) __leaf;

#ifdef __cplusplus
}
#endif

#endif /* HAVE_X86  */

#endif /* padlock.h */
