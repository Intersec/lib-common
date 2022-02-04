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
#include <lib-common/log.h>
#include <lib-common/file-bin.h>
#include <lib-common/unix.h>

/* File header */
#define CURRENT_VERSION  1

/* Should always be 16 characters long */
#define SIG_0100  "IS_binary/v01.0\0"
#define SIG       SIG_0100

typedef struct file_bin_header_t {
    char   version[16];
    le32_t slot_size;
} file_bin_header_t;

#define HEADER_VERSION_SIZE  16
#define HEADER_SIZE(file) \
    (file->version == 0 ? 0 : ssizeof(file_bin_header_t))

/* Slot header */
typedef le32_t slot_hdr_t;

#define SLOT_HDR_SIZE(file)  \
    ((file)->version == 0 ? 0 : ssizeof(slot_hdr_t))

/* Record header */
typedef le32_t rc_hdr_t;
#define RC_HDR_SIZE  ssizeof(rc_hdr_t)

static struct {
    logger_t logger;
} file_bin_g = {
#define _G  file_bin_g
    .logger = LOGGER_INIT_INHERITS(NULL, "file_bin"),
};

/* {{{ Helpers */

static uint32_t file_bin_remaining_space_in_slot(const file_bin_t *file)
{
    return file->slot_size - (file->cur % file->slot_size);
}

static off_t file_bin_get_prev_slot(const file_bin_t *file, off_t pos)
{
    return pos < file->slot_size ? HEADER_SIZE(file)
                                 : pos - (pos % file->slot_size);
}

static bool is_at_slot_start(const file_bin_t *f)
{
    return f->cur % f->slot_size == 0 || f->cur == HEADER_SIZE(f);
}

static int file_bin_seek(file_bin_t *file, off_t offset, int whence)
{
    if (fseek(file->f, offset, whence) < 0) {
        return logger_error(&_G.logger, "cannot use fseek on file "
                            "'%*pM': %m", LSTR_FMT_ARG(file->path));
    }

    return 0;
}

static off_t file_bin_tell(const file_bin_t *file)
{
    off_t res = ftell(file->f);

    if (res < 0) {
        logger_error(&_G.logger, "cannot use ftell on file '%*pM': %m",
                     LSTR_FMT_ARG(file->path));
    }

    return res;
}

static off_t file_bin_get_entry_end_off(const file_bin_t *f, uint32_t d_len)
{
    uint32_t len = d_len;
    off_t res;
    uint32_t nb_slots;
    uint32_t remaining = file_bin_remaining_space_in_slot(f);

    if (is_at_slot_start(f)) {
        len += SLOT_HDR_SIZE(f);
    }
    res = f->cur + len;
    if (len <= remaining) {
        return res;
    }
    /* compute the number of extra slots needed to store the entry */
    nb_slots = DIV_ROUND_UP(len - remaining,
                            f->slot_size - SLOT_HDR_SIZE(f));
    res += nb_slots * SLOT_HDR_SIZE(f);

    return res;
}

static off_t file_bin_get_next_entry_off(const file_bin_t *f, uint32_t d_len)
{
    off_t res = file_bin_get_entry_end_off(f, d_len);
    uint32_t remaining = 0;

    if (res % f->slot_size) {
        remaining = f->slot_size - (res % f->slot_size);
    }
    /* else it means res is exactly at the end of a slot so remaining is 0 */

    if (remaining < RC_HDR_SIZE) {
        /* if there is not enough space in the slot to put the record header,
         * go to the beginning of the next slot
         */
        res += remaining + SLOT_HDR_SIZE(f);
    }
    return res;
}

/* }}} */
/* {{{ Reading */

static int file_bin_parse_header(lstr_t path, void *data, size_t len,
                                 uint16_t *version, uint32_t *slot_size)
{
    file_bin_header_t *header = data;

    if (len < sizeof(file_bin_header_t)) {
        logger_error(&_G.logger,
                     "not enough data in '%*pM' to parse header: %ju < %ju",
                     LSTR_FMT_ARG(path), len, sizeof(file_bin_header_t));
        return -1;
    }

    if (strequal(header->version, SIG_0100)) {
        *version = 1;
        *slot_size = le_to_cpu32p(&header->slot_size);
    } else {
        /* Unknown header. File is probably in version 0 */
        *version = 0;
        *slot_size = FILE_BIN_DEFAULT_SLOT_SIZE;
    }

    logger_trace(&_G.logger, 3, "parsed file header for '%*pM': version = %u,"
                 " slot size = %u", LSTR_FMT_ARG(path), *version, *slot_size);

    return 0;
}

int file_bin_refresh(file_bin_t *file)
{
    struct stat st;
    void *new_map = NULL;
    bool parse_header = false;

    assert (file->read_mode);

    if (fstat(fileno(file->f), &st) < 0) {
        return logger_error(&_G.logger, "cannot stat file '%*pM': %m",
                            LSTR_FMT_ARG(file->path));
    }

    if (file->length == st.st_size) {
        return 0;
    }

    assert (file->map || file->length == 0);
#ifdef __linux__
    if (file->map) {
        new_map = mremap(file->map, file->length, st.st_size, MREMAP_MAYMOVE);
    }
#else
    if (file->map) {
        munmap(file->map, file->length);
        file->map = NULL;
        new_map = NULL;
    }
#endif

    if (!file->map) {
        parse_header = true;
        new_map = mmap(NULL, st.st_size, PROT_READ,
                       MAP_SHARED, fileno(file->f), 0);
    }

    if (new_map == MAP_FAILED) {
        logger_error(&_G.logger, "cannot %smap file '%*pM': %m",
                     file->map ? "re" : "", LSTR_FMT_ARG(file->path));
        return -1;
    }

    if (parse_header) {
        RETHROW(file_bin_parse_header(file->path, new_map, st.st_size,
                                      &file->version, &file->slot_size));
    }

    file->map = new_map;
    file->length = st.st_size;

    return 0;
}

int _file_bin_seek(file_bin_t *file, off_t pos)
{
    /* If this one fails, you are probably looking for file_bin_truncate. */
    assert (file->read_mode);

    THROW_ERR_UNLESS(expect(pos <= file->length));

    file->cur = pos;

    return 0;
}

static int file_bin_get_cpu32(file_bin_t *file, uint32_t *res)
{
    pstream_t ps = ps_init(file->map + file->cur, sizeof(le32_t));
    le32_t le32;

    THROW_ERR_IF(!file_bin_has(file, sizeof(le32_t)));

    if (ps_get_le32(&ps, &le32) < 0) {
        return logger_error(&_G.logger, "cannot read le32 at offset '%ju' "
                            "for file '%*pM'", file->cur,
                            LSTR_FMT_ARG(file->path));
    }

    *res = le32;
    file->cur += sizeof(le32_t);

    return 0;
}

static int _file_bin_skip(file_bin_t *file, off_t toskip)
{
    THROW_ERR_IF(!file_bin_has(file, toskip));
    file->cur += toskip;
    return 0;
}

static int _file_bin_get_next_record(file_bin_t *file, lstr_t *rec)
{
    off_t prev_off = file->cur;
    off_t rec_end_off;
    uint32_t sz;
    uint32_t check_slot_hdr;
    bool is_spanning = false;

    if (file_bin_is_finished(file)) {
        return -1;
    }

    if (file->version > 0) {
        file->cur = MAX(file->cur, HEADER_SIZE(file));
    }

    sb_reset(&file->record_buf);

    sz = file_bin_remaining_space_in_slot(file);
    if (sz < RC_HDR_SIZE) {
        if (_file_bin_skip(file, sz) < 0) {
            return -1;
        }
    }

    if (file->version > 0) {
        while (is_at_slot_start(file)) {
            if (file_bin_get_cpu32(file, &sz) < 0
            ||  _file_bin_skip(file, sz) < 0)
            {
                goto error;
            }
        }
    }

    if (file_bin_is_finished(file)) {
        return -1;
    }
    if (file_bin_get_cpu32(file, &sz) < 0) {
        goto error;
    }

    rec_end_off = file_bin_get_entry_end_off(file, sz);
    if (rec_end_off > file->length) {
        slot_hdr_t tmp_size;

        /* There is not enough data in the file to read the record. This could
         * happen for two reasons:
         *  - the record header is corrupted and the length is non-sense;
         *    in that case, we want to skip this corrupted slot.
         *  - the record is being written and we do not have enough data yet;
         *    in that case, we want to stay here.
         *
         * In order to guess in which case we are, read the next slot header
         * (if available) in which there is also the information about the
         * offset of the end of the record. If the two offsets mismatch, we
         * are in the first case.
         */
        if (_file_bin_skip(file, file_bin_remaining_space_in_slot(file)) < 0
        ||  file_bin_get_cpu32(file, &tmp_size) < 0
        ||  rec_end_off == file->cur + tmp_size)
        {
            file->cur = prev_off;
            return -1;
        }
        logger_error(&_G.logger, "corrupted record length in file '%*pM' at "
                     "pos %jd", LSTR_FMT_ARG(file->path), prev_off);
        file->cur -= sizeof(slot_hdr_t);
        *rec = LSTR_NULL_V;
        return 0;
    }

    if (is_at_slot_start(file)
    &&  _file_bin_skip(file, SLOT_HDR_SIZE(file)) < 0)
    {
        goto error;
    }

    if (sz == 0) {
        if (file->version == 0) {
            /* In V0, the end of the slots are filled with 0s. Go to error so
             * that we jump to next slot. */
            goto error;
        } else {
            *rec = LSTR_EMPTY_V;
            return 0;
        }
    }

    check_slot_hdr = file_bin_get_next_entry_off(file, sz);

    while (!file_bin_is_finished(file)) {
        uint32_t remaining = file_bin_remaining_space_in_slot(file);
        slot_hdr_t tmp_size;

        remaining = MIN(remaining, file->length - file->cur);

        if (sz <= remaining) {
            /* The last part of this record is in this slot. */
            lstr_t res = LSTR_DATA(file->map + file->cur, sz);

            file->cur += sz;

            if (!is_spanning) {
                *rec = res;
            } else {
                sb_add_lstr(&file->record_buf, res);
                *rec = LSTR_SB_V(&file->record_buf);
            }
            return 0;
        }

        assert (file->version > 0);

        /* Record spans on multiple slots. */
        if (!is_spanning) {
            is_spanning = true;
            sb_grow(&file->record_buf, sz);
        }

        sb_add(&file->record_buf, file->map + file->cur, remaining);
        sz -= remaining;
        file->cur += remaining;

        if (!is_at_slot_start(file)) {
            logger_error(&_G.logger, "corrupted file '%*pM', a slot start "
                         "was expected at pos %jd",
                         LSTR_FMT_ARG(file->path), file->cur);
            assert (false);
            goto error;
        }

        /* Consuming slot header */
        if (file_bin_get_cpu32(file, &tmp_size) < 0) {
            goto error;
        }

        if (tmp_size != check_slot_hdr - file->cur) {
            logger_error(&_G.logger, "buggy slot header in file '%*pM', "
                         "expected %jd, got %u, jumping to next slot",
                         LSTR_FMT_ARG(file->path), check_slot_hdr - file->cur,
                         tmp_size);
            goto error;
        }
    }

    assert (false);

  error:
    /* An error occured, try to jump to the next slot. */
    if (_file_bin_skip(file, file_bin_remaining_space_in_slot(file)) < 0) {
        /* There is not enough data in the file, rollback position. */
        file->cur = prev_off;
        return -1;
    }
    *rec = LSTR_NULL_V;
    return 0;
}

lstr_t file_bin_get_next_record(file_bin_t *file)
{
    int res;
    lstr_t rec = LSTR_NULL_V;

    assert (file->read_mode);

    do {
        res = _file_bin_get_next_record(file, &rec);
    } while (res >= 0 && !rec.s);

    return rec;
}

int t_file_bin_get_last_records(file_bin_t *file, int count, qv_t(lstr) *out)
{
    off_t save_cur = file->cur;
    off_t slot_off = file->length;
    off_t prev_slot;
    qv_t(lstr) tmp;

    assert (file->read_mode);

    t_qv_init(&tmp, count);

    do {
        prev_slot = slot_off;
        file->cur = slot_off = file_bin_get_prev_slot(file, prev_slot - 1);

        while (file->cur <= prev_slot - RC_HDR_SIZE
            && !file_bin_is_finished(file))
        {
            lstr_t res = file_bin_get_next_record(file);

            if (!res.s) {
                break;
            }
            qv_append(&tmp, t_lstr_dup(res));
        }

        qv_grow(out, tmp.len);
        for (int pos = tmp.len; pos-- > 0 && count > 0; count--) {
            qv_append(out, tmp.tab[pos]);
        }

        qv_clear(&tmp);
    } while (count > 0 && slot_off > HEADER_SIZE(file));

    file->cur = save_cur;

    return 0;
}

file_bin_t *file_bin_open(lstr_t path)
{
    file_bin_t *res;
    struct stat st;
    void *mapping = NULL;
    uint16_t version = 0;
    uint32_t slot_size = 0;
    lstr_t r_path = lstr_dup(path);
    FILE *file = fopen(r_path.s, "r");

    if (!file) {
        logger_error(&_G.logger, "cannot open file '%*pM: %m",
                     LSTR_FMT_ARG(path));
        goto error;
    }

    if (fstat(fileno(file), &st) < 0) {
        logger_error(&_G.logger, "cannot get stat on file '%*pM': %m",
                     LSTR_FMT_ARG(path));
        goto error;
    }

    if (st.st_size < 0) {
        logger_error(&_G.logger, "invalid size of binary file '%*pM'",
                     LSTR_FMT_ARG(path));
        goto error;
    }

    if (st.st_size > 0) {
        mapping = mmap(NULL, st.st_size, PROT_READ,
                       MAP_SHARED, fileno(file), 0);
        if (mapping == MAP_FAILED) {
            logger_error(&_G.logger, "cannot map file '%*pM': %m",
                         LSTR_FMT_ARG(path));
            goto error;
        }

        if (file_bin_parse_header(path, mapping, st.st_size,
                                  &version, &slot_size) < 0)
        {
            goto error;
        }
    }

    res = file_bin_new();
    res->read_mode = true;
    res->f = file;
    res->path = r_path;
    res->length = st.st_size;
    res->map = mapping;
    res->version = version;
    res->slot_size = slot_size;
    res->cur = HEADER_SIZE(res);

    return res;

  error:
    p_fclose(&file);
    lstr_wipe(&r_path);
    return NULL;
}

/* }}} */
/* {{{ Writing */

int file_bin_flush(file_bin_t *file)
{
    if (fflush(file->f) < 0) {
        return logger_error(&_G.logger, "cannot flush file '%*pM': %m",
                            LSTR_FMT_ARG(file->path));
    }

    return 0;
}

int file_bin_sync(file_bin_t *file)
{
    RETHROW(file_bin_flush(file));

    if (fsync(fileno(file->f)) < 0) {
        return logger_error(&_G.logger, "cannot sync file '%*pM': %m",
                            LSTR_FMT_ARG(file->path));
    }

    return 0;
}

int file_bin_truncate(file_bin_t *file, off_t pos)
{
    RETHROW(file_bin_flush(file));

    if (xftruncate(fileno(file->f), pos) < 0) {
        return logger_error(&_G.logger, "cannot truncate file '%*pM' at pos "
                            "%jd: %m", LSTR_FMT_ARG(file->path),
                            (int64_t)pos);
    }

    file->cur = MIN(file->cur, pos);

    RETHROW(file_bin_seek(file, file->cur, SEEK_SET));

    return 0;
}

static int file_bin_pad(file_bin_t *file, off_t new_pos)
{
    off_t real_cur = ftell(file->f);

    if (real_cur < 0) {
        return logger_error(&_G.logger, "cannot use ftell on file '%*pM': %m",
                            LSTR_FMT_ARG(file->path));
    }

    THROW_ERR_UNLESS(expect(real_cur <= new_pos));

    if (real_cur == new_pos) {
        return 0;
    }

    RETHROW(file_bin_truncate(file, new_pos));

    real_cur = RETHROW(file_bin_tell(file));

    THROW_ERR_UNLESS(expect(real_cur == new_pos));

    return 0;
}

/* Write opaque data in file starting at offset file->cur */
static int _write_file_bin(file_bin_t *file, const void *data, uint32_t len)
{
    uint32_t res;

    RETHROW(file_bin_pad(file, file->cur));

    res = fwrite(data, 1, len, file->f);

    if (res < len) {
        IGNORE(file_bin_truncate(file, file->cur));
        return logger_error(&_G.logger, "cannot write in file '%*pM': %m",
                            LSTR_FMT_ARG(file->path));
    }

    file->cur += res;

    return 0;
}

static int file_bin_write_header(file_bin_t *file)
{
    lstr_t version_str = LSTR_DATA(SIG, 16);
    le32_t slot_size = cpu_to_le32(file->slot_size);

    RETHROW(_write_file_bin(file, version_str.data, version_str.len));
    RETHROW(_write_file_bin(file, &slot_size, sizeof(le32_t)));

    return 0;
}

static int file_bin_write_slot_header(file_bin_t *file, off_t next_entry)
{
    off_t to_write = next_entry - (file->cur + SLOT_HDR_SIZE(file));
    slot_hdr_t towrite_le32 = cpu_to_le32((uint32_t)to_write);

    assert (next_entry >= (long)(file->cur + SLOT_HDR_SIZE(file)));
    assert (to_write <= UINT32_MAX);

    return _write_file_bin(file, &towrite_le32, SLOT_HDR_SIZE(file));
}

/* Write data in file and write slot headers when necessary */
static int file_bin_write_data(file_bin_t *f, const void *data, uint32_t len,
                               off_t next_entry, bool r_start)
{
    while (len > 0) {
        uint32_t w_size;

        if (is_at_slot_start(f)) {
            if (r_start) {
                file_bin_write_slot_header(f, f->cur + SLOT_HDR_SIZE(f));
            } else {
                file_bin_write_slot_header(f, next_entry);
            }
        }

        w_size = MIN(file_bin_remaining_space_in_slot(f), len);

        len -= w_size;
        RETHROW(_write_file_bin(f, data, w_size));

        data = (byte *)data + w_size;
        r_start = false;
    }

    return 0;
}

int file_bin_put_record(file_bin_t *file, const void *data, uint32_t len)
{
    rc_hdr_t rec_len_le32 = cpu_to_le32(len);
    uint32_t total_size = len + RC_HDR_SIZE;
    off_t next_entry;
    uint32_t remaining;

    if (file->cur == 0 && file->version > 0) {
        /* File beginning */
        RETHROW(file_bin_write_header(file));
    }

    remaining = file_bin_remaining_space_in_slot(file);

    if (remaining < RC_HDR_SIZE
    ||  (file->version == 0 && remaining < total_size))
    {
        file->cur += remaining;
    }
    next_entry = file_bin_get_next_entry_off(file, total_size);

    RETHROW(file_bin_write_data(file, &rec_len_le32, RC_HDR_SIZE,
                                next_entry, true));
    RETHROW(file_bin_write_data(file, data, len, next_entry, false));

    return 0;
}

file_bin_t *file_bin_create(lstr_t path, uint32_t slot_size, bool trunc)
{
    file_bin_t *res;
    FILE *file;
    lstr_t r_path = lstr_dup(path);
    uint32_t min_slot_size;

    file = fopen(r_path.s, trunc ? "w" : "a+");
    if (!file) {
        logger_error(&_G.logger, "cannot open file '%*pM': %m",
                     LSTR_FMT_ARG(path));
        lstr_wipe(&r_path);
        return NULL;
    }

#define GOTO_ERROR_IF_FAIL(expr)  if (unlikely((expr) < 0)) { goto error; }

    slot_size = slot_size > 0 ? slot_size : FILE_BIN_DEFAULT_SLOT_SIZE;

    res = file_bin_new();
    res->f = file;
    res->path = r_path;

    GOTO_ERROR_IF_FAIL(file_bin_seek(res, 0, SEEK_END));
    GOTO_ERROR_IF_FAIL((res->cur = file_bin_tell(res)));

    res->version = CURRENT_VERSION;
    res->slot_size = slot_size;

    if (res->cur > 0) {
        /* Appending an already existing file */
        byte buf[20];

        if (res->cur < countof(buf)) {
            res->slot_size = FILE_BIN_DEFAULT_SLOT_SIZE;
            res->version = 0;
            return res;
        }

        rewind(file);
        if (fread(buf, 1, countof(buf), file) < countof(buf)) {
            logger_error(&_G.logger, "cannot read binary file header "
                         "for file '%*pM': %m", LSTR_FMT_ARG(r_path));
            goto error;
        }

        GOTO_ERROR_IF_FAIL(file_bin_seek(res, 0, SEEK_END));
        GOTO_ERROR_IF_FAIL(file_bin_parse_header(r_path, buf, countof(buf),
                                                 &res->version,
                                                 &res->slot_size));
        return res;
    }

    min_slot_size = HEADER_SIZE(res) + SLOT_HDR_SIZE(res) + RC_HDR_SIZE;

    if (unlikely(slot_size < min_slot_size)) {
        logger_error(&_G.logger, "slot size should be higher than %u, got "
                     " %u for file '%*pM'", min_slot_size, slot_size,
                     LSTR_FMT_ARG(r_path));
        goto error;
    }

    return res;

#undef GOTO_ERROR_IF_FAIL

  error:
    IGNORE(file_bin_close(&res));
    return NULL;
}

/* }}} */

int file_bin_close(file_bin_t **file_ptr)
{
    int res = 0;
    file_bin_t *file = *file_ptr;

    if (!file) {
        return 0;
    }

    if (file->map) {
        if (munmap(file->map, file->length) < 0) {
            res = logger_error(&_G.logger, "cannot unmap file '%*pM': %m",
                               LSTR_FMT_ARG(file->path));
        }
    }

    if (p_fclose(&file->f) < 0) {
        res = logger_error(&_G.logger, "cannot close file '%*pM': %m",
                           LSTR_FMT_ARG(file->path));
    }

    file_bin_delete(file_ptr);

    return res;
}
