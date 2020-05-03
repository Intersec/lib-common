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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_CTYPE_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_CTYPE_H

/* @typedef ctype_desc_t
 * @brief is an array of 256 bits which represents in
 * common use the 256 8-bitted characters. Then one can
 * tests and sets the presence of every characters by modifying
 * a single bit
 */
typedef struct ctype_desc_t {
    uint32_t tab[256 / 32];
} ctype_desc_t;

extern ctype_desc_t const ctype_isalnum;
extern ctype_desc_t const ctype_isalpha;
extern ctype_desc_t const ctype_islower;
extern ctype_desc_t const ctype_isupper;
extern ctype_desc_t const ctype_isdigit;
extern ctype_desc_t const ctype_isspace;
extern ctype_desc_t const ctype_ishexdigit;
extern ctype_desc_t const ctype_isbindigit;
extern ctype_desc_t const ctype_iswordpart;
extern ctype_desc_t const ctype_iscvar;

/* @func ctype_desc_reset
 * @param[in] d
 */
static inline void ctype_desc_reset(ctype_desc_t * nonnull d)
{
    p_clear(d, 1);
}

/* @func ctype_desc_build
 * @param[in] d
 * @param[in] toks string of characters containing a token at every
 *                 characters.
 */
static inline void ctype_desc_build(ctype_desc_t * nonnull d,
                                    const char * nonnull toks)
{
    ctype_desc_reset(d);
    while (*toks) {
        SET_BIT(d->tab, (unsigned char)*toks);
        toks++;
    }
}

static inline void
ctype_desc_build2(ctype_desc_t * nonnull d, const char * nonnull toks,
                  int len)
{
    ctype_desc_reset(d);
    for (int i = 0; i < len; i++) {
        SET_BIT(d->tab, (unsigned char)toks[i]);
    }
}

/* TODO adding more functions to manipulate the ctype_desc_t
 *      structure(unset, toggle, ...).
 */

/* @func ctype_desc_contains
 * @param[in] d
 * @param[in] b 
 * @return the function returns true in the case b is present in d
 * @brief This function checks if a byte is set on in a ctype_desc
 *        structure
 */
static inline bool ctype_desc_contains(const ctype_desc_t * nonnull d, byte b)
{
    return TST_BIT(d->tab, b);
}

/* @func ctype_desc_combine
 * param[in] d1
 * param[in] d2
 * param[inout] dout
 * TODO binary operation on memory instead of a stupid bit per bit operating
 */
static inline void
ctype_desc_combine(ctype_desc_t * nonnull dst,
                   const ctype_desc_t * nonnull d1,
                   const ctype_desc_t * nonnull d2)
{
    for (int i = 0; i < countof(d1->tab); i++) {
        dst->tab[i] = d1->tab[i] | d2->tab[i];
    }
}

/* @func ctype_desc_invert
 * param[inout] d
 */
static inline void ctype_desc_invert(ctype_desc_t * nonnull d)
{
    for (int i = 0 ; i < countof(d->tab) ; i++)
        d->tab[i] = ~(d->tab[i]);
}

#endif
