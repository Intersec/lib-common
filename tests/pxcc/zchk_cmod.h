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

#ifndef IS_PXCC_ZCHK_H
#define IS_PXCC_ZCHK_H

#include <stdbool.h>
#include <stdint.h>

#define LEN  5

/* {{{ Types & symbols syntax */

typedef struct foo_t foo_t;
typedef struct bar_t bar_t;

typedef void (*cb_ptr_f)(foo_t *);
typedef void cb_non_ptr_f(foo_t *);

typedef struct nested_t {
    int a;
} nested_t;

typedef enum enum_t {
    ENUM_A,
    ENUM_B = 5,
    ENUM_C = (1 << 5),
} enum_t;

typedef union union_t {
    int a;
    int b;
} union_t;

typedef struct struct_t {
    int a : 32;
    double b, c;

    nested_t const * const ptr_st;
    enum enum_t en;
    union_t un;

    union {
        int ua;
        double ub;
    } anon_un;
    union {
        const bool uc;
        const long ud;
    };
    enum {
        STRUCT_ENUM1,
        STRUCT_ENUM2,
    } anon_en;
    struct {
        uint64_t st1;
        struct {
            long st2;
        } st3;
    } anon_st;

    cb_ptr_f cb1;
    cb_non_ptr_f *cb2;
    bar_t (*cb3)(void);
    void (*cb4)(char *(*(*)(void *))[42]);
    float arr1[LEN];
    double arr2[];
} struct_t;

struct empty_struct_t;
typedef struct empty_struct_t empty_struct_t;

union empty_union_t;
typedef union empty_union_t empty_union_t;

enum empty_enum_t;
typedef enum empty_enum_t empty_enum_t;

struct non_typedef_struct_t {
    int a;
};

typedef struct {
    int a;
} only_typedef_struct_t;

typedef struct different_name_struct_t {
    int a;
} different_name_struct_t;
typedef different_name_struct_t different_name_typedef_struct_t;

typedef void *void_ptr_t;
typedef int array_ptr_t[5];

typedef struct result_t result_t;
typedef union arg1_t arg1_t;
typedef enum arg2_t arg2_t;

result_t func(arg1_t arg1, arg2_t arg2);

typedef struct var_type_t var_type_t;
extern var_type_t *global_var_g;

/* function which returns a pointer to an array of 10 pointers to doubles */
double *(*crazy_fn(int*, char*))[10];

/* pointer to function which returns pointer to array of 42 pointers to char
 */
typedef char *(*(*crazy_fn_ptr)(void *))[42];

/* function that that takes int and returns pointer to function that takes
 * two floats and returns float. */
float (*returns_func_ptr(int foo))(float, float);

/* function which takes pointer to char and returns pointer to function which
 * takes int and double and returns pointer to function that takes int and
 * long and returns pointer to function that takes pointer to char and
 * returns pointer to double. */
double *(*(*(*returns_func_ptr_nested(char*))(int, double))(int, long))(char*);

/* struct with function pointer field which takes int and returns pointer
 * to function which takes float pointer and returns pointer to char. */
typedef struct crazy_field_t {
    char *(*(*crazy_ptr)(int))(float*);
} crazy_field_t;

typedef struct qhash_hdr_t {
    /* size_t     * nonnull bits; */
    uint32_t    len;
    uint32_t    size;
    /* mem_pool_t * nullable mp; */
} qhash_hdr_t;

#define STRUCT_QHASH_T(key_t, val_t)                                         \
    struct {                                                                 \
        qhash_hdr_t  hdr;                                                    \
        qhash_hdr_t *old;                                                    \
        key_t       *keys;                                                   \
        val_t       *values;                                                 \
        uint32_t    *hashes;                                                 \
        uint32_t     ghosts;                                                 \
        uint8_t      h_size;                                                 \
        uint8_t      k_size;                                                 \
        uint16_t     v_size;                                                 \
        uint32_t     minsize;                                                \
    }

/* uint8_t allow us to use pointer arith on ->{values,vec} */
typedef STRUCT_QHASH_T(uint8_t, uint8_t) qhash_t;

typedef union qh_u32_t {
    qhash_t qh;
    STRUCT_QHASH_T(uint32_t, void);
} qh_u32_t;

/* }}} */
/* {{{ Python->C call */

int square(int a);

/* }}} */

#endif /* IS_PXCC_ZCHK_H */
