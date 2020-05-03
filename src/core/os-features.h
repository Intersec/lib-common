/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_OS_FEATURES_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_OS_FEATURES_H

/*---------------- Guess the OS ----------------*/
#if defined(__linux__)
#  define OS_LINUX
#else
#  error "we don't know about your OS"
#endif

/* <sys/poll.h> availability */
#ifndef HAVE_SYS_POLL_H
# define HAVE_SYS_POLL_H
#endif

/* <sys/inotify.h> availability */
#ifdef OS_LINUX
# ifndef HAVE_SYS_INOTIFY_H
#  define HAVE_SYS_INOTIFY_H
# endif
#endif

#ifndef SO_FILEEXT
# define SO_FILEEXT  ".so"
#endif

#endif
