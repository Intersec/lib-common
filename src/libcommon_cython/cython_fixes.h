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

#ifndef IS_CYTHON_FIXES_H
#define IS_CYTHON_FIXES_H

/* These macros are redefined by Cython */
#ifdef likely
#  undef likely
#endif
#ifdef unlikely
#  undef unlikely
#endif
#ifdef __unused__
#  undef __unused__
#endif

/* Disable clang comma warnings for Python >= 3.9 */
#if defined(__clang__)                                                       \
 && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 9))
#  pragma GCC diagnostic ignored "-Wcomma"
#endif

#endif /* IS_CYTHON_FIXES_H */
