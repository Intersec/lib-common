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

#ifdef F_PROTO
#  define  __F_PROTO  , F_PROTO
#  define  __F_ARGS   , F_ARGS
#else
#  define  __F_PROTO
#  define  __F_ARGS
#endif

static void
F(qhash_move)(qhash_t *qh, qhash_hdr_t *old, uint64_t pos __F_PROTO);

static inline uint32_t
F(qhash_put_ll)(qhash_t *qh, qhash_hdr_t *old, bool check_collision,
                uint32_t h, const key_t k, uint64_t *out __F_PROTO)
{
    uint32_t inc, ghost = UINT32_MAX;
    uint64_t pos;
    qhash_hdr_t *hdr = &qh->hdr;

    pos = h % hdr->size;
    inc = 1 + h % (hdr->size - 1);

    for (;;) {
        size_t flags;

        while ((flags = qhash_slot_get_flags(hdr->bits, pos)) != 0) {
            if (flags & 1) {
                if (check_collision
#ifdef MAY_CACHE_HASHES
                &&  (!qh->hashes || qh->hashes[pos] == h)
#endif
                &&  iseqK(qh, getK(qh, pos), k))
                {
                    *out = pos;
                    return QHASH_COLLISION;
                }
            } else {
                if (ghost == UINT32_MAX)
                    ghost = pos;
            }
            if ((pos += inc) >= hdr->size)
                pos -= hdr->size;
        }

        if (ghost != UINT32_MAX) {
            qhash_slot_inv_flags(hdr->bits, ghost);
            qh->ghosts--;
            pos = ghost;
            break;
        }

        if (likely(!old || pos >= old->len || !qhash_slot_is_set(old, pos))) {
            SET_BIT(hdr->bits, 2 * pos);
            break;
        }
        F(qhash_move)(qh, old, pos __F_ARGS);
    }
    hdr->len++;
    *out = pos;
    return 0;
}

static inline int32_t
F(qhash_get_ll)(const qhash_t *qh, const qhash_hdr_t *hdr,
                uint32_t h, const key_t k __F_PROTO)
{
    if (hdr->len) {
        uint32_t pos = h % hdr->size;
        uint32_t inc = 1 + h % (hdr->size - 1);
        size_t flags;

        while ((flags = qhash_slot_get_flags(hdr->bits, pos)) != 0) {
            if (flags & 1) {
#ifdef MAY_CACHE_HASHES
                if (!qh->hashes || qh->hashes[pos] == h)
#endif
                {
                    if (iseqK(qh, getK(qh, pos), k))
                        return pos;
                }
            }
            if ((pos += inc) >= hdr->size)
                pos -= hdr->size;
        }
    }
    return -1;
}

static void
F(qhash_move)(qhash_t *qh, qhash_hdr_t *old, uint64_t pos __F_PROTO)
{
    uint64_t v_size = qh->v_size;
    qv_t(u32) moves;

#ifdef QH_DEEP_COPY
    uint64_t k_size = qh->k_size;
    uint8_t  cycle_k[k_size];
#else
    key_t    cycle_k  = 0;
#endif
#ifdef MAY_CACHE_HASHES
    uint32_t cycle_h  = 0;
#endif
    uint8_t  cycle_v[v_size];
    bool     has_loop = false;

    qv_inita(&moves, 1024);

    do {
        key_t    k = getK(qh, pos);
        uint32_t h = hashK(qh, pos, k);

        qv_append(&moves, pos);
        qhash_slot_inv_flags(old->bits, pos);
        F(qhash_put_ll)(qh, NULL, false, h, k, &pos __F_ARGS);

        /* if we have a cycle, we must stop. */
        if (pos == moves.tab[0]) {
            /* optimizes the case of cells hashing in place */
            if (moves.len == 1) {
                qh->hdr.len--;
                return;
            }
            has_loop = true;
            break;
        }

        /* loop until we find a cell that isn't occupied in the old view */
    } while (pos < old->len && qhash_slot_is_set(old, pos));

    if (has_loop) {
#ifdef QH_DEEP_COPY
        memcpy(cycle_k, getK(qh, pos), k_size);
#else
        cycle_k = getK(qh, pos);
#endif
#ifdef MAY_CACHE_HASHES
        if (qh->hashes)
            cycle_h = qh->hashes[pos];
#endif
        memcpy(cycle_v, qh->values + v_size * pos, v_size);
    }
    for (int i = moves.len; i-- > has_loop; ) {
        uint64_t newpos = pos;

        pos = moves.tab[i];
        putK(qh, newpos, getK(qh, pos));
#ifdef MAY_CACHE_HASHES
        if (qh->hashes)
            qh->hashes[newpos] = qh->hashes[pos];
#endif
        if (v_size) {
            memcpy(qh->values + v_size * newpos,
                   qh->values + v_size * pos, v_size);
        }
    }
    if (has_loop) {
        putK(qh, pos, cycle_k);
#ifdef MAY_CACHE_HASHES
        if (qh->hashes)
            qh->hashes[pos] = cycle_h;
#endif
        memcpy(qh->values + v_size * pos, cycle_v, v_size);
    }

    qh->hdr.len -= moves.len;
    qv_wipe(&moves);
}

static void
F(qhash_move_walk)(qhash_t *qh, qhash_hdr_t *old, uint32_t h __F_PROTO)
{
    uint32_t pos = h % old->size;
    uint32_t inc = 1 + h % (old->size - 1);
    size_t flags;

    while ((flags = qhash_slot_get_flags(old->bits, pos)) != 0) {
        if (flags & 1)
            F(qhash_move)(qh, old, pos __F_ARGS);
        if ((pos += inc) >= old->size)
            pos -= old->size;
    }
}

static void F(qhash_resize_do)(qhash_t *qh, qhash_hdr_t *old __F_PROTO)
{
    size_t  *bits, *end;
    size_t   word, mask;
    uint64_t pos;

    pos  = 2 * (uint64_t)old->len - 1;
    bits = old->bits + pos / bitsizeof(size_t);
    /* rehash upto (16 * (bitsizeof(size_t) / 2)) slots */
    end  = bits - 16;
    if (old->bits > end)
        end = old->bits;

    mask = QH_SETBITS_MASK & BITMASK_LE(size_t, pos);
    pos &= -bitsizeof(size_t);
    while ((word = *bits & mask)) {
        F(qhash_move)(qh, old, (pos + bsfsz(word)) / 2 __F_ARGS);
    }

    while (--bits >= end) {
        pos -= bitsizeof(size_t);
        while ((word = *bits & QH_SETBITS_MASK)) {
            F(qhash_move)(qh, old, (pos + bsfsz(word)) / 2 __F_ARGS);
        }
    }

    old->len = (end - old->bits) * bitsizeof(size_t) / 2;
    if (old->len == 0)
        qhash_resize_done(qh);
}

void F(qhash_seal)(qhash_t *qh __F_PROTO)
{
#ifndef NDEBUG
    e_assert(panic, qh->ghosts != UINT32_MAX, "hash table already sealed");
#endif

    /* Complete any pending resize. */
    while (qh->old) {
        F(qhash_resize_do)(qh, qh->old __F_ARGS);
    }

    /* Check for the need for a new resize. */
    if (qh->ghosts || qhash_should_resize(qh)) {
        qhash_resize_start(qh);
        while (qh->old) {
            F(qhash_resize_do)(qh, qh->old __F_ARGS);
        }
    }

    qh->ghosts = UINT32_MAX;
}

int32_t F(qhash_get)(qhash_t *qh, uint32_t h, const key_t k __F_PROTO)
{
#ifndef NDEBUG
    e_assert(panic, qh->ghosts != UINT32_MAX,
             "unsafe find operation performed on a sealed hash table");
#endif

    if (unlikely(qh->old != NULL)) {
        F(qhash_move_walk)(qh, qh->old, h __F_ARGS);
        F(qhash_resize_do)(qh, qh->old __F_ARGS);
    }

    return F(qhash_get_ll)(qh, &qh->hdr, h, k __F_ARGS);
}

int32_t F(qhash_safe_get)(const qhash_t *qh, uint32_t h, const key_t k __F_PROTO)
{
    int32_t pos = F(qhash_get_ll)(qh, &qh->hdr, h, k __F_ARGS);

    if (unlikely(qh->old != NULL) && pos < 0) {
        return F(qhash_get_ll)(qh, qh->old, h, k __F_ARGS);
    }
    return pos;
}

uint32_t F(__qhash_put)(qhash_t *qh, uint32_t h, const key_t k, uint32_t flags __F_PROTO)
{
    uint64_t pos, collision;

#ifndef NDEBUG
    e_assert(panic, qh->ghosts != UINT32_MAX,
             "insert operation performed on a sealed hash table");
#endif

    if (qhash_should_resize(qh)) {
        qhash_resize_start(qh);
    }

    if (unlikely(qh->old != NULL)) {
        F(qhash_move_walk)(qh, qh->old, h __F_ARGS);
        F(qhash_resize_do)(qh, qh->old __F_ARGS);
    }
    collision = F(qhash_put_ll)(qh, qh->old, true, h, k, &pos __F_ARGS);
#ifdef MAY_CACHE_HASHES
    if (qh->hashes)
        qh->hashes[pos] = h;
#endif
    return collision | pos;
}

#undef F
#undef key_t
#undef getK
#undef putK
#undef hashK
#undef iseqK
#undef __F_ARGS
#undef __F_PROTO
#undef F_PROTO
#undef F_ARGS
#undef MAY_CACHE_HASHES
#undef QH_DEEP_COPY
