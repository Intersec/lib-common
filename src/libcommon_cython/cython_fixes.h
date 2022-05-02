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

#ifndef IS_LIBCOMMON_CYTHON_FIXES_H
#define IS_LIBCOMMON_CYTHON_FIXES_H

#include <Python.h>

/* These macros are redefined by Cython */
#ifdef likely
#  undef likely
#endif /* likely */
#ifdef unlikely
#  undef unlikely
#endif /* unlikely */
#ifdef __unused__
#  undef __unused__
#endif /* __unused__ */

/* Disable clang comma warnings for Clang >= 3.9 */
#if defined(__clang__)                                                       \
 && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 9))
#  pragma GCC diagnostic ignored "-Wcomma"
#endif /* Clang >= 3.9 */

/* Redefine PyMODINIT_FUNC to properly export init function on Python < 3.9 */
#if PY_VERSION_HEX < 0x03090000

# ifndef EXPORT
#   ifdef __GNUC__
#     define EXPORT extern __attribute__((visibility("default")))
#   else /* __GNUC__ */
#     define EXPORT extern
#   endif /* __GNUC__ */
# endif /* EXPORT */

#  ifndef PyMODINIT_FUNC
#    error "PyMODINIT_FUNC should be defined"
#  endif /* PyMODINIT_FUNC */

#  if PY_MAJOR_VERSION < 3
#    error "invalid python version, python >= 3 is required"
#  endif /*  PY_MAJOR_VERSION < 3 */

#  undef PyMODINIT_FUNC
#  define PyMODINIT_FUNC EXPORT PyObject *

#endif /* PY_VERSION_HEX < 0x03090000 */

#endif /* IS_LIBCOMMON_CYTHON_FIXES_H */
