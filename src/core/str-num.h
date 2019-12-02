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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_NUM_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_NUM_H

/* Wrappers to fix constness issue in strtol() */
__attr_nonnull__((1))
static inline unsigned long
cstrtoul(const char * nonnull str, const char * nullable * nullable endp,
         int base)
{
    return (strtoul)(str, (char **)endp, base);
}

__attr_nonnull__((1))
static inline unsigned long
vstrtoul(char * nonnull str, char * nullable * nullable endp, int base)
{
    return (strtoul)(str, endp, base);
}
#define strtoul(str, endp, base)  cstrtoul(str, endp, base)

__attr_nonnull__((1))
static inline long
cstrtol(const char * nonnull str, const char * nullable * nullable endp,
        int base)
{
    return (strtol)(str, (char **)endp, base);
}

__attr_nonnull__((1))
static inline long vstrtol(char * nonnull str,
                           char * nullable * nullable endp,
                           int base)
{
    return (strtol)(str, endp, base);
}
#define strtol(str, endp, base)  cstrtol(str, endp, base)

__attr_nonnull__((1))
static inline long long
cstrtoll(const char * nonnull str, const char * nullable * nullable endp,
         int base)
{
    return (strtoll)(str, (char **)endp, base);
}
__attr_nonnull__((1))
static inline long long
vstrtoll(char * nonnull str, char * nullable * nullable endp, int base)
{
    return (strtoll)(str, endp, base);
}
#define strtoll(str, endp, base)  cstrtoll(str, endp, base)

__attr_nonnull__((1))
static inline unsigned long long
cstrtoull(const char * nonnull str, const char * nullable * nullable endp,
          int base)
{
    return (strtoull)(str, (char **)endp, base);
}
__attr_nonnull__((1))
static inline unsigned long long
vstrtoull(char * nonnull str, char * nullable * nullable endp, int base)
{
    return (strtoull)(str, endp, base);
}
#define strtoull(str, endp, base)  cstrtoull(str, endp, base)

int strtoip(const char * nonnull p, const char * nullable * nullable endp)
    __leaf __attr_nonnull__((1));
static inline int vstrtoip(char * nonnull p, char * nullable * nullable endp)
{
    return strtoip(p, (const char **)endp);
}
int memtoip(const void * nonnull p, int len,
            const byte * nullable * nullable endp)
    __leaf __attr_nonnull__((1));
int64_t memtollp(const void * nonnull s, int len,
                 const byte * nullable * nullable endp)
    __leaf __attr_nonnull__((1));
int64_t parse_number(const char * nonnull str) __leaf;

uint64_t memtoullp(const void * nonnull s, int len,
                   const byte * nullable * nullable endp)
    __leaf __attr_nonnull__((1));

#define STRTOLP_IGNORE_SPACES  (1 << 0)
#define STRTOLP_CHECK_END      (1 << 1)
#define STRTOLP_EMPTY_OK       (1 << 2)
#define STRTOLP_CHECK_RANGE    (1 << 3)
#define STRTOLP_CLAMP_RANGE    (1 << 4)
/* returns 0 if success, negative errno if error */
int strtolp(const char * nonnull p, const char * nullable * nullable endp,
            int base, long * nullable res, int flags, long min, long max)
    __leaf;

/* The four following functions read an integer followed by an
 * extension from a string
 * @param p    start address of the first character to read
 * @param len  maximum number of characters to read
 * @param out  pointer to the value read by the function
 * @param endp if not null, pointer to address of the first character
 *             after the last character read by the function
 * @param base specify the base of the representation of the number
 *             0 is for same syntax as integer constants in C
 *             otherwise between 2 and 36
 *
 * @param s    start address of the null terminated string to be read
 * @param tail if not null, pointer to address of the first character
 *             after the last character read by the function
 */
int
memtoll_ext(const void * nonnull p, int len, int64_t * nonnull out,
            const void * nullable * nullable endp, int base);

int
memtoull_ext(const void * nonnull p, int len, uint64_t * nonnull out,
             const void * nullable * nullable endp, int base);

int strtoll_ext(const char * nonnull s, int64_t * nonnull out,
                const char * nullable * nullable tail, int base);

int strtoull_ext(const char * nonnull s, uint64_t * nonnull out,
                 const char * nullable * nullable tail, int base);

__attr_nonnull__((1))
static inline double memtod(const void * nonnull s, int len,
                            const byte * nullable * nullable endptr)
{
    if (!len) {
        errno = EINVAL;
        if (endptr) {
            *endptr = (byte *)s;
        }
        return 0;
    }
    if (len > 0) {
        t_scope;

        /* Ensure we have a '\0' */
        const char *duped = (const char *)t_dupz(s, len);
        double res = strtod(duped, (char **)endptr);

        if (endptr) {
            *(char **)endptr = (char *)s + (*(char **)endptr - duped);
        }

        return res;
    }

    return strtod((const char *)s, (char **)endptr);
}

#endif
