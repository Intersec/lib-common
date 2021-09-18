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

#ifndef IS_CYTHON_EXPORT_FIX_H
#define IS_CYTHON_EXPORT_FIX_H

#include <Python.h>

#ifndef EXPORT
#  ifdef __GNUC__
#    define EXPORT  extern __attribute__((visibility("default")))
#  else
#    define EXPORT  extern
#  endif
#endif

#if PY_MAJOR_VERSION < 3
#  error "invalid python version, python >= 3 is required"
#endif

#ifndef PyMODINIT_FUNC
#  error "PyMODINIT_FUNC should be defined"
#endif

#undef PyMODINIT_FUNC
#define PyMODINIT_FUNC  EXPORT PyObject *

#endif /* IS_CYTHON_EXPORT_FIX_H */
