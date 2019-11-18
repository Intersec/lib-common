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

#ifndef IS_LIB_COMMON_CORE_H
#define IS_LIB_COMMON_CORE_H

#define LIB_COMMON_VERSION  "master"
#define LIB_COMMON_MAJOR    9999
#define LIB_COMMON_MINOR    1
#define __LIB_COMMON_VER(Maj, Min)  (((Maj) << 16) | Min)

#define LIB_COMMON_PREREQ(Maj, Min)                                          \
    (__LIB_COMMON_VER(Maj, Min) <= __LIB_COMMON_VER(LIB_COMMON_MAJOR,        \
                                                    LIB_COMMON_MINOR))

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
#include "src/core/os-features.h"
#include "src/core/macros.h"

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
#include "src/core/macros++.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "src/core/bithacks.h"
#include "src/core/blocks.h"
#include "src/core/errors.h"
#include "src/core/mem.h"
#include "src/core/mem-stack.h"
#include "src/core/types.h"
#include "src/core/stdlib.h"
#include "src/core/obj.h"

#include "src/core/str-ctype.h"
#include "src/core/str-iprintf.h"
#include "src/core/str-num.h"
#include "src/core/str-conv.h"
#include "src/core/str-l.h"
#include "src/core/str-buf.h"
#include "src/core/str-stream.h"
#include "src/core/str.h"

#include "src/core/module.h"

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
