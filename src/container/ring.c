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

#include <lib-common/container-ring.h>

void generic_ring_ensure(generic_ring *r, int newlen, int el_siz)
{
    int cursize = r->size;

    if (unlikely(newlen < 0 || newlen * el_siz < 0))
        e_panic("trying to allocate insane amount of RAM");

    if (newlen <= r->size)
        return;

    r->size = p_alloc_nr(r->size);
    if (r->size < newlen)
        r->size = newlen;
    r->tab = irealloc(r->tab, r->len * el_siz, r->size * el_siz, 0,
                      MEM_RAW | MEM_LIBC);

    /* if elements are split in two parts. Move the shortest one */
    if (r->first + r->len > cursize) {
        char *base = r->tab;
        int right_len = cursize - r->first;
        int left_len  = r->len - right_len;

        if (left_len > right_len || left_len > r->size - cursize) {
            memmove(base + el_siz * (r->size - right_len),
                    base + el_siz * r->first, el_siz * right_len);
            r->first = r->size - right_len;
        } else {
            memcpy(base + el_siz * cursize, base, el_siz * left_len);
        }
    }
}
