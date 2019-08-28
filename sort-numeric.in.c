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

/* XXX Syntastic tranquility block. */
#ifndef type_t
# include "sort.h"
# define type_t uint8_t
# define dsort dsort8
# define uniq uniq8
#endif

#ifndef TYPE_MIN
# define TYPE_MIN  (type_t)0
#endif

/* Translate an integer from [ min, max ] to [ 0, max - min ]. */
#ifdef utype_t
# define TRANSLATE(v)  ((utype_t)0 - TYPE_MIN + (v))
#else
# define TRANSLATE(v)  (v)
#endif

#ifdef SIMPLE_SORT
void dsort(type_t base[], size_t n)
{
    size_t count[1 << bitsizeof(type_t)]= { 0, };
    size_t pos = 0;

    for (size_t i = 0; i < n; i++) {
        count[TRANSLATE(base[i])]++;
    }

    for (uint32_t i = 0; i < countof(count); i++) {
        if (count[i]) {
            if (sizeof(type_t) == 1) {
                memset(&base[pos], TRANSLATE(i), count[i]);
                pos += count[i];
            } else {
                for (size_t j = 0; j < count[i]; j++) {
                    base[pos++] = TRANSLATE(i);
                }
            }
        }
    }
    assert (pos == n);
}

#else
/* Multipass stable byte based radix sort */
void dsort(type_t base[], size_t n)
{

    if (n <= 1)
        return;

    /* Check if array is already sorted */
    for (size_t i = 1; base[i - 1] <= base[i]; i++) {
        if (i == n - 1)
            return;
    }
    {
        t_scope;
        volatile uint32_t count[sizeof(type_t)][256] = { { 0, } };
#ifdef utype_t
        const type_t *r = base;
#else
        const uint8_t *bp = (const uint8_t *)base;
#endif
        type_t *p1, *p2;

        /* Achtung little endian version */
        for (size_t i = 0; i < n; i++) {
#ifdef utype_t
            utype_t b = TRANSLATE(*r);
            const uint8_t *bp = (const uint8_t *)&b;
#endif

            count[0][bp[0]]++;
            if (sizeof(type_t) > 1) {
                count[1][bp[1]]++;
                if (sizeof(type_t) > 2) {
                    count[2][bp[2]]++;
                    count[3][bp[3]]++;
                    if (sizeof(type_t) > 4) {
                        count[4][bp[4]]++;
                        count[5][bp[5]]++;
                        count[6][bp[6]]++;
                        count[7][bp[7]]++;
                    }
                }
            }
#ifdef utype_t
            r++;
#else
            bp += sizeof(type_t);
#endif
        }

        p2 = t_new_raw(type_t, n);
        p1 = base;

        for (size_t shift = 0; shift < sizeof(type_t); shift++) {
            volatile uint32_t *cp = count[shift];
            size_t cc, pos = 0;

            for (cc = 0; cc < 256; cc++) {
                size_t slot = cp[cc];

                cp[cc] = pos;
                pos   += slot;
                if (slot == n)
                    break;
            }
            if (cc == 256) {
                for (size_t i = 0; i < n; i++) {
#ifdef utype_t
                    utype_t b = TRANSLATE(p1[i]);
                    uint8_t k = ((const uint8_t *)&b)[shift];
#else
                    uint8_t k = ((const uint8_t *)&p1[i])[shift];
#endif

                    p2[cp[k]++] = p1[i];
                }
                SWAP(type_t *, p1, p2);
            }
        }
        if (p1 != base)
            p_copy(base, p1, n);
    }
#ifndef NDEBUG
    /* Check if array is already sorted */
    for (size_t i = 1; base[i - 1] <= base[i]; i++) {
        if (i == n - 1)
            return;
    }
    e_panic("should not happen");
#endif
}
#endif

#ifdef uniq
# ifdef SIMPLE_SORT
size_t uniq(type_t data[], size_t len)
{
    uint64_t flags[BITS_TO_ARRAY_LEN(uint64_t, 1 << bitsizeof(type_t))] = { 0, };
    size_t pos = 0;

    for (size_t i = 0; i < len; i++) {
        SET_BIT(flags, data[i]);
    }

    for (int i = 0; i < countof(flags); i++) {
        while (flags[i]) {
            int bit = bsf64(flags[i]);

            RST_BIT(&flags[i], bit);
            data[pos++] = bitsizeof(flags[i]) * i + bit;
        }
    }
    return pos;
}

# else

size_t uniq(type_t data[], size_t len)
{
    for (size_t i = 1; i < len; i++) {
        if (unlikely(data[i - 1] == data[i])) {
            type_t *end = data + len;
            type_t *w   = data + i;
            type_t *r   = data + i + 1;

            for (;;) {
                while (r < end && *r == w[-1])
                    r++;
                if (r == end)
                    break;
                *w++ = *r++;
            }
            len = w - data;
            break;
        }
    }
    return len;
}
# endif
#endif

#undef TRANSLATE
#undef utype_t
#undef TYPE_MIN
#undef type_t
#undef dsort
#undef uniq
#undef SIMPLE_SORT
