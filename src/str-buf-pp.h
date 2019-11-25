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

#ifndef IS_LIB_COMMON_STR_BUF_PP_H
#define IS_LIB_COMMON_STR_BUF_PP_H

#include <lib-common/core.h>
#include <lib-common/container-qvector.h>

/** \file str-buf-pp.h
 * Helpers to perform some complex pretty-printing.
 */

/** Description of a column table.
 */
typedef struct table_hdr_t {
    /** Title of the column.
     */
    lstr_t title;

    /** Value to put if a cell is empty or missing.
     */
    lstr_t empty_value;

    /** Maximum width of the column.
     */
    int max_width;

    /** Minimum width of the column.
     */
    int min_width;

    /** Alignment of the column.
     */
    enum {
        ALIGN_LEFT,
        ALIGN_CENTER,
        ALIGN_RIGHT,
    } align;

    /** If true, add ellipsis (…) when the content does not fit in the maximum
     * width.
     */
    bool add_ellipsis;

    /** Omit the column if no value is found.
     */
    bool omit_if_empty;
} table_hdr_t;

qvector_t(table_hdr, table_hdr_t);
qvector_t(table_data, qv_t(lstr));

/** Format a table.
 *
 * This function appends a table formatted from the given columns, whose
 * descriptions are provided in \p hdr, and rows whose content is provided in
 * \p data in the buffer \p out. The content is guaranteed to end with a
 * newline character.
 *
 * The output contains a first row with the column title, followed by one line
 * per entry of \p data. The width of the columns is adjusted to their content
 * as well as the dimensioning parameters provided in the column description.
 * Columns are separated by two spaces. A row may contain less columns than
 * the header, in which case the missing cells are filled with the default
 * values for those columns.
 *
 * The header of the columns is always left-aligned. The last column may
 * contain extra data that does not fit on a single line.
 *
 * \param[in] out  The output buffer.
 * \param[in] hdr  The description of the columns.
 * \param[in] data The content of the table.
 */
void sb_add_table(sb_t *out, const qv_t(table_hdr) *hdr,
                  const qv_t(table_data) *data);

/** Format a table in CSV.
 *
 * This function appends a CSV table formatted from the given columns, whose
 * descriptions are provided in \p hdr, and rows whose content is provided in
 * \p data in the buffer \p out. The content is guaranteed to end with a
 * newline character. This function is meant to offer the possibility to print
 * content as CSV or as a table. If you only want to print CSV and not tables,
 * a more straightforward and better option is to used directly CSV helpers
 * for string buffers 'sb_t'.
 *
 * The output contains a first row with the column names, followed by one line
 * per entry of \p data. The columns are separated by \ref sep.
 *
 * Only `omit_if_empty` and `empty_value` column description variables are
 * used to format the CSV output.
 *
 * \param[in,out] out  The output buffer.
 * \param[in] sep      CSV separator.
 * \param[in] hdr      The description of the columns.
 * \param[in] data     The content of the table.
 */
void sb_add_csv_table(sb_t *out, const qv_t(table_hdr) *hdr,
                      const qv_t(table_data) *data, int sep);

#endif /* IS_LIB_COMMON_STR_BUF_PP_H */
