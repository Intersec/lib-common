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

#ifndef IS_FILE_BIN_H
#define IS_FILE_BIN_H

/** \file  Common binary file library header.
 *
 * This file contains the APIs to read and write binary files.
 *
 * A binary file is a file containing binary data, each entry being preceded
 * by its length as a little-endian unsigned 32 integer. The files are built
 * so that there is the offset of the next entry at each offset multiple of
 * the slot size. Thus, binary files are robust to corruptions (as we can
 * easily skip some corrupted entries) and it is possible to easily perform
 * reverse runs on it.
 *
 * Example:
 *
 * Here is the structure of a binary file, in version 1,  with a size of slot
 * of 30 with three records: A, of size 11, B, of size 10 and C of size 14.
 * Bits 0 to 16 contain file header, which indicates the library version.
 *
 *    0     1     2     3     4     5     6     7     8     9    10
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 *  0 | 'I' | 'S' | '_' | 'b' | 'i' | 'n' | 'a' | 'r' | 'y' | '/' |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 10 | 'v' | '0' | '1' | '.' | '0' | '0' | SS  | SS  | SS  | SS  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 20 | SH  | SH  | SH  | SH  |sizeA|sizeA|sizeA|sizeA|  A  |  A  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 30 | SH  | SH  | SH  | SH  |  A  |  A  |  A  |  A  |  A  |  A  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 40 |  A  |  A  |  A  |sizeB|sizeB|sizeB|sizeB|  B  |  B  |  B  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 50 |  B  |  B  |  B  |  B  |  B  |  B  |  B  |  0  |  0  |  0  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 60 | SH  | SH  | SH  | SH  |sizeC|sizeC|sizeC|sizeC|  C  |  C  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 70 |  C  |  C  |  C  |  C  |  C  |  C  |  C  |  C  |  C  |  C  |
 *    *-----*-----*-----*-----*-----*-----*-----*-----*-----*-----*
 * 80 |  C  |  C  |
 *
 *  SS --> Slot Size, in little endian (here 30).
 *  SH --> Slot Header
 */

#include <lib-common/container-qvector.h>

#define FILE_BIN_DEFAULT_SLOT_SIZE  (1 << 20) /* 1 Megabyte */

typedef struct file_bin_t {
    bool read_mode;

    /* Read/Write mode common fields. */
    FILE     *f;
    off_t     cur;
    lstr_t    path;
    uint32_t  slot_size;
    uint16_t  version;

    /* Read mode fields. */
    uint32_t  length;
    byte     *map;
    sb_t      record_buf;
} file_bin_t;
static inline file_bin_t *file_bin_init(file_bin_t *var)
{
    p_clear(var, 1);
    sb_init(&var->record_buf);
    return var;
}
GENERIC_NEW(file_bin_t, file_bin);
static inline void file_bin_wipe(file_bin_t *var)
{
    lstr_wipe(&var->path);
    sb_wipe(&var->record_buf);
}
GENERIC_DELETE(file_bin_t, file_bin);

/* {{{ Writing */

/** Open a binary file for writing.
 *
 * This function will do a write-only opening on the binary file specified,
 * making it ready to be written.
 *
 * \param[in] path       The path to the binary file to write.
 * \param[in] slot_size  The slot size to use for this file. Use 0 to use
 *                       FILE_BIN_DEFAULT_SLOT_SIZE.
 * \param[in] truncate   Tells if the file should be truncated if it already
 *                       exists.
 *
 * \return the newly created file_bin_t on success, NULL otherwise.
 */
file_bin_t *file_bin_create(lstr_t path, uint32_t slot_size, bool truncate);

/** Put a record in a binary file.
 *
 * \param[in]  file  The file to put the binary data in.
 * \param[in]  data  Binary data.
 * \param[in]  len   Length of the data.
 *
 * \return  0 on success, negative value otherwise.
 */
__must_check__
int file_bin_put_record(file_bin_t *file, const void *data, uint32_t len);

__must_check__
static inline int file_bin_put_record_lstr(file_bin_t *file, lstr_t data) {
    return file_bin_put_record(file, data.data, data.len);
}

__must_check__
static inline int file_bin_put_record_sb(file_bin_t *file, sb_t data) {
    return file_bin_put_record(file, data.data, data.len);
}

__must_check__
int file_bin_truncate(file_bin_t *file, off_t pos);

__must_check__
int file_bin_flush(file_bin_t *file);

__must_check__
int file_bin_sync(file_bin_t *file);

/* }}} */
/* {{{ Reading */

/** Open a binary file for reading.
 *
 * This function will do a read-only opening on the binary file specified,
 * making it ready to be parsed.
 *
 * \param[in] path  The path to the binary file to read.
 *
 * \return the newly created file_bin_t on success, NULL otherwise.
 */
file_bin_t *file_bin_open(lstr_t path);

/** Refresh the mapping of a binary file if needed.
 *
 * If binary file has changed, its content will be reloaded in memory.
 *
 * \param[in] file  The binary file to refresh.
 *
 * \return 0 on success, a negative value otherwise.
 */
__must_check__
int file_bin_refresh(file_bin_t *file);

/** Read last records from a binary file.
 *
 * This function will parse the last records (starting from the end of the
 * file) of a binary file.
 *
 * \param[in]  file   The binary file we want to read.
 * \param[in]  count  Maximum number of records wanted. If negative, every
 *                    record will be fetched.
 * \param[out] out    The vector that will be filled with records.
 *
 * \return  0 on success, negative value otherwise.
 */
__must_check__
int t_file_bin_get_last_records(file_bin_t *file, int count, qv_t(lstr) *out);

/** Get next record from a file.
 *
 * This function will parse the next record of a binary file from its current
 * position.
 *
 * \param[in]  file   The binary file to read.
 *
 * \return  a lstr_t containing the record on success, LSTR_NULL_V on failure
 *          or on file's end reaching (use \ref file_bin_is_finished to know
 *          if the file's end is reached or not). Note that the memory pointed
 *          by the lstr may becomes invalid after the next call to
 *          file_bin_get_next_entry.
 */
lstr_t file_bin_get_next_record(file_bin_t *file);

/** Iterates on each record of a file.
 *
 * Create a loop to iterate on each record (starting from the current reading
 * position) of the file. This macro should be used when it is possible
 * instead of having a manual loop built with \ref t_file_bin_get_next_record,
 * to avoid the loading of the entire file in memory.
 *
 * Note that the memory pointed by 'entry' lstr_t becomes invalid on the next
 * iteration.
 *
 * \param[in]  file   The binary file to read.
 * \param[in]  entry  The name of the lstr_t that will be created and which
 *                    will contains each record.
 */
#define file_bin_for_each_entry(file, entry)                                 \
    for (lstr_t entry = file_bin_get_next_record(file);                      \
         entry.s; entry = file_bin_get_next_record(file))

/** Tell if the parsing of a binary file is finished or not.
 *
 * \return  true if the current offset of the file has reached the end of the
 *          file, false otherwise.
 */
static inline bool file_bin_is_finished(file_bin_t *file)
{
    return file->cur >= file->length;
}

/** Tell if the file_bin has at least \p len bytes from the current position.
 */
static inline bool file_bin_has(file_bin_t *file, off_t len)
{
    return file->cur + len <= file->length;
}

/** Move the current file position to the one given as argument.
 *
 * Be careful as this function is kind of tricky. It will move the current
 * reading position of a file_bin (which MUST have been opened with \ref
 * file_bin_open before) to the one given as argument. If the new position is
 * not the beginning of a slot or the beginning of an entry, it will break the
 * record reading for the slot you are in.
 *
 * \param[in]  file  the file_bin concerned by the seek.
 * \param[in]  pos   the new reading position.
 *
 * \return  0 on success, a negative value otherwise.
 */
__must_check__
int _file_bin_seek(file_bin_t *file, off_t pos);

/* }}} */

/** Close a previously opened or created file_bin.
 *
 * \param[in]  file  the file to close.
 *
 * \return  0 on success, a negative value on failure.
 */
__must_check__
int file_bin_close(file_bin_t **file);

#endif /* IS_FILE_BIN_H */
