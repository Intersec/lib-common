/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#ifndef IS_PXCC_HEADER_H
#define IS_PXCC_HEADER_H

#define __PXCC_EXPORT_FILE(x, l)                                             \
    static const char *pxcc_exported_file_ ## l = (x)
#define _PXCC_EXPORT_FILE(x, l)   __PXCC_EXPORT_FILE(x, l)
#define PXCC_EXPORT_FILE(x)      _PXCC_EXPORT_FILE(x, __LINE__)

#define __PXCC_EXPORT_TYPE(x, l)  static x *pxcc_exported_type_ ## l
#define _PXCC_EXPORT_TYPE(x, l)   __PXCC_EXPORT_TYPE(x, l)
#define PXCC_EXPORT_TYPE(x)      _PXCC_EXPORT_TYPE(x, __LINE__)

#define __PXCC_EXPORT_SYMBOL(x, l)                                           \
    static void *pxcc_exported_symbol_ ## l = (void *)&(x)
#define _PXCC_EXPORT_SYMBOL(x, l)  __PXCC_EXPORT_SYMBOL(x, l)
#define PXCC_EXPORT_SYMBOL(x)      _PXCC_EXPORT_SYMBOL(x, __LINE__)

/* Remove _Nonull, _Null_unspecified and _Nullable because of some issues with
 * clang 3.4 */
#define _Nonnull
#define _Null_unspecified
#define _Nullable

#endif /* IS_PXCC_HEADER_H */
