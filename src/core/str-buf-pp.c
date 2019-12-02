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

#include <lib-common/str-buf-pp.h>

static void sb_add_cell(sb_t *out, const struct table_hdr_t *col,
                        int col_size, bool is_hdr, bool is_last,
                        lstr_t content)
{
    int len = lstr_utf8_strlen(content);

    if (len > col_size && col->add_ellipsis) {
        content = lstr_utf8_truncate(content, col_size - 1);
        sb_add(out, content.s, content.len);
        sb_adduc(out, 0x2026 /* … */);
    } else
    if (len >= col_size) {
        content = lstr_utf8_truncate(content, col_size);
        sb_add(out, content.s, content.len);
    } else {
        int padding = col_size - len;
        int left_padding = 0;
        int right_padding = 0;

        switch (is_hdr ? ALIGN_LEFT : col->align) {
          case ALIGN_LEFT:
            break;

          case ALIGN_CENTER:
            left_padding = padding / 2;
            break;

          case ALIGN_RIGHT:
            left_padding = padding;
            break;
        }
        right_padding = padding - left_padding;
        if (left_padding) {
            sb_addnc(out, left_padding, ' ');
        }
        sb_add_lstr(out, content);
        if (right_padding && !is_last) {
            sb_addnc(out, right_padding, ' ');
        }
    }
}

/* Return 0 if something has been written. */
static int
sb_write_table_cell(sb_t *out, const struct table_hdr_t *col, int col_size,
                    bool is_hdr, bool is_first, bool is_last, lstr_t content,
                    int csv_sep)
{
    if (col_size == 0) {
        /* Omit column. */
        return -1;
    }

    if (!content.len) {
        content = col->empty_value;
    }

    if (!is_first) {
        if (csv_sep) {
            sb_addc(out, csv_sep);
        } else {
            sb_adds(out, "  ");
        }
    }

    if (csv_sep) {
        sb_add_lstr_csvescape(out, csv_sep, content);
    } else {
        sb_add_cell(out, col, col_size, is_hdr, is_last, content);
    }

    return 0;
}

/** Write table or a csv if csv_sep != 0. */
static void
sb_write_table(sb_t *out, const qv_t(table_hdr) *hdr,
               const qv_t(table_data) *data, int *col_sizes, int csv_sep)
{
    bool first_column = true;

    /* Write the header. */
    tab_enumerate_ptr(pos, col_hdr, hdr) {
        if (sb_write_table_cell(out, col_hdr, col_sizes[pos], true,
                                first_column, pos == hdr->len - 1,
                                col_hdr->title, csv_sep) == 0)
        {
            first_column = false;
        }
    }
    sb_addc(out, '\n');

    /* Write the content. */
    tab_for_each_ptr(row, data) {
        first_column = true;

        tab_enumerate_ptr(pos, col_hdr, hdr) {
            lstr_t content = LSTR_NULL;

            if (pos < row->len) {
                content = row->tab[pos];
            }

            if (sb_write_table_cell(out, col_hdr, col_sizes[pos],
                                    false, first_column, pos == hdr->len - 1,
                                    content, csv_sep) == 0)
            {
                first_column = false;
            }
        }
        sb_addc(out, '\n');
    }
}

void sb_add_table(sb_t *out, const qv_t(table_hdr) *hdr,
                  const qv_t(table_data) *data)
{
    int *col_sizes = p_alloca(int, hdr->len);
    int row_size = 0;
    int col_count = 0;

    /* Compute the size of the columns */
    tab_enumerate_ptr(pos, col_hdr, hdr) {
        int *col = &col_sizes[pos];
        bool has_value = false;

        *col = MAX(col_hdr->min_width, lstr_utf8_strlen(col_hdr->title));

        tab_for_each_ptr(row, data) {
            if (row->len <= pos) {
                *col = MAX(*col, lstr_utf8_strlen(col_hdr->empty_value));
            } else {
                *col = MAX(*col, lstr_utf8_strlen(row->tab[pos]));

                if (row->tab[pos].len) {
                    has_value = true;
                }
            }
        }

        if (hdr->tab[pos].max_width) {
            *col = MIN(*col, col_hdr->max_width);
        }
        if (hdr->tab[pos].omit_if_empty && !has_value) {
            *col = 0;
        } else {
            col_count++;
        }
        row_size += *col;
    }

    row_size += 2 * (col_count - 1) + 1;
    sb_grow(out, row_size * (data->len + 1));

    sb_write_table(out, hdr, data, col_sizes, 0);
}

void sb_add_csv_table(sb_t *out, const qv_t(table_hdr) *hdr,
                      const qv_t(table_data) *data, int sep)
{
    int *populated_cols = p_alloca(int, hdr->len);

    /* Check if we have empty columns. */
    tab_enumerate_ptr(pos, col_hdr, hdr) {
        int *populated_col = &populated_cols[pos];

        *populated_col = false;

        if (!col_hdr->omit_if_empty || col_hdr->empty_value.len) {
            /* If table_hdr__t has a default empty value, the column will not
             * be omitted.
             */
            *populated_col = true;
            continue;
        }

        tab_for_each_ptr(row, data) {
            if (row->len <= pos) {
                /* Not enough data columns. */
                break;
            }

            if (row->tab[pos].len) {
                /* Column will not be omitted. */
                *populated_col = true;
                break;
            }
        }
    }

    sb_write_table(out, hdr, data, populated_cols, sep);
}
