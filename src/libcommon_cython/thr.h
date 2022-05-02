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

#ifndef IS_CYTHON_LIBCOMMON_THR_H
#define IS_CYTHON_LIBCOMMON_THR_H

#include <Python.h>

/* Fix deprecated warning of PyEval_InitThreads for Python >= 3.9. */
#if PY_VERSION_HEX < 0x03090000
#  define py_eval_init_threads() PyEval_InitThreads()
#else
#  define py_eval_init_threads() do { } while(0)
#endif

#endif /* IS_CYTHON_LIBCOMMON_THR_H */
