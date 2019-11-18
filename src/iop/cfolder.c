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

#include <lib-common/iop.h>

#define _CF_WANT(e, err)    \
    do { if (unlikely(!(e))) return err; } while (0)
#define CF_WANT(e)          _CF_WANT(e, CF_ERR_INVALID)
#define CF_OVERFLOW(e)      _CF_WANT(e, CF_ERR_OVERFLOW)
#define CF_ERR(c, s,...) \
    ({ e_error(s, ##__VA_ARGS__); CF_ERR_##c; })


#define SIGNED(expr)    ((int64_t)(expr))

static int cf_get_last_type(qv_t(cf_elem) *stack)
{
    if (stack->len > 0)
        return stack->tab[stack->len - 1].type;
    return CF_ELEM_STACK_EMPTY;
}
static iop_cfolder_elem_t *cf_get_prev_op(qv_t(cf_elem) *stack)
{
    int pos = stack->len - 1;

    while (pos >= 0) {
        if (stack->tab[pos].type == CF_ELEM_OP)
            return &stack->tab[pos];
        pos--;
    }
    return NULL;
}
static int cf_op_precedence(iop_cfolder_op_t op, bool unary)
{
    /* From highest precedence to lowest */
    switch (op) {
      case CF_OP_EXP:
        return 7;
      case CF_OP_NOT:
        return 6;
      case CF_OP_MUL:
      case CF_OP_DIV:
      case CF_OP_MOD:
        return 5;
      case CF_OP_ADD:
        return 4;
      case CF_OP_SUB:
        if (unary) {
            return 6;
        } else {
            return 4;
        }
      case CF_OP_LSHIFT:
      case CF_OP_RSHIFT:
        return 3;
      case CF_OP_AND:
        return 2;
      case CF_OP_OR:
      case CF_OP_XOR:
        return 1;
      default:
        return CF_ERR_INVALID;
    }
}

/* Return true if an operator has a right associativity */
static bool cf_op_is_rassoc(iop_cfolder_op_t op, bool unary)
{
    return op == CF_OP_NOT || op == CF_OP_EXP || (unary && op == CF_OP_SUB);
}

/* Stack abstraction */
static void cf_stack_push(qv_t(cf_elem) *stack, iop_cfolder_elem_t elem)
{
    qv_append(stack, elem);
}
static int cf_stack_pop(qv_t(cf_elem) *stack, iop_cfolder_elem_t *elem)
{
    if (stack->len <= 0)
        return CF_ERR_INVALID;
    if (elem)
        *elem = stack->tab[stack->len - 1];
    qv_remove(stack, stack->len - 1);
    return 0;
}

static int cf_reduce(qv_t(cf_elem) *stack)
{
    iop_cfolder_elem_t eleft, op, eright, res;
    bool unary = false;
    cf_elem_init(&res);

    CF_WANT(stack->len >= 2);
    cf_stack_pop(stack, &eright);
    CF_WANT(eright.type == CF_ELEM_NUMBER);

    CF_WANT(stack->len >= 1);
    cf_stack_pop(stack, &op);
    CF_WANT(op.type == CF_ELEM_OP);

    if (op.op == CF_OP_LPAREN || op.op == CF_OP_RPAREN)
        return CF_ERR_INVALID;

    if (op.unary) {
        unary = true;
    } else {
        RETHROW(cf_stack_pop(stack, &eleft));
        CF_WANT(eleft.type == CF_ELEM_NUMBER);
    }

    /* Compute eleft OP eright */
    switch (op.op) {
        /* Arithmetic operations */
      case CF_OP_ADD:
        assert (!unary);
        res.num       = eleft.num + eright.num;
        res.is_signed = eleft.is_signed || eright.is_signed;

        if (res.is_signed) {
            int64_t l = SIGNED(eleft.num);
            int64_t r = SIGNED(eright.num);
            if (r < 0)
                CF_OVERFLOW(INT64_MIN - r <= l);
            else
                CF_OVERFLOW(INT64_MAX - r >= l);
        } else {
            CF_OVERFLOW(UINT64_MAX - eright.num >= eleft.num);
        }
        break;
      case CF_OP_SUB:
        if (unary) {
            res.num       = -eright.num;
            res.is_signed = !eright.is_signed;
        } else {
            res.num = eleft.num - eright.num;
            if (eleft.is_signed && eright.is_signed) {
                res.is_signed = SIGNED(eleft.num) > SIGNED(eright.num);
            } else
            if (eleft.is_signed && !eright.is_signed) {
                res.is_signed = true;
            } else
            if (!eleft.is_signed && eright.is_signed) {
                res.is_signed = false;
            } else {
                res.is_signed = eleft.num < eright.num;
            }
        }
        break;
      case CF_OP_MUL:
        assert (!unary);
        res.is_signed = eleft.is_signed ^ eright.is_signed;
        if (eleft.is_signed || eright.is_signed) {
            res.num = SIGNED(eleft.num) * SIGNED(eright.num);
        } else {
            res.num = eleft.num * eright.num;
        }
        break;
      case CF_OP_DIV:
        assert (!unary);
        if (!eright.num) {
            return e_error("invalid division by 0");
        }
        if (eleft.is_signed && eleft.num == (uint64_t)INT64_MIN
        &&  eright.is_signed && eright.num == (uint64_t)-1)
        {
            return e_error("division overflow");
        }

        res.is_signed = eleft.is_signed ^ eright.is_signed;
        if (eleft.is_signed || eright.is_signed) {
            res.num = SIGNED(eleft.num) / SIGNED(eright.num);
        } else {
            res.num = eleft.num / eright.num;
        }
        break;
      case CF_OP_MOD:
        assert (!unary);
        if (!eright.num) {
            return e_error("invalid modulo by 0");
        }
        res.is_signed = eleft.is_signed ^ eright.is_signed;
        if (eleft.is_signed || eright.is_signed) {
            res.num = SIGNED(eleft.num) % SIGNED(eright.num);
        } else {
            res.num = eleft.num % eright.num;
        }
        break;
      case CF_OP_EXP:
        assert (!unary);
        /* Negative exponent are forbidden */
        if (eright.is_signed && SIGNED(eright.num) < 0) {
            return CF_ERR(INVALID, "negative expressions are forbidden when"
                          " used as exponent");
        }
        res.is_signed = eleft.is_signed && (eright.num % 2 != 0);
        if (eright.num == 0) {
            res.num = 1;
        } else
        if (eleft.num == 0) {
            res.num = 0;
        } else
        if (eleft.is_signed) {
            int64_t sres = 1;
            int64_t l = SIGNED(eleft.num);

            if (l == -1) {
                res.num = (eright.num % 2 == 0) ? 1 : -1;
                break;
            }
            while (eright.num-- > 0) {
                int64_t tmp = sres * l;

                CF_OVERFLOW(tmp / l == sres);
                sres = tmp;
            }
            res.num = sres;
        } else {
            if (eleft.num == 1) {
                res.num = 1;
                break;
            }
            res.num = 1;
            while (eright.num-- > 0) {
                uint64_t tmp = res.num * eleft.num;

                CF_OVERFLOW(tmp / eleft.num == res.num);
                res.num = tmp;
            }
        }
        break;

        /* Logical operations */
        /* XXX when a logical expression is used, the result is considered as
         * an unsigned expression */
      case CF_OP_XOR:
        assert (!unary);
        res.num = eleft.num ^ eright.num;
        break;
      case CF_OP_AND:
        assert (!unary);
        res.num = eleft.num & eright.num;
        break;
      case CF_OP_OR:
        assert (!unary);
        res.num = eleft.num | eright.num;
        break;
      case CF_OP_NOT:
        res.num = ~eright.num;
        break;
      case CF_OP_LSHIFT:
        assert (!unary);
        res.num = eleft.num << eright.num;
        break;
      case CF_OP_RSHIFT:
        assert (!unary);
        res.num = eleft.num >> eright.num;
        break;
      default:
        return CF_ERR(INVALID, "unknown operator");
    }

    res.type = CF_ELEM_NUMBER;
    cf_stack_push(stack, res);
    return 0;
}

static int cf_reduce_all(qv_t(cf_elem) *stack)
{
    while (stack->len > 1) {
        RETHROW(cf_reduce(stack));
    }
    return 0;
}

static int cf_reduce_until_paren(qv_t(cf_elem) *stack)
{
    iop_cfolder_elem_t num, op;

    while (stack->len > 1) {
        iop_cfolder_elem_t *sptr = &stack->tab[stack->len - 2];

        if (sptr->type == CF_ELEM_OP && sptr->op == CF_OP_LPAREN)
            break;

        RETHROW(cf_reduce(stack));
    }

    /* Pop the reduced number and the open parentheses */
    RETHROW(cf_stack_pop(stack, &num));
    CF_WANT(num.type == CF_ELEM_NUMBER);

    RETHROW(cf_stack_pop(stack, &op));
    CF_WANT(op.type == CF_ELEM_OP);
    CF_WANT(op.op   == CF_OP_LPAREN);

    /* Replace the number */
    cf_stack_push(stack, num);

    return 0;
}

iop_cfolder_err_t
iop_cfolder_feed_number(iop_cfolder_t *folder, uint64_t num, bool is_signed)
{
    iop_cfolder_elem_t elem;
    cf_elem_init(&elem);

    if (cf_get_last_type(&folder->stack) == CF_ELEM_NUMBER)
        return CF_ERR(INVALID, "there is already a number on the stack");

    elem.type      = CF_ELEM_NUMBER;
    elem.num       = num;
    elem.is_signed = is_signed && SIGNED(num) < 0;

    cf_stack_push(&folder->stack, elem);
    return CF_OK;
}

iop_cfolder_err_t
iop_cfolder_feed_operator(iop_cfolder_t *folder, iop_cfolder_op_t op)
{
    iop_cfolder_elem_t elem;
    int op_prec;

    cf_elem_init(&elem);
    elem.type  = CF_ELEM_OP;
    elem.num   = op;

    if (cf_get_last_type(&folder->stack) != CF_ELEM_NUMBER) {
        /* Check for an unary operator */
        switch (op) {
          case CF_OP_SUB:
          case CF_OP_NOT:
            elem.type  = CF_ELEM_OP;
            elem.op    = op;
            elem.unary = true;
            goto shift;
          case CF_OP_LPAREN:
            folder->paren_cnt++;
            goto shift;
          default:
            return CF_ERR(INVALID, "an unary operator was expected");
        }
    }

    /* Number case */
    if (op == CF_OP_NOT || op == CF_OP_LPAREN)
        return CF_ERR(INVALID, "a binary operator was expected");

    /* Handle parentheses */
    if (op == CF_OP_RPAREN) {
        folder->paren_cnt--;
        if (folder->paren_cnt < 0)
            return CF_ERR(INVALID, "there are too many closed parentheses");
        /* Reduce until we reach an open parentheses */
        if (cf_reduce_until_paren(&folder->stack) < 0)
            return CF_ERR(INVALID, "invalid closed parentheses position");
        return CF_OK;
    }

    RETHROW(op_prec = cf_op_precedence(op, false));

    /* Test for reduce */
    for (iop_cfolder_elem_t *pelem; (pelem = cf_get_prev_op(&folder->stack)); ) {
        iop_cfolder_op_t pop = pelem->op;
        int pop_prec         = cf_op_precedence(pop, pelem->unary);

        if (pop_prec > op_prec) {
            /* The previous operator has a higest priority than the new
             * one, we reduce it before to continue and we check again */
            RETHROW(cf_reduce(&folder->stack));
            continue;
        } else
        if (pop_prec == op_prec) {
            /* If precendences are equal, then a right associative operator
             * continue to shift whereas a left associative operator reduce */
            if (!cf_op_is_rassoc(pop, pelem->unary)) {
                RETHROW(cf_reduce(&folder->stack));
            }
        }

        /* If the previous operator has a lowest priority than the new one we
         * continue to shift */
        break;
    }

    /* Now shift the new operator */
  shift:
    cf_stack_push(&folder->stack, elem);

    return CF_OK;
}

iop_cfolder_err_t
iop_cfolder_get_result(iop_cfolder_t *folder, uint64_t *res, bool *is_signed)
{
    int ret;
    iop_cfolder_elem_t elem;

    if (folder->stack.len == 0)
        return CF_ERR(INVALID, "there is nothing on the stack");

    if (folder->paren_cnt)
        return CF_ERR(INVALID, "there too many opened parentheses");

    /* Reduce until the end */
    if ((ret = cf_reduce_all(&folder->stack)) < 0) {
        if (ret == CF_ERR_OVERFLOW)
            return CF_ERR(OVERFLOW, "overflow");
        else
            return CF_ERR(INVALID, "can't reduce completly the stack");
    }

    RETHROW(cf_stack_pop(&folder->stack, &elem));
    if (elem.type != CF_ELEM_NUMBER)
        return CF_ERR(INVALID, "invalid stack content");

    *res = elem.num;
    if (is_signed) {
        *is_signed = (elem.is_signed && SIGNED(elem.num) < 0);
    }

    return CF_OK;
}
