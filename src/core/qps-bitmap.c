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

#include <lib-common/arith.h>
#include <lib-common/qps-bitmap.h>

/* Deref {{{ */

static
qps_bitmap_dispatch_t *w_deref_dispatch(qps_bitmap_t *map,
                                        qps_bitmap_key_t key,
                                        bool create)
{
    qps_bitmap_node_t dispatch_node;
    STATIC_ASSERT(sizeof(qps_bitmap_dispatch_t) == 3 * QPS_PAGE_SIZE);

    qps_hptr_deref(map->qps, &map->root_cache);
    dispatch_node = map->root->roots[key.root];
    if (dispatch_node == 0) {
        if (!create) {
            return NULL;
        }
        qps_hptr_w_deref(map->qps, &map->root_cache);
        dispatch_node = qps_pg_map(map->qps, 3);
        qps_pg_zero(map->qps, dispatch_node, 3);
        map->root->roots[key.root] = dispatch_node;
    }
    return qps_pg_deref(map->qps, dispatch_node);
}

static
uint64_t *w_deref_leaf(qps_bitmap_t *map, qps_bitmap_dispatch_t **dispatch,
                       qps_bitmap_key_t key, bool create)
{
    qps_bitmap_node_t leaf_node;

    if (dispatch == NULL || *dispatch == NULL) {
        return NULL;
    }

    leaf_node = (*(*dispatch))[key.dispatch].node;
    if (leaf_node == 0) {
        const uint32_t pages = map->root->is_nullable ? 2 : 1;
        if (!create) {
            return NULL;
        }
        *dispatch = w_deref_dispatch(map, key, false);
        assert (*dispatch);
        leaf_node = qps_pg_map(map->qps, pages);
        qps_pg_zero(map->qps, leaf_node, pages);
        (*(*dispatch))[key.dispatch].node = leaf_node;
        (*(*dispatch))[key.dispatch].active_bits = 0;
    }
    return qps_pg_deref(map->qps, leaf_node);
}

static
void delete_leaf(qps_bitmap_t *map, qps_bitmap_key_t key)
{
    qps_bitmap_dispatch_t *dispatch = w_deref_dispatch(map, key, false);
    qps_bitmap_node_t leaf_node;
    if (dispatch == NULL) {
        return;
    }

    leaf_node = (*dispatch)[key.dispatch].node;
    if (leaf_node == 0) {
        return;
    }

    qps_pg_unmap(map->qps, leaf_node);
    (*dispatch)[key.dispatch].node = 0;
    for (int i = 0; i < QPS_BITMAP_DISPATCH; i++) {
        if ((*dispatch)[i].node != 0) {
            return;
        }
    }
    qps_hptr_w_deref(map->qps, &map->root_cache);
    qps_pg_unmap(map->qps, map->root->roots[key.root]);
    map->root->roots[key.root] = 0;
}

static
void delete_nodes(qps_bitmap_t *map)
{
    for (int i = 0; i < QPS_BITMAP_ROOTS; i++) {
        const qps_bitmap_dispatch_t *dispatch;
        const void *buf;
        int pos = 0;

        if (map->root->roots[i] == 0) {
            continue;
        }
        dispatch = qps_pg_deref(map->qps, map->root->roots[i]);
        buf      = *dispatch;

        /* scan_non_zero16 return the first non nul u16 position in range
         * [pos, len] in given u16 buffer buf.
         * We're looking for a nul node in qps_bitmap_dispatch_t which is
         * (u32int_t) node + (uint16_t) active_bits
         * we assume active_bits == 0 if node == 0
         */
        STATIC_ASSERT(sizeof(qps_bitmap_dispatch_t)
            == QPS_BITMAP_DISPATCH * 3 * sizeof(uint16_t));
        while ((pos = scan_non_zero16(buf, pos, 3 * QPS_BITMAP_DISPATCH)) >= 0) {
            qps_bitmap_node_t node;
            int p = pos / 3;
            int r = pos % 3;

            node = (*dispatch)[p].node;
            if (!expect(node > 0)) {
                /* XXX: "node" is not supposed to be 0 here, but it was
                 * observed on some production platforms; when it happens,
                 * following values of "node" are garbage and thus
                 * qps_pg_unmap crashes, so just avoid the crash.
                 */
                break;
            }
            qps_pg_unmap(map->qps, node);
            pos += 3 - r;
        }
        qps_pg_unmap(map->qps, map->root->roots[i]);
    }
}

static
void unload_nodes(qps_bitmap_t *map)
{
    for (int i = 0; i < QPS_BITMAP_ROOTS; i++) {
        const qps_bitmap_dispatch_t *dispatch;
        if (map->root->roots[i] == 0) {
            continue;
        }
        dispatch = qps_pg_deref(map->qps, map->root->roots[i]);
        for (int j = 0; j < QPS_BITMAP_DISPATCH; j++) {
            if ((*dispatch)[j].node == 0) {
                continue;
            }
            qps_pg_unload(map->qps, (*dispatch)[j].node);
        }
        qps_pg_unload(map->qps, map->root->roots[i]);
    }
}

/* }}} */
/* Public API {{{ */

qps_handle_t qps_bitmap_create(qps_t *qps, bool is_nullable)
{
    qps_bitmap_root_t *map;
    qps_hptr_t cache;

    map = qps_hptr_alloc(qps, sizeof(qps_bitmap_root_t), &cache);
    p_clear(map, 1);
    memcpy(map->sig, QPS_BITMAP_SIG, countof(map->sig));
    map->is_nullable = is_nullable;
    return cache.handle;
}

void qps_bitmap_destroy(qps_bitmap_t *map)
{
    qps_hptr_deref(map->qps, &map->root_cache);
    delete_nodes(map);
    qps_hptr_free(map->qps, &map->root_cache);
}

void qps_bitmap_clear(qps_bitmap_t *map)
{
    qps_hptr_w_deref(map->qps, &map->root_cache);
    delete_nodes(map);
    p_clear(map->root->roots, 1);
    map->bitmap_gen++;
}

void qps_bitmap_unload(qps_bitmap_t *map)
{
    qps_hptr_deref(map->qps, &map->root_cache);
    unload_nodes(map);
    map->bitmap_gen++;
}

qps_bitmap_state_t qps_bitmap_get(qps_bitmap_t *map, uint32_t row)
{
    qps_bitmap_key_t key = { .key = row };
    const qps_bitmap_dispatch_t *dispatch;
    const uint64_t *leaf;
    qps_bitmap_node_t dispatch_node;
    qps_bitmap_node_t leaf_node;

    qps_hptr_deref(map->qps, &map->root_cache);
    dispatch_node = map->root->roots[key.root];
    if (dispatch_node == QPS_HANDLE_NULL) {
        return map->root->is_nullable ? QPS_BITMAP_NULL : QPS_BITMAP_0;
    }

    dispatch  = qps_pg_deref(map->qps, dispatch_node);
    leaf_node = (*dispatch)[key.dispatch].node;
    if (leaf_node == 0) {
        return map->root->is_nullable ? QPS_BITMAP_NULL : QPS_BITMAP_0;
    }

    leaf = qps_pg_deref(map->qps, leaf_node);
    if (map->root->is_nullable) {
        uint64_t word = leaf[key.word_null];
        word >>= (key.bit_null * 2);
        if (!(word & 0x2)) {
            return QPS_BITMAP_NULL;
        }
        return (qps_bitmap_state_t)(word & 0x1);
    } else {
        uint64_t word = leaf[key.word];
        word >>= key.bit;
        return (qps_bitmap_state_t)(word & 0x1);
    }
}

qps_bitmap_state_t qps_bitmap_set(qps_bitmap_t *map, uint32_t row)
{
    qps_bitmap_key_t key = { .key = row };
    qps_bitmap_dispatch_t *dispatch;
    uint64_t *leaf;

    map->bitmap_gen++;
    dispatch = w_deref_dispatch(map, key, true);
    leaf     = w_deref_leaf(map, &dispatch, key, true);

    if (map->root->is_nullable) {
        uint64_t word = leaf[key.word_null];
        word >>= (key.bit_null * 2);
        if (!(word & 0x2)) {
            word = (UINT64_C(0x3) << (key.bit_null * 2));
            leaf[key.word_null] |= word;
            (*dispatch)[key.dispatch].active_bits++;
            return QPS_BITMAP_NULL;
        } else
        if (!(word & 0x1)) {
            word = (UINT64_C(0x3) << (key.bit_null * 2));
            leaf[key.word_null] |= word;
            return QPS_BITMAP_0;
        }
        return QPS_BITMAP_1;
    } else {
        uint64_t word = UINT64_C(1) << key.bit;
        if (!(leaf[key.word] & word)) {
            leaf[key.word] |= word;
            (*dispatch)[key.dispatch].active_bits++;
            return QPS_BITMAP_0;
        }
        return QPS_BITMAP_1;
    }
}

qps_bitmap_state_t qps_bitmap_reset(qps_bitmap_t *map, uint32_t row)
{
    qps_bitmap_key_t key = { .key = row };
    qps_bitmap_dispatch_t *dispatch;
    uint64_t *leaf;

    map->bitmap_gen++;
    qps_hptr_deref(map->qps, &map->root_cache);
    dispatch = w_deref_dispatch(map, key, map->root->is_nullable);
    leaf = w_deref_leaf(map, &dispatch, key, map->root->is_nullable);
    if (leaf == NULL) {
        return QPS_BITMAP_0;
    }

    if (map->root->is_nullable) {
        uint64_t word = leaf[key.word_null];
        uint64_t mask;
        word >>= (key.bit_null * 2);
        if (!(word & 0x2)) {
            mask = (UINT64_C(0x3) << (key.bit_null * 2));
            word = (UINT64_C(0x2) << (key.bit_null * 2));
            leaf[key.word_null] &= ~mask;
            leaf[key.word_null] |= word;
            (*dispatch)[key.dispatch].active_bits++;
            return QPS_BITMAP_NULL;
        } else
        if ((word & 0x1)) {
            mask = (UINT64_C(0x3) << (key.bit_null * 2));
            word = (UINT64_C(0x2) << (key.bit_null * 2));
            leaf[key.word_null] &= ~mask;
            leaf[key.word_null] |= word;
            return QPS_BITMAP_1;
        }
        return QPS_BITMAP_0;
    } else {
        uint64_t word = UINT64_C(1) << key.bit;
        if ((leaf[key.word] & word)) {
            leaf[key.word] &= ~word;
            (*dispatch)[key.dispatch].active_bits--;
            if ((*dispatch)[key.dispatch].active_bits == 0) {
                delete_leaf(map, key);
            }
            return QPS_BITMAP_1;
        }
        return QPS_BITMAP_0;
    }
}

qps_bitmap_state_t qps_bitmap_remove(qps_bitmap_t *map, uint32_t row)
{
    qps_bitmap_key_t key = { .key = row };
    qps_bitmap_dispatch_t *dispatch;
    uint64_t *leaf;

    map->bitmap_gen++;
    dispatch = w_deref_dispatch(map, key, false);
    leaf = w_deref_leaf(map, &dispatch, key, false);
    if (leaf == NULL) {
        return map->root->is_nullable ? QPS_BITMAP_NULL : QPS_BITMAP_0;
    }

    if (map->root->is_nullable) {
        qps_bitmap_state_t previous;
        uint64_t word = leaf[key.word_null];
        uint64_t mask;
        word >>= (key.bit_null * 2);
        if (!(word & 0x2)) {
            return QPS_BITMAP_NULL;
        } else {
            mask = (UINT64_C(0x3) << (key.bit_null * 2));
            leaf[key.word_null] &= ~mask;
            (*dispatch)[key.dispatch].active_bits--;
            previous = (qps_bitmap_state_t)(word & 0x1);
            if ((*dispatch)[key.dispatch].active_bits == 0) {
                delete_leaf(map, key);
            }
            return previous;
        }
    } else {
        uint64_t word = UINT64_C(1) << key.bit;
        if ((leaf[key.word] & word)) {
            leaf[key.word] &= ~word;
            (*dispatch)[key.dispatch].active_bits--;
            if ((*dispatch)[key.dispatch].active_bits == 0) {
                delete_leaf(map, key);
            }
            return QPS_BITMAP_1;
        }
        return QPS_BITMAP_0;
    }

}

void qps_bitmap_compute_stats(qps_bitmap_t *map, size_t *_memory,
                              uint32_t *_entries, uint32_t *_slots)
{
    size_t   memory  = 0;
    uint32_t entries = 0;
    uint32_t slots   = 0;

    qps_hptr_deref(map->qps, &map->root_cache);
    for (int i = 0; i < QPS_BITMAP_ROOTS; i++) {
        if (map->root->roots[i]) {
            const qps_bitmap_dispatch_t *dispatch;

            memory  += 3 * QPS_PAGE_SIZE;
            dispatch = qps_pg_deref(map->qps, map->root->roots[i]);

            for (int j = 0; j < QPS_BITMAP_DISPATCH; j++) {
                if ((*dispatch)[j].node) {
                    if (map->root->is_nullable) {
                        memory += 2 * QPS_PAGE_SIZE;
                    } else {
                        memory += QPS_PAGE_SIZE;
                    }
                    entries += (*dispatch)[j].active_bits;
                    slots   += QPS_BITMAP_LEAF;
                }
            }
        }
    }

    *_memory  = memory;
    *_entries = entries;
    *_slots   = slots;
}

/* }}} */
/* Debugging tool {{{ */

void qps_bitmap_get_qps_roots(qps_bitmap_t *map, qps_roots_t *roots)
{
    qps_hptr_deref(map->qps, &map->root_cache);
    for (int i = 0; i < QPS_BITMAP_ROOTS; i++) {
        const qps_bitmap_dispatch_t *dispatch;

        if (map->root->roots[i] == 0) {
            continue;
        }
        qv_append(&roots->pages, map->root->roots[i]);
        dispatch = qps_pg_deref(map->qps, map->root->roots[i]);
        for (int j = 0; j < QPS_BITMAP_DISPATCH; j++) {
            if ((*dispatch)[j].node == 0) {
                continue;
            }
            qv_append(&roots->pages, (*dispatch)[j].node);
        }
    }
    qv_append(&roots->handles, map->root_cache.handle);
}

void qps_bitmap_debug_print(qps_bitmap_t *map)
{
    fprintf(stderr, "QPS: debugging bitmap\n");
    fprintf(stderr, "map:\n"
            " \\bitmap_gen: %u\n"
            " \\nullable: %s\n",
            map->bitmap_gen, map->root->is_nullable ? "True" : "False");

    fprintf(stderr, " \\keys:\n");
    qps_bitmap_for_each_safe(en, map) {
        fprintf(stderr, "  \\en.key: %u\n", en.key.key);
    }
    qps_hptr_deref(map->qps, &map->root_cache);
    for (int i = 0; i < QPS_BITMAP_ROOTS; i++) {
        qps_bitmap_node_t root = map->root->roots[i];

        if (root) {
            const qps_bitmap_dispatch_t *dispatch;
            uint32_t nil_nodes = 0;

            fprintf(stderr, "  root node %d: " QPS_PG_FMT "\n",
                    i, QPS_PG_ARG(root));

            dispatch = qps_pg_deref(map->qps, map->root->roots[i]);
            for (int j = 0; j < QPS_BITMAP_DISPATCH; j++) {
                qps_bitmap_node_t node = (*dispatch)[j].node;

                if (node) {
                    if (nil_nodes) {
                        fprintf(stderr, "    dispatch %u nodes nil\n",
                                nil_nodes);
                        nil_nodes = 0;
                    }

                    fprintf(stderr, "    dispatch node %d: " QPS_PG_FMT "\n",
                            j, QPS_PG_ARG(node));
                    fprintf(stderr, "     \\active_bits: %u\n",
                            (*dispatch)[j].active_bits);
                } else {
                    nil_nodes++;
                }
            }
        }
    }
}

/* }}} */
