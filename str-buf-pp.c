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

#include "str-buf-pp.h"

static void sb_add_cell(sb_t *out, const struct table_hdr_t *col,
                        int col_size, bool is_hdr, bool is_last,
                        lstr_t content)
{
    int len = lstr_utf8_strlen(content);

    if (len <= 0) {
        content = col->empty_value;
        len = lstr_utf8_strlen(content);
    }

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

void sb_add_table(sb_t *out, const qv_t(table_hdr) *hdr,
                  const qv_t(table_data) *data)
{
    int *col_sizes = p_alloca(int, hdr->len);
    int row_size = 0;
    int col_count = 0;
    bool first_column = true;

    /* Compute the size of the columns */
    tab_for_each_pos(pos, hdr) {
        int *col = &col_sizes[pos];
        bool has_value = false;

        *col = MAX(hdr->tab[pos].min_width,
                   lstr_utf8_strlen(hdr->tab[pos].title));

        tab_for_each_ptr(row, data) {
            if (row->len <= pos) {
                *col = MAX(*col, lstr_utf8_strlen(hdr->tab[pos].empty_value));
            } else {
                *col = MAX(*col, lstr_utf8_strlen(row->tab[pos]));

                if (row->tab[pos].len) {
                    has_value = true;
                }
            }
        }

        if (hdr->tab[pos].max_width) {
            *col = MIN(*col, hdr->tab[pos].max_width);
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

    /* Write the header */
    tab_for_each_pos(pos, hdr) {
        if (col_sizes[pos] == 0) {
            continue;
        }
        if (!first_column) {
            sb_adds(out, "  ");
        }
        sb_add_cell(out, &hdr->tab[pos], col_sizes[pos], true,
                        pos == hdr->len - 1, hdr->tab[pos].title);
        first_column = false;
    }
    sb_addc(out, '\n');

    /* Write the content */
    tab_for_each_ptr(row, data) {
        first_column = true;

        tab_for_each_pos(pos, hdr) {
            lstr_t content = LSTR_NULL;

            if (col_sizes[pos] == 0) {
                continue;
            }
            if (pos < row->len) {
                content = row->tab[pos];
            }

            if (!first_column) {
                sb_adds(out, "  ");
            }
            sb_add_cell(out, &hdr->tab[pos], col_sizes[pos], false,
                            pos == hdr->len - 1, content);
            first_column = false;
        }
        sb_addc(out, '\n');
    }
}
