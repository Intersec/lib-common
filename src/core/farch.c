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

#include <lib-common/farch.h>
#include <lib-common/container-qhash.h>
#include <lib-common/qlzo.h>

qm_khptr_ckey_t(persisted, farch_entry_t, lstr_t);

static struct {
    qm_t(persisted) persisted;
} farch_g;
#define _G  farch_g

/* {{{ Public API */

static lstr_t t_farch_aggregate(const farch_entry_t *entry)
{
    char *contents = t_new_raw(char, entry->compressed_size);
    char *tail = contents;
    int compressed_size = 0;

    for (int i = 0; i < entry->nb_chunks; i++) {
        lstr_t chunk = entry->chunks[i];
        lstr_t content_chunk = LSTR_INIT(tail, chunk.len);

        compressed_size += chunk.len;
        if (!expect(compressed_size <= entry->compressed_size)) {
            return LSTR_NULL_V;
        }
        lstr_unobfuscate(chunk, chunk.len, content_chunk);
        tail += chunk.len;
    }

    if (!expect(compressed_size == entry->compressed_size)) {
        return LSTR_NULL_V;
    }

    return LSTR_INIT_V(contents, entry->compressed_size);
}

char *farch_get_filename(const farch_entry_t *entry, char *name)
{
    lstr_t out = LSTR_INIT(name, entry->name.len);

    if (!entry->name.s) {
        return NULL;
    }
    lstr_unobfuscate(entry->name, entry->nb_chunks, out);
    name[entry->name.len] = '\0';
    return name;
}

/** Get compressed farch entry.
 *
 * Get a farch entry by its name in a farch archive.
 *
 * \return  a pointer on the (compressed) farch entry if found,
 *          NULL otherwise.
 */
static const farch_entry_t *farch_get_entry(const farch_entry_t files[],
                                            const char *name)
{
    char real_name[FARCH_MAX_FILENAME];
    int len = strlen(name);

    for (; files->name.len > 0; files++) {
        if (len == files->name.len
        &&  strequal(farch_get_filename(files, real_name), name))
        {
            return files;
        }
    }
    return NULL;
}

/** Unarchive and append a '\0' if needed. */
lstr_t t_farch_unarchive(const farch_entry_t *entry)
{
    lstr_t res;
    char real_name[FARCH_MAX_FILENAME];

    res = LSTR_INIT_V(t_new_raw(char, entry->size + 1), entry->size + 1);

    do {
        t_scope;
        lstr_t contents;

        contents = t_farch_aggregate(entry); /* (and unobfuscate) */
        if (!contents.s) {
            goto error;
        }
        if (entry->compressed_size == entry->size) {
            memcpy(res.v, contents.s, entry->size);
            res.len = entry->size;
        } else { /* uncompress */
            res.len = qlzo1x_decompress_safe(res.v, entry->size,
                                             ps_initlstr(&contents));
            if (res.len != entry->size) {
                goto error;
            }
        }
    } while (0);

    res.v[res.len] = '\0';

    return res;

  error:
    lstr_unobfuscate(entry->name, entry->nb_chunks,
                     LSTR_INIT_V(real_name, entry->name.len));
    real_name[entry->name.len] = '\0';
    e_panic("cannot uncompress farch entry `%s`", real_name);
}

lstr_t farch_unarchive_persist(const farch_entry_t * nonnull entry)
{
    t_scope;
    lstr_t content;
    int pos;

    assert (MODULE_IS_LOADED(farch));

    pos = qm_reserve(persisted, &_G.persisted, entry, 0);
    if (pos & QHASH_COLLISION) {
        return _G.persisted.values[pos ^ QHASH_COLLISION];
    }

    content = t_farch_unarchive(entry);
    lstr_persists(&content);
    _G.persisted.values[pos] = content;

    return content;
}

lstr_t t_farch_get_data(const farch_entry_t *files, const char *name)
{
    const farch_entry_t *entry = name ? farch_get_entry(files, name) : files;

    return entry ? t_farch_unarchive(entry) : LSTR_NULL_V;
}

lstr_t farch_get_data_persist(const farch_entry_t *files, const char *name)
{
    const farch_entry_t *entry = name ? farch_get_entry(files, name) : files;

    return entry ? farch_unarchive_persist(entry) : LSTR_NULL_V;
}

/* }}} */
/* {{{ Module */

static int farch_initialize(void *arg)
{
    qm_init(persisted, &_G.persisted);
    return 0;
}

static int farch_shutdown(void)
{
    qm_deep_wipe(persisted, &_G.persisted, IGNORE, lstr_wipe);
    return 0;
}

MODULE_BEGIN(farch)
MODULE_END()

/* }}} */
