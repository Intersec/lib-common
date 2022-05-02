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

#ifndef IS_LIB_COMMON_PARSING_HELPERS_H
#define IS_LIB_COMMON_PARSING_HELPERS_H

#include "core.h"

/** Parse a backslash in a quoted string to handle escape sequences.
 *
 *  * \\, \a, \t, \n, etc
 *  * \0, \0DD, \1DD, \2DD for octal
 *  * \xXX for hexa
 *  * \uXXYY for unicode
 */
int
parse_backslash(pstream_t * nonnull ps, sb_t * nonnull buf,
                int * nonnull line, int * nonnull col);

#endif /* IS_LIB_COMMON_PARSING_HELPERS_H */
