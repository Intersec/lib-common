/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#if !defined(IS_LIB_COMMON_IOP_H) || defined(IS_LIB_COMMON_IOP_CFOLDER_H)
#  error "you must include <lib-common/iop.h> instead"
#else
#define IS_LIB_COMMON_IOP_CFOLDER_H

typedef enum iop_cfolder_op_t {
    CF_OP_ADD       = '+',
    CF_OP_SUB       = '-',
    CF_OP_MUL       = '*',
    CF_OP_DIV       = '/',
    CF_OP_XOR       = '^',
    CF_OP_MOD       = '%',
    CF_OP_AND       = '&',
    CF_OP_OR        = '|',
    CF_OP_NOT       = '~',
    CF_OP_LPAREN    = '(',
    CF_OP_RPAREN    = ')',

    CF_OP_LSHIFT    = 128,
    CF_OP_RSHIFT,
    CF_OP_EXP,
} iop_cfolder_op_t;

typedef enum iop_cfolder_err_t {
    CF_OK               =  0,
    CF_ERR_INVALID      = -1,
    CF_ERR_OVERFLOW     = -2,
} iop_cfolder_err_t;

/* Stack object of constant folder elements */
typedef struct iop_cfolder_elem_t {
#define CF_ELEM_STACK_EMPTY  (-1)
#define CF_ELEM_NUMBER         1
#define CF_ELEM_OP             2
    unsigned             type;
    bool                 unary     : 1;
    bool                 is_signed : 1;
    union {
        uint64_t         num;
        iop_cfolder_op_t op;
    };
} iop_cfolder_elem_t;
GENERIC_FUNCTIONS(iop_cfolder_elem_t, cf_elem);
qvector_t(cf_elem, iop_cfolder_elem_t);

/* Constant folder object */
typedef struct iop_cfolder_t {
    qv_t(cf_elem) stack;
    int           paren_cnt;
} iop_cfolder_t;

static inline void iop_cfolder_wipe(iop_cfolder_t * nonnull folder)
{
    qv_wipe(&folder->stack);
}
GENERIC_DELETE(iop_cfolder_t, iop_cfolder);

GENERIC_NEW_INIT(iop_cfolder_t, iop_cfolder);

static inline bool iop_cfolder_empty(iop_cfolder_t * nonnull cfolder)
{
    return (cfolder->stack.len == 0 && cfolder->paren_cnt == 0);
}

iop_cfolder_err_t iop_cfolder_feed_number(iop_cfolder_t * nonnull , uint64_t,
                                          bool is_signed);
iop_cfolder_err_t iop_cfolder_feed_operator(iop_cfolder_t * nonnull,
                                            iop_cfolder_op_t);
iop_cfolder_err_t
iop_cfolder_get_result(iop_cfolder_t * nonnull, uint64_t * nonnull res,
                       bool * nullable is_signed);

#endif
