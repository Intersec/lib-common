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

#ifndef IS_LIB_COMMON_FARCH_H
#define IS_LIB_COMMON_FARCH_H

#include <lib-common/core.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/* {{{ Private API */

#define FARCH_MAX_SYMBOL_SIZE  128

/** Farch archives data structures.
 *
 * XXX: You should never use these strutures directly: they are only exposed
 *      for farchc and the files it generates.
 *
 * Farch is used to store compressed file contents as C obfuscated symbols.
 *
 * File contents are obfuscated by splitting them in small chunks which are
 * xored with a randomly selected string (the string is a symbol which, in
 * principle, already exists in the binary). Each chunk is represented as a
 * farch_data_t structure.
 *
 * A farch archive is an array of farch_entry_t. Each of those represents a
 * single file with its obfuscated filename, its contents as described above,
 * and the size of the contents after and before compression. The obfuscation
 * method for the filename is the same than for the contents.
 */
typedef lstr_t farch_data_t;

typedef struct farch_entry_t {
    lstr_t name; /* obfuscated filename, its length is the obfuscation key */
    const lstr_t * nonnull chunks; /* obfuscated content chunks */
    int compressed_size;
    int size;
    int nb_chunks;
} farch_entry_t;

/* }}} */
/* {{{ Public API */

#define FARCH_MAX_FILENAME  (2 * PATH_MAX)

/** Get the filename of this entry.
 *
 * This function can be used to iterate on names entries: entries are stored
 * in an array terminating by an empty entry (name.len == 0).
 *
 *     for (const farch_entry_t *entry = entries,
 *              char __buf[FARCH_MAX_FILENAME];
 *          name = farch_get_filename(entry, __buf);
 *          entry++)
 *
 * \param[in]  file  a farch entry.
 * \param[in|out]  name  a buffer to write the name -- should contains
 *                       FARCH_MAX_FILENAME bytes.
 * \return  NULL if there is no entry or if an error occurs, name otherwise.
 */
char * nullable farch_get_filename(const farch_entry_t * nonnull file,
                                   char * nonnull name_outbuf);

/** Get the uncompressed content of a farch entry.
 *
 * Finds a farch entry by its name in a farch archive, and return its
 * uncompressed content.  If the name is not provided, the content of the
 * first entry is returned.
 *
 * \return  the uncompressed content if the entry is found,
 *          LSTR_NULL otherwise.
 */
lstr_t t_farch_get_data(const farch_entry_t * nonnull files,
                        const char * nullable name);

/** Similar to t_farch_get_uncompressed, but make the data persistent.
 *
 * The persisted data will be freed when the farch module is released.
 */
lstr_t farch_get_data_persist(const farch_entry_t * nonnull files,
                              const char * nullable name);

/* }}} */

/** Farch module.
 *
 * Using it is necessary only if \ref t_farch_uncompress_persist is used.
 */
MODULE_DECLARE(farch);

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif /* IS_LIB_COMMON_FARCH_H */
