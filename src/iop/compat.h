/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

/* This header contains a minimal set of definitions needed to compile IOP
 * files (and will be used to compile IOP at runtime).
 * These definitions are duplicated from other lib-common headers.
 */

#ifndef IS_LIB_COMMON_IOP_COMPAT_H
#define IS_LIB_COMMON_IOP_COMPAT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>

/* core-macros.h */

#define __has_feature(x)  0

#define __must_be_array(a) \
         (sizeof(char[1 - 2 * __builtin_types_compatible_p(typeof(a), typeof(&(a)[0]))]) - 1)

#define cast(type, v)    ((type)(v))

#define fieldsizeof(type_t, m)  sizeof(cast(type_t *, 0)->m)
#define fieldtypeof(type_t, m)  typeof(cast(type_t *, 0)->m)
#define countof(table)          (cast(ssize_t, sizeof(table) / sizeof((table)[0]) \
                                      + __must_be_array(table)))
#define ssizeof(foo)            (cast(ssize_t, sizeof(foo)))

#define bitsizeof(type_t)       (sizeof(type_t) * CHAR_BIT)

# ifndef EXPORT
#   define EXPORT  extern __attribute__((visibility("default")))
# endif

# define __cold
# define __attr_printf__(a, b)  __attribute__((format(printf, a, b)))

#define nullable
#define nonnull
#define null_unspecified

/* str-l.h */
typedef struct lstr_t {
    union {
        const char *s;
        char       *v;
        void       *data;
    };
    int len;
    unsigned mem_pool;
} lstr_t;

#define LSTR_INIT(s_, len_)     { { (s_) }, (len_), 0 }
#define LSTR_IMMED(str)         LSTR_INIT(""str, sizeof(str) - 1)
#define LSTR_FMT_ARG(s_)        (s_).len, (s_).s

/* core-types.h */
#define OPT_OF(type_t)     struct { type_t v; bool has_field; }
typedef OPT_OF(int8_t)     opt_i8_t;
typedef OPT_OF(uint8_t)    opt_u8_t;
typedef OPT_OF(int16_t)    opt_i16_t;
typedef OPT_OF(uint16_t)   opt_u16_t;
typedef OPT_OF(int32_t)    opt_i32_t;
typedef OPT_OF(uint32_t)   opt_u32_t;
typedef OPT_OF(int64_t)    opt_i64_t;
typedef OPT_OF(uint64_t)   opt_u64_t;
typedef OPT_OF(int)        opt_enum_t;
typedef OPT_OF(bool)       opt_bool_t;
typedef OPT_OF(double)     opt_double_t;
typedef opt_bool_t         opt__Bool_t;

/* iop-macros.h */
#define IOP_ENUM(pfx)
#define IOP_CLASS(pfx)
#define IOP_GENERIC(pfx)

#endif
