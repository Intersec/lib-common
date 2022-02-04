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
        unsigned long long st1;
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

union non_typedef_union_t {
    int a;
};

typedef union {
    int plop;
} only_typedef_union_t;

typedef union different_name_union_t {
    int a;
} different_name_union_t;
typedef different_name_union_t different_name_typedef_union_t;

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
    unsigned int len;
    unsigned int size;
    /* mem_pool_t * nullable mp; */
} qhash_hdr_t;

#define STRUCT_QHASH_T(key_t, val_t)                                         \
    struct {                                                                 \
        qhash_hdr_t    hdr;                                                  \
        qhash_hdr_t   *old;                                                  \
        key_t         *keys;                                                 \
        val_t         *values;                                               \
        unsigned int  *hashes;                                               \
        unsigned int   ghosts;                                               \
        unsigned char  h_size;                                               \
        unsigned char  k_size;                                               \
        unsigned short v_size;                                               \
        unsigned int   minsize;                                              \
    }

/* unsigned char allow us to use pointer arith on ->{values,vec} */
typedef STRUCT_QHASH_T(unsigned char, unsigned char) qhash_t;

typedef union qh_u32_t {
    qhash_t qh;
    STRUCT_QHASH_T(unsigned int, void);
} qh_u32_t;

/* Recursive reference between struct and callback */
typedef struct recursive_ref_struct_t recursive_ref_struct_t;
typedef void (*recursive_ref_cb_f)(recursive_ref_struct_t *);
struct recursive_ref_struct_t {
    recursive_ref_cb_f cb;
};

/* Cython keywords */
typedef struct cython_keywords_t {
    int False;
    int None;
    int True;
    int and;
    int as;
    int async;
    int await;
    int cimport;
    int class;
    int def;
    int del;
    int elif;
    int except;
    int finally;
    int from;
    int global;
    int include;
    int import;
    int in;
    int is;
    int lambda;
    int nonlocal;
    int not;
    int or;
    int pass;
    int raise;
    int try;
    int with;
    int yield;
} cython_keywords_t;

typedef struct include include;
union import;
typedef union import with;
enum except {
    finally,
};
typedef union {
    enum except pass;
} elif;
void yield(void);

/* }}} */
/* {{{ Python->C call */

int square(int a);

/* }}} */

#endif /* IS_PXCC_ZCHK_H */
