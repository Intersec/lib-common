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

#ifndef IS_LIB_COMMON_CORE_H
#define IS_LIB_COMMON_CORE_H

#define LIB_COMMON_VERSION  "2021.04"

#if HAS_LIBCOMMON_REPOSITORY
# define LIBCOMMON_PATH "lib-common/"
# if HAS_PLATFORM_REPOSITORY
#   define PLATFORM_PATH "platform/"
# else
#   define PLATFORM_PATH ""
# endif
#else
# define LIBCOMMON_PATH ""
# define PLATFORM_PATH ""
#endif

#include <Block.h>
#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/socket.h>
#undef ECHO
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define __ISLIBC__

#define IPRINTF_HIDE_STDIO 1
#include "core/os-features.h"
#include "core/macros.h"

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#ifdef __cplusplus
}
#endif
#include "core/macros++.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "core/bithacks.h"
#include "core/blocks.h"
#include "core/types.h"
#include "core/errors.h"
#include "core/mem.h"
#include "core/mem-stack.h"
#include "core/stdlib.h"
#include "core/obj.h"

#include "core/str-ctype.h"
#include "core/str-iprintf.h"
#include "core/str-num.h"
#include "core/str-conv.h"
#include "core/str-l.h"
#include "core/str-buf.h"
#include "core/str-stream.h"
#include "core/str.h"

#include "core/module.h"

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
