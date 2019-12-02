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

#ifndef IS_LIB_COMMON_PARSING_HELPERS_H
#define IS_LIB_COMMON_PARSING_HELPERS_H

#include "core.h"

typedef enum parse_str_res_t {
    /** String was never closed. */
    PARSE_STR_ERR_UNCLOSED = -2,
    /** An escape sequence was started but ill-formed. */
    PARSE_STR_ERR_EXP_SMTH = -1,
    /** String properly parsed. */
    PARSE_STR_OK = 0,
} parse_str_res_t;

/** Parse a quoted string and handle escape sequences.
 *
 * The stream is parsed until the terminating character is met.
 * Escape sequences are handled:
 *  * \\, \a, \t, \n, etc
 *  * \0, \0DD, \1DD, \2DD for octal
 *  * \xXX for hexa
 *  * \uXXYY for unicode
 *
 * \warning the string must not contain newline characters, it must be on a
 * single line (or newline characters must be written as '\n').
 */
parse_str_res_t
parse_quoted_string(pstream_t * nonnull ps, sb_t * nonnull buf,
                    int * nonnull line, int * nonnull col, int term);

#endif /* IS_LIB_COMMON_PARSING_HELPERS_H */
