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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_MACROS_H)
# error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_MACROS_H

/** \defgroup generic_macros Intersec Generic Macros.
 * \{
 */

/** \file core-macros.h
 * \brief Intersec generic macros.
 */

#ifndef __has_feature
# define __has_feature(x)  0
#endif
#ifndef __has_builtin
# define __has_builtin(x)  0
#endif
#ifndef __has_attribute
# define __has_attribute(x)  0
#endif
#ifndef __has_warning
# define __has_warning(x)  0
#endif

/* {{{ GNU extension wrappers */

/* {{{ Helpers to handle compiler warnings */

/** Below is the definition of several macro to ignore compiler warnings.
 *
 * The CC_HAS_WARNING macro checks if the current compiler supports a warning.
 * Since GCC doesn't support checking directly a warning option name, it is
 * required to specify the GCC version supporting the option.
 *
 * CC_WARNING_IGNORE_PUSH and CC_WARNING_IGNORE_POP are equivalent to the
 * #pragma diagnostic push and pop directives but ignoring old GCC versions
 * which don't support them.
 *
 * The CC_WARNING_IGNORE macro allows to disable a specific warning and should
 * be used between the CC_WARNING_IGNORE_PUSH and CC_WARNING_IGNORE_POP
 * macros.
 *
 * General usage pattern looks like:
 *
 * \code
 * CC_WARNING_IGNORE_PUSH
 * #if CC_HAS_WARNING("-Wformat-nonliteral", 4, 2)
 *     CC_WARNING_IGNORE("-Wformat-nonliteral")
 * #endif
 *
 * // Some code triggering an unwanted -Wformat-nonliteral
 *
 * CC_WARNING_IGNORE_POP
 * \endcode
 *
 * But since it's a pain to always check which GCC version introduces which
 * warning, several common helpers are provided like this:
 *
 * \code
 * CC_WARNING_IGNORE_PUSH
 * CC_WARNING_IGNORE_FORMAT_NONLITERAL
 *
 * // Some code triggering an unwanted -Wformat-nonliteral
 *
 * CC_WARNING_IGNORE_POP
 * \endcode
 */
#define CC_HAS_WARNING(warn_option, gnuc_maj, gnuc_min) \
    /* Note: __has_warning exist since clang 3.6 and do not exist at all for \
     * GCC, which is why checking for GCC version with __GNUC_PREREQ is      \
     * necessary.                                                            \
     */                                                                      \
    (__has_warning(warn_option) || __GNUC_PREREQ(gnuc_maj, gnuc_min))

#if __GNUC_PREREQ(4, 6) || __CLANG_PREREQ(3, 0)
# define CC_WARNING_IGNORE_PUSH \
      _Pragma("GCC diagnostic push")
# define CC_WARNING_IGNORE_POP \
      _Pragma("GCC diagnostic pop")
#else
# define CC_WARNING_IGNORE_PUSH
# define CC_WARNING_IGNORE_POP
#endif

#define __DO_PRAGMA(x) _Pragma (#x)

#define CC_WARNING_IGNORE(warn_option) \
    __DO_PRAGMA(GCC diagnostic ignored warn_option)

#if CC_HAS_WARNING("-Wformat-nonliteral", 4, 2)
# define CC_WARNING_IGNORE_FORMAT_NONLITERAL \
     CC_WARNING_IGNORE("-Wformat-nonliteral")
#else
# define CC_WARNING_IGNORE_FORMAT_NONLITERAL
#endif

#if CC_HAS_WARNING("-Wsign-compare", 4, 2)
# define CC_WARNING_IGNORE_SIGN_COMPARE \
     CC_WARNING_IGNORE("-Wsign-compare")
#else
# define CC_WARNING_IGNORE_SIGN_COMPARE
#endif

#if CC_HAS_WARNING("-Wunused-but-set-variable", 4, 6)
# define CC_WARNING_IGNORE_UNUSED_BUT_SET_VARIABLE \
     CC_WARNING_IGNORE("-Wunused-but-set-variable")
#else
# define CC_WARNING_IGNORE_UNUSED_BUT_SET_VARIABLE
#endif

#if CC_HAS_WARNING("-Wcast-function-type", 8, 0)
# define CC_WARNING_IGNORE_CAST_FUNCTION_TYPE \
     CC_WARNING_IGNORE("-Wcast-function-type")
#else
# define CC_WARNING_IGNORE_CAST_FUNCTION_TYPE
#endif

#if CC_HAS_WARNING("-Wunused-value", 11, 2)
# define CC_WARNING_IGNORE_UNUSED_VALUE \
     CC_WARNING_IGNORE("-Wunused-value")
#else
# define CC_WARNING_IGNORE_UNUSED_VALUE
#endif

#if CC_HAS_WARNING("-Warray-bounds", 14, 0)
# define CC_WARNING_IGNORE_ARRAY_BOUNDS \
     CC_WARNING_IGNORE("-Warray-bounds")
#else
# define CC_WARNING_IGNORE_ARRAY_BOUNDS
#endif

#if __has_warning("-Wcast-function-type-mismatch")
# define CC_WARNING_IGNORE_CAST_FUNCTION_TYPE_MISMATCH \
     CC_WARNING_IGNORE("-Wcast-function-type-mismatch")
#else
# define CC_WARNING_IGNORE_CAST_FUNCTION_TYPE_MISMATCH
#endif

/* }}} */

#if !defined(__doxygen_mode__)
# if !__GNUC_PREREQ(3, 0) && !defined(__clang__)
#   define __attribute__(attr)
# endif
# if !defined(__GNUC__) || defined(__cplusplus)
#   define __must_be_array(a)   0
# else
#   define __must_be_array(a) \
         (sizeof(char[1 - 2 * __builtin_types_compatible_p(typeof(a), typeof(&(a)[0]))]) - 1)
# endif

#if  __has_feature(address_sanitizer)
#  define __has_asan       1
#  define __attr_noasan__  __attribute__((no_sanitize_address))
#else
# define __attr_noasan__
#endif

#if  __has_feature(thread_sanitizer)
#  define __has_tsan       1
#  define __attr_notsan__  __attribute__((no_sanitize_thread))
#else
# define __attr_notsan__
#endif

#if  !__has_feature(nullability)
# define nullable
# define _Nullable
# define nonnull
# define _Nonnull
# define null_unspecified
# define _Null_unspecified
#else
# define nullable          _Nullable
# define nonnull           _Nonnull
# define null_unspecified  _Null_unspecified
#endif

#if  !__has_feature(objc_arc)
# define __bridge
# define __bridge_transfer
# define __bridge_retain
# define __unsafe_unretained
#endif

#ifdef __cplusplus
#define restrict
#endif

/*
 * __attr_unused__             => unused vars
 * __attr_noreturn__           => functions that perform abord()/exit()
 * __attr_printf__(pos_fmt, pos_first_arg) => printf format
 * __attr_scanf__(pos_fmt, pos_first_arg) => scanf format
 */
# define __attr_unused__        __attribute__((unused))
# define __must_check__         __attribute__((warn_unused_result))
# define __attr_noreturn__      __attribute__((noreturn))
# define __attr_nonnull__(l)    __attribute__((__nonnull__ l))
# define __attr_printf__(a, b)  __attribute__((format(printf, a, b),__nonnull__(a)))
# define __attr_scanf__(a, b)   __attribute__((format(scanf, a, b),__nonnull__(a)))
# define __attr_deprecated__    __attribute__((deprecated))
# define __attr_cleanup__(f)    __attribute__((cleanup(f)))

#ifndef __attribute_deprecated__
# define __attribute_deprecated__  __attr_deprecated__
#endif
#endif

#ifdef __GNUC__
# ifndef EXPORT
#   define EXPORT  extern __attribute__((visibility("default")))
# endif
# define HIDDEN    extern __attribute__((visibility("hidden")))
# ifdef __OPTIMIZE__
#   if __GNUC_PREREQ(4, 3) || __has_attribute(artificial)
#     define ALWAYS_INLINE inline __attribute__((always_inline,artificial))
#   else
#     define ALWAYS_INLINE inline __attribute__((always_inline))
#   endif
# else
#   define ALWAYS_INLINE inline
# endif
# define NEVER_INLINE __attribute__((noinline))
#else
# ifndef EXPORT
#   define EXPORT  extern
# endif
# define HIDDEN    extern
# define ALWAYS_INLINE inline
# define NEVER_INLINE __attribute__((noinline))
#endif

#if __GNUC_PREREQ(4, 1) || __has_attribute(flatten)
# define __flatten __attribute__((flatten))
#else
# define __flatten
#endif

#if __GNUC_PREREQ(4, 3) || __has_attribute(cold)
# define __cold __attribute__((cold))
#else
# define __cold
#endif

#if !(__GNUC_PREREQ(4, 5) || __has_builtin(builtin_unreachable))
# define __builtin_unreachable() assert (false)
#endif

#if __GNUC_PREREQ(4, 6) || __has_attribute(leaf)
# define __leaf __attribute__((leaf))
#else
# define __leaf
#endif

#if __GNUC_PREREQ(7, 0) || __has_attribute(fallthrough)
# define __fallthrough __attribute__((fallthrough))
#else
# define __fallthrough
#endif

#if __has_attribute(nonstring)
#define __attr_nonstring__ __attribute__((nonstring))
#else
#define __attr_nonstring__
#endif

#if __has_attribute(optimize)
# define __attr_optimize__(o)  __attribute__((optimize(o)))
#else
# define __attr_optimize__(o)
#endif

#ifdef __GNUC__
# define likely(expr)     __builtin_expect(!!(expr), 1)
# define unlikely(expr)   __builtin_expect(!!(expr), 0)
#else
# define likely(expr)     (expr)
# define unlikely(expr)   (expr)
#endif

/** \def STATIC_ASSERT
 * \brief Check a condition at build time.
 * \param[in]  expr    the expression you want to be always true at compile * time.
 * \safemacro
 *
 */
#if __has_feature(c_static_assert) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
# define __error__(msg)       (void)({__asm__(".error \""msg"\"");})
# define STATIC_ASSERT(cond)  do { _Static_assert(cond, #cond); } while (0)
#   define ASSERT_COMPATIBLE(e1, e2) \
     STATIC_ASSERT(__builtin_types_compatible_p(typeof(e1), typeof(e2)))
#elif !defined(__cplusplus)
# ifdef __GNUC__
#   define __error__(msg)          (void)({__asm__(".error \""msg"\"");})
#   define STATIC_ASSERT(cond) \
       __builtin_choose_expr(cond, (void)0, \
                             __error__("static assertion failed: "#cond""))
#   define ASSERT_COMPATIBLE(e1, e2) \
     STATIC_ASSERT(__builtin_types_compatible_p(typeof(e1), typeof(e2)))
# else
#   define __error__(msg)            0
#   define STATIC_ASSERT(condition)  ((void)sizeof(char[1 - 2 * !(condition)]))
#   define ASSERT_COMPATIBLE(e1, e2)
# endif
#else
# define ASSERT_COMPATIBLE(e1, e2)
#endif

/** \brief Forcefully ignore the value of an expression.
 *
 * Use this to explicitly ignore return values of functions declared with
 * __attribute__((warn_unused_result)).
 *
 * \param[in]  expr    the expression.
 * \safemacro
 */
#define IGNORE(expr)             do { if (expr) (void)0; } while (0)

/** \brief Convenient functional macro that always expands to true.
 *
 * \warning This macro ignores all of its arguments. The arguments are
 *          not evaluated at all.
 */
#define TRUEFN(...)              true

#define cast(type, v)    ((type)(v))
#define acast(type, v)   ({                                                  \
        union {                                                              \
            typeof(*(v)) *__v;                                               \
            type         *__c;                                               \
        } __u = { .__v = (v) };                                              \
        __u.__c;                                                             \
    })

#define __attr_section(sg, sc)  __attribute__((section("."sg"."sc)))

/* }}} */
/* {{{ Useful expressions */

#ifndef MAX
# define MAX(a, b)                                                           \
    ({                                                                       \
        typeof(a) __a = (a);                                                 \
        typeof(b) __b = (b);                                                 \
                                                                             \
        __a > __b ? __a : __b;                                               \
    })
#endif
#ifndef MIN
# define MIN(a, b)                                                           \
    ({                                                                       \
        typeof(a) __a = (a);                                                 \
        typeof(b) __b = (b);                                                 \
                                                                             \
        __a > __b ? __b : __a;                                               \
    })
#endif
#define MIN3(a, b, c)        MIN((a), MIN((b), (c)))
#define MAX3(a, b, c)        MAX((a), MAX((b), (c)))

#define CLIP(v, m, M)                                                        \
    ({                                                                       \
        typeof(v) __v = (v);                                                 \
        typeof(m) __m = (m);                                                 \
        typeof(M) __M = (M);                                                 \
                                                                             \
        __v > __M ? __M : (__v < __m ? __m : __v);                           \
    })

#define ROUND(x, y)                                                          \
    ({                                                                       \
        typeof(x) __x = (x);                                                 \
        typeof(y) __y = (y);                                                 \
                                                                             \
        (__x / __y) * __y;                                                   \
    })
#define DIV_ROUND_UP_S(x, y)   (((x) + (y) - 1) / (y))
#define DIV_ROUND_UP(x, y)                                                   \
    ({                                                                       \
        typeof(x) __x = (x);                                                 \
        typeof(y) __y = (y);                                                 \
                                                                             \
        (__x + __y - 1) / __y;                                               \
    })
#define ROUND_UP(x, y)                                                       \
    ({                                                                       \
        typeof(x) __x = (x);                                                 \
        typeof(y) __y = (y);                                                 \
                                                                             \
        ((__x + __y - 1) / __y) * __y;                                       \
    })

#define ROUND_UP_2EXP(x, y)                            \
    ({ typeof((y)) __y = (y);                          \
       assert((__y & (__y - 1)) == 0);                 \
       ((x) + __y - 1) & ~(__y - 1);                   \
    })

#define ROUND_2EXP(x, y)                               \
    ({ typeof((y)) __y = (y);                          \
       typeof((x)) __x = (x);                          \
       assert((__y & (__y - 1)) == 0);                 \
       __x - (__x & (__y - 1));                        \
    })

#define NEXTARG(argc, argv)  (assert (argc), argc--, (void)argc, *argv++)
#define NEXTOPTARG(argc, argv)  ((argc) ? NEXTARG(argc, argv) : NULL)

/** RETHROW macros.
 *
 * P = Pointer
 * N = Negative
 *
 * These macros can be used on the following pattern:
 * \code
 *     int toto(...) {
 *         object_t *obj;               int toto(...) {
 *                                          object *obj;
 *         obj = get_object(...);  =>
 *         if (!obj) {                      obj = RETHROW_PN(get_object(...));
 *             return -1;                   ...
 *         }                            }
 *     ...
 *     }
 * \endcode
 */
#define RETHROW(e)                                                           \
    ({ typeof(e) __res = (e);                                                \
       if (unlikely(__res < 0)) {                                            \
           return __res;                                                     \
       }                                                                     \
       __res;                                                                \
    })

#define RETHROW_P(e)                                                         \
    ({ typeof(e) __res = (e);                                                \
       if (unlikely(__res == NULL)) {                                        \
           return NULL;                                                      \
       }                                                                     \
       __res;                                                                \
    })

#define RETHROW_PN(e)                                                        \
    ({ typeof(e) __res = (e);                                                \
       if (unlikely(__res == NULL)) {                                        \
           return -1;                                                        \
       }                                                                     \
       __res;                                                                \
    })

#define RETHROW_NP(e)                                                        \
    ({ typeof(e) __res = (e);                                                \
       if (unlikely(__res < 0)) {                                            \
           return NULL;                                                      \
       }                                                                     \
       __res;                                                                \
    })

/** THROW macros.
 *
 * These macros can be used on the following pattern:
 * \code
 *     if (time < lunch_time) {
 *         return false;         =>    THROW_FALSE_IF(time < lunch_time );
 *     }
 * \endcode
 */
#define THROW_IF(e, val)                                                     \
    do {                                                                     \
        if (unlikely(e)) {                                                   \
            return (val);                                                    \
        }                                                                    \
    } while (0)
#define THROW_UNLESS(e, val)    THROW_IF(!(e), (val))

#define THROW_NULL_IF(e)        THROW_IF((e), NULL)
#define THROW_NULL_UNLESS(e)    THROW_UNLESS((e), NULL)
#define THROW_ERR_IF(e)         THROW_IF((e), -1)
#define THROW_ERR_UNLESS(e)     THROW_UNLESS((e), -1)
#define THROW_FALSE_IF(e)       THROW_IF((e), false)
#define THROW_FALSE_UNLESS(e)   THROW_UNLESS((e), false)


#ifdef CMP
# error CMP already defined
#endif
#ifdef SIGN
# error SIGN already defined
#endif

enum sign {
    NEGATIVE = -1,
    ZERO     = 0,
    POSITIVE = 1,
};

#define CMP(x, y)     cast(enum sign, ((x) > (y)) - ((x) < (y)))
#define CMP_LESS      NEGATIVE
#define CMP_EQUAL     ZERO
#define CMP_GREATER   POSITIVE
#define SIGN(x)       CMP(x, 0)

#define PAD4(len)     (((len) + 3) & ~3)
#define PAD4EXT(len)  (3 - (((len) - 1) & 3))

#define TOSTR_AUX(x)  #x
#define TOSTR(x)      TOSTR_AUX(x)

#define SWAP(typ, a, b)    do { typ __c = a; a = b; b = __c; } while (0)

#define get_unaligned_type(type_t, ptr)        \
    ({                                         \
        struct __attribute__((packed)) {       \
            type_t __v;                        \
        } const *__p;                          \
        __p = cast(typeof(__p), ptr);          \
        __p->__v;                              \
    })
#define get_unaligned(ptr)  get_unaligned_type(typeof(*(ptr)), ptr)

#define put_unaligned_type(type_t, ptr, v)     \
    ({                                         \
        struct __attribute__((packed)) {       \
            type_t __v;                        \
        } *__p;                                \
        type_t __v = (v);                      \
        __p = cast(typeof(__p), ptr);          \
        __p->__v = __v;                        \
        __p + 1;                               \
    })
#define put_unaligned(ptr, v)  put_unaligned_type(typeof(v), ptr, v)

#ifndef __BIGGEST_ALIGNMENT__
# define __BIGGEST_ALIGNMENT__  16
#endif

/* }}} */
/* {{{ Types */

/* 128 bits integers */
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;

/* Useful atomics not defined in standard */
typedef _Atomic(int8_t)   atomic_int8_t;
typedef _Atomic(uint8_t)  atomic_uint8_t;
typedef _Atomic(int16_t)  atomic_int16_t;
typedef _Atomic(uint16_t) atomic_uint16_t;
typedef _Atomic(int32_t)  atomic_int32_t;
typedef _Atomic(uint32_t) atomic_uint32_t;
typedef _Atomic(int64_t)  atomic_int64_t;
typedef _Atomic(uint64_t) atomic_uint64_t;
typedef _Atomic(ssize_t)  atomic_ssize_t;

/** Endianess aware types.
 *
 * There is no magic behind those types, they are just used as an information
 * for the reader.
 */
typedef uint128_t cpu128_t;
typedef uint128_t be128_t;
typedef uint128_t le128_t;
typedef uint64_t cpu64_t;
typedef uint64_t be64_t;
typedef uint64_t le64_t;
typedef uint64_t le48_t;
typedef uint64_t be48_t;
typedef uint32_t cpu32_t;
typedef uint32_t le32_t;
typedef uint32_t be32_t;
typedef uint32_t le24_t;
typedef uint32_t be24_t;
typedef uint16_t cpu16_t;
typedef uint16_t le16_t;
typedef uint16_t be16_t;

#define MAKE64(hi, lo)  (((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo))
#define MAKE128(hi, lo) (((uint128_t)(uint64_t)(hi) << 64) | (uint64_t)(lo))

#define UINT128_MAX  MAKE128(UINT64_MAX, UINT64_MAX)
#define INT128_MAX  (int128_t)MAKE128(INT64_MAX, UINT64_MAX)
#define INT128_MIN  (-INT128_MAX - 1)

typedef unsigned char byte;

#define fieldsizeof(type_t, m)  sizeof(cast(type_t *, 0)->m)
#define fieldtypeof(type_t, m)  typeof(cast(type_t *, 0)->m)
#define countof(table)          (cast(ssize_t, sizeof(table) / sizeof((table)[0]) \
                                      + __must_be_array(table)))
#define ssizeof(foo)            (cast(ssize_t, sizeof(foo)))

#define bitsizeof(type_t)       (sizeof(type_t) * CHAR_BIT)
#define BITS_TO_ARRAY_LEN(type_t, nbits)  \
    DIV_ROUND_UP_S(nbits, bitsizeof(type_t))

#define BITMASK_NTH(type_t, n) ( cast(type_t, 1) << ((n) & (bitsizeof(type_t) - 1)))
#define BITMASK_LT(type_t, n)  (BITMASK_NTH(type_t, n) - 1)
#define BITMASK_LE(type_t, n)  ((BITMASK_NTH(type_t, n) << 1) - 1)
#define BITMASK_GE(type_t, n)  (~cast(type_t, 0) << ((n) & (bitsizeof(type_t) - 1)))
#define BITMASK_GT(type_t, n)  (BITMASK_GE(type_t, n) << 1)

#define OP_BIT(bits, n, shift, op) \
    ((bits)[(unsigned)(n) / (shift)] op BITMASK_NTH(typeof(*(bits)), n))
#define TST_BIT(bits, n)    OP_BIT(bits, n, bitsizeof(*(bits)), &  )
#define SET_BIT(bits, n)    (void)OP_BIT(bits, n, bitsizeof(*(bits)), |= )
#define RST_BIT(bits, n)    (void)OP_BIT(bits, n, bitsizeof(*(bits)), &=~)
#define CLR_BIT(bits, n)    RST_BIT(bits, n)
#define XOR_BIT(bits, n)    (void)OP_BIT(bits, n, bitsizeof(*(bits)), ^= )

#ifdef __GNUC__
# define container_of(obj, type_t, member) \
      ({ const typeof(cast(type_t *, 0)->member) *__mptr = (obj);              \
         cast(type_t *, cast(char *, __mptr) - offsetof(type_t, member)); })
#else
# define container_of(obj, type_t, member) \
      cast(type_t *, cast(char *, (obj)) - offsetof(type_t, member))
#endif

/** Gets rid of the 'const' modifier of the pointer but keeps the type check.
 */
#define unconst_cast(type_t, p)  ({ const type_t *__p = (p); (type_t *)__p; })

/* }}} */
/* {{{ Const size array */

/** Create an array of type \ref _type_t with variable name \ref _name of
 *  constant size \ref _sz.
 */
#ifdef __cplusplus
# define CONST_SIZE_ARRAY(_name, _type_t, _sz)  _type_t _name[(_sz)]
#else
# define CONST_SIZE_ARRAY(_name, _type_t, _sz)                               \
    typeof(*(_type_t *)({                                                    \
        STATIC_ASSERT(__builtin_constant_p(_sz));                            \
        NULL;                                                                \
    })) _name[(_sz)]
#endif

/* }}} */
/* {{{ Macro-building helpers */

#define __PFX_LINE_SFX(pfx, line, sfx)  pfx##line##sfx
#define _PFX_LINE_SFX(pfx, line, sfx)   __PFX_LINE_SFX(pfx, line, sfx)

/** Builds a name by concatenating \p pfx to the line number. */
#define PFX_LINE(pfx)  _PFX_LINE_SFX(pfx, __LINE__, )

/** Builds a name by concatenating \p pfx, line number and \p sfx. */
#define PFX_LINE_SFX(pfx, sfx)  _PFX_LINE_SFX(pfx, __LINE__, sfx)

/* The FOR_INSTR* macros are used to declare variables with different types
 * when building for_each-like macros. The `ctx` argument is used to avoid
 * conflicts with multiple invocations but must be the same for the same
 * instruction block.
 *
 * Note that ';' is not allowed because we're inside a for loop.
 *
 * You will find a typical example with __iop_array_for_each.
 */
#define FOR_INSTR_INIT(ctx)                                                  \
    for (bool ctx##stop = false; !ctx##stop; ctx##stop = true)

#define FOR_INSTR(ctx, instr)                                                \
    for (instr; !ctx##stop; ctx##stop = true)

#define FOR_INSTR1(ctx, i1)                                                  \
    FOR_INSTR_INIT(ctx)                                                      \
    FOR_INSTR(ctx, i1)

#define FOR_INSTR2(ctx, i1, i2)                                              \
    FOR_INSTR1(ctx, i1)                                                      \
    FOR_INSTR(ctx, i2)

#define FOR_INSTR3(ctx, i1, i2, i3)                                          \
    FOR_INSTR2(ctx, i1, i2)                                                  \
    FOR_INSTR(ctx, i3)

#define FOR_INSTR4(ctx, i1, i2, i3, i4)                                      \
    FOR_INSTR3(ctx, i1, i2, i3)                                              \
    FOR_INSTR(ctx, i4)

#define FOR_INSTR5(ctx, i1, i2, i3, i4, i5)                                  \
    FOR_INSTR4(ctx, i1, i2, i3, i4)                                          \
    FOR_INSTR(ctx, i5)

#define FOR_INSTR6(ctx, i1, i2, i3, i4, i5, i6)                              \
    FOR_INSTR5(ctx, i1, i2, i3, i4, i5)                                      \
    FOR_INSTR(ctx, i6)

#define FOR_INSTR7(ctx, i1, i2, i3, i4, i5, i6, i7)                          \
    FOR_INSTR6(ctx, i1, i2, i3, i4, i5, i6)                                  \
    FOR_INSTR(ctx, i7)

/* }}} */
/* {{{ Loops */

/* Standard loops for structures of the form struct { type_t *tab; int len; }.
 */

#define tab_for_each_pos(pos, vec)                                           \
    for (int pos = 0; pos < (vec)->len; pos++)

#define _tab_for_each_ptr(ptr_var, ptr_cpy_var, vec_var, _vec)               \
    FOR_INSTR1(_tab_for_each_ptr##ptr_var,                                   \
               typeof(*_vec) *vec_var = (_vec))                              \
    for (__attr_unused__ typeof(*vec_var->tab) *ptr_var = vec_var->tab,      \
         *ptr_cpy_var = vec_var->tab;                                        \
         ptr_cpy_var < vec_var->tab + vec_var->len;                          \
         ptr_var = ++ptr_cpy_var)

#define tab_for_each_ptr(ptr_var, _vec)                                      \
    _tab_for_each_ptr(ptr_var, tab_for_each_ptr_ptr_cpy##ptr_var,            \
                      tab_for_each_ptr_vec##ptr_var, (_vec))

#define _tab_enumerate_ptr(pos_var, ptr_var, vec_var, _vec)                  \
    FOR_INSTR2(_tab_enumerate_ptr##ptr_var,                                  \
               typeof(*_vec) *vec_var = (_vec),                              \
               __attr_unused__ typeof(*vec_var->tab) *ptr_var)               \
    for (int pos_var = 0;                                                    \
         (ptr_var = &vec_var->tab[pos_var], pos_var < vec_var->len);         \
         pos_var++)

#define tab_enumerate_ptr(pos_var, ptr_var, _vec)                            \
    _tab_enumerate_ptr(pos_var, ptr_var, tab_enumerate_ptr_vec##ptr_var,     \
                       (_vec))


#define _tab_enumerate(pos_var, entry_var, vec_var, _vec)                    \
    FOR_INSTR2(_tab_enumerate##entry_var,                                    \
               typeof(*_vec) *vec_var = (_vec),                              \
               __attr_unused__ typeof(*vec_var->tab) entry_var)              \
    for (int pos_var = 0;                                                    \
         pos_var < vec_var->len                                              \
      && (entry_var = vec_var->tab[pos_var], true);                          \
         pos_var++)

#define tab_enumerate(pos_var, entry_var, _vec)                              \
    _tab_enumerate(pos_var, entry_var, tab_enumerate_vec##entry_var, (_vec))

#define tab_for_each_entry(entry_var, _vec)                                  \
    tab_enumerate(tab_for_each_entry_pos##entry_var, entry_var, (_vec))

#define tab_for_each_pos_rev(pos, vec)                                       \
    for (int pos = (vec)->len; pos-- > 0; )

#define tab_for_each_pos_safe(pos, vec)                                      \
    tab_for_each_pos_rev(pos, vec)

#define tab_last(vec)                                                        \
    ({  typeof(*(vec)) const *__vec = (vec);                                 \
        assert (__vec->len > 0);                                             \
        __vec->tab + __vec->len - 1; })

#define tab_swap(vec, pos1, pos2)                                            \
    do {                                                                     \
        typeof(*(vec)) *__vec = (vec);                                       \
        typeof(pos1) __pos1 = (pos1);                                        \
        typeof(pos2) __pos2 = (pos2);                                        \
                                                                             \
        assert (__pos1 < __vec->len);                                        \
        assert (__pos2 < __vec->len);                                        \
                                                                             \
        SWAP(typeof(*__vec->tab), __vec->tab[__pos1], __vec->tab[__pos2]);   \
    } while (0)

/* Standard loops for C arrays (ex: int values[] = { 1, 2, 3 }) */

#define carray_for_each_pos(pos, array)                                      \
    for (int pos = 0; pos < countof(array); pos++)

#define carray_for_each_ptr(ptr, array)                                      \
    for (__attr_unused__ typeof(*array) *ptr = (array),                      \
         *__i_##ptr = (array);                                               \
         __i_##ptr < (array) + countof(array);                               \
         ptr = ++__i_##ptr)

#define carray_for_each_entry(e, array)                                      \
    for (typeof(*array) e, *e##__ptr = (array);                              \
         ({                                                                  \
              bool e##__res = e##__ptr < (array) + countof(array);           \
              if (e##__res) {                                                \
                  e = *(e##__ptr++);                                         \
              }                                                              \
              e##__res;                                                      \
         });)

/* }}} */
/* {{{ Dangerous APIs */

#undef sprintf
#define sprintf(...)  NEVER_USE_sprintf(__VA_ARGS__)
#undef strtok
#define strtok(...)   NEVER_USE_strtok(__VA_ARGS__)
#undef strncpy
#define strncpy(...)  NEVER_USE_strncpy(__VA_ARGS__)
#undef strncat
#define strncat(...)  NEVER_USE_strncat(__VA_ARGS__)
#undef readdir_r
#define readdir_r(...)  NEVER_USE_readdir_r(__VA_ARGS__)

/* fork() is not dangerous, but ifork() must be used instead */
#undef fork
#define fork(...)  NEVER_USE_fork(__VA_ARGS__)

/* }}} */
/** \} */
#endif
