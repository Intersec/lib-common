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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_STR__H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_STR_H

/* Simple helpers {{{ */

const char * nonnull skipspaces(const char * nonnull s)
    __attr_nonnull__((1));

__attr_nonnull__((1))
static inline char * nonnull vskipspaces(char * nonnull s)
{
    return (char*)skipspaces(s);
}

const char * nonnull strnextspace(const char * nonnull s)
    __attr_nonnull__((1));
__attr_nonnull__((1))
static inline char * nonnull vstrnextspace(char * nonnull s)
{
    return (char*)strnextspace(s);
}

const char * nonnull skipblanks(const char * nonnull s)
    __attr_nonnull__((1));
__attr_nonnull__((1))
static inline char * nonnull vskipblanks(char * nonnull s)
{
    return (char*)skipblanks(s);
}

/* Trim spaces at end of string, return pointer to '\0' */
char * nullable strrtrim(char * nullable str);

int strstart(const char * nullable str, const char * nonnull p,
             const char * nullable * nullable pp);
static inline int vstrstart(char * nullable str, const char * nonnull p,
                            char * nullable * nullable pp)
{
    return strstart(str, p, (const char **)pp);
}

int stristart(const char * nullable str, const char * nonnull p,
              const char * nullable * nullable pp);
static inline int vstristart(char * nullable str, const char * nonnull p,
                             char * nullable * nullable pp)
{
    return stristart(str, p, (const char **)pp);
}

const char * nullable stristrn(const char * nonnull haystack,
                               const char * nonnull needle, size_t nlen)
          __attr_nonnull__((1, 2));

__attr_nonnull__((1, 2))
static inline char * nullable
vstristrn(char * nonnull haystack, const char * nonnull needle, size_t nlen)
{
    return (char *)stristrn(haystack, needle, nlen);
}

__attr_nonnull__((1, 2))
static inline const char * nullable
stristr(const char * nonnull haystack, const char * nonnull needle)
{
    return stristrn(haystack, needle, strlen(needle));
}

__attr_nonnull__((1, 2))
static inline char * nullable
vstristr(char * nonnull haystack, const char * nonnull needle)
{
    return (char *)stristr(haystack, needle);
}

/* Implement inline using strcmp, unless strcmp is hopelessly fucked up */
__attr_nonnull__((1, 2))
static inline bool strequal(const char * nonnull str1,
                            const char * nonnull str2)
{
    return !strcmp(str1, str2);
}

/* find a word in a list of words separated by sep.
 */
bool strfind(const char * nonnull keytable, const char * nonnull str, int sep);

int buffer_increment(char * nullable buf, int len);
int buffer_increment_hex(char * nullable buf, int len);
ssize_t pstrrand(char * nonnull dest, ssize_t size, int offset, ssize_t len);
size_t strrand(char dest[], size_t dest_size, lstr_t alphabet);

/* Return the number of occurences replaced */
/* OG: need more general API */
int str_replace(const char search, const char replace,
                char * nonnull subject);

/* }}} */
/* Path helpers {{{ */

/*----- simple file name splits -----*/

ssize_t path_dirpart(char * nonnull dir, ssize_t size,
                     const char * nonnull filename)
    __leaf;

__attr_nonnull__()
    const char * nonnull path_filepart(const char * nonnull filename);
__attr_nonnull__()
static inline char * nonnull vpath_filepart(char * nonnull path)
{
    return (char*)path_filepart(path);
}

__attr_nonnull__()
const char * nonnull path_extnul(const char * nonnull filename);
__attr_nonnull__()
static inline char * nonnull vpath_extnul(char * nonnull path)
{
    return (char*)path_extnul(path);
}
const char * nonnull path_ext(const char * nonnull filename);
static inline char * nonnull vpath_ext(char * nonnull path)
{
    return (char*)path_ext(path);
}

/*----- libgen like functions -----*/

int path_dirname(char * nonnull buf, int len, const char * nonnull path)
    __leaf;
int path_basename(char * nonnull buf, int len, const char * nonnull path)
    __leaf;

/*----- path manipulations -----*/

int path_join(char * nonnull buf, int len, const char * nonnull path)
    __leaf;
int path_simplify2(char * nonnull path, bool keep_trailing_slash) __leaf;
#define path_simplify(path)   path_simplify2(path, false)
int path_canonify(char * nonnull buf, int len, const char * nonnull path)
    __leaf;
char * nullable path_expand(char * nonnull buf, int len,
                            const char * nonnull path)
    __leaf;

bool path_is_safe(const char * nonnull path) __leaf;

#ifndef __cplusplus

/** Extend a relative path from a prefix.
 *
 *      prefix + fmt = prefix + / + fmt
 *
 * A '/' will be added between /p prefix and /p fmt if necessary.
 *
 * If /p fmt begins with a '/', the prefix will be ignored.
 * If /p fmt begins with '~/', the prefix will be ignored, and ~ replaced by
 * the HOME prefix of the current user.
 *
 * \param[out] buf    buffer where the path will be written.
 * \param[in]  prefix
 * \param[in]  fmt    format of the suffix
 * \param[in]  args   va_list containing the arguments for the \p fmt
 *                    formatting
 *
 * \return -1 if the path overflows the buffer,
 *            length of the resulting path otherwise.
 */
__attribute__((format(printf, 3, 0)))
int path_va_extend(char buf[static PATH_MAX], const char * nonnull prefix,
                   const char * nonnull fmt, va_list args);
__attribute__((format(printf, 3, 4)))
int path_extend(char buf[static PATH_MAX], const char * nonnull prefix,
                const char * nonnull fmt, ...);

/** Create a relative path from one path to another path.
 *
 * \p from and \p to can be absolute or relative paths from the current
 * working directory.
 * If \p from ends with a '/', it is treated as a directory and the created
 * relative path startpoint is within the directory.
 *
 * Examples:
 *  - The relative path from "/a/b/c/d" to "/a/b/e/f" is "../e/f".
 *  - The relative path from "/a/b/c/d/" to "/a/b/e/f" is "../../e/f".
 *  - The relative path from "a/b/c" to "d/e/" is "../../d/e".
 *  - The relative path from "a/b/c/" to "a/b/c" is "c".
 *  - The relative path from "/a/b/c" to "d/e" is "../d/e" with the current
 *    working directory set to "/a".
 *
 * \param[out] buf  buffer where the path will be written.
 * \param[in]  len  length of the buffer.
 * \param[in]  from the path that defines the startpoint of the relative path.
 * \param[in]  to   the path that defines the endpoint of the relative path.
 *
 * \return -1 if the length of the absolute path of \p from or \p to, or the
 *         length of the resulting path exceed PATH_MAX.
 *         Length of the resulting path otherwise.
 */
int path_relative_to(char buf[static PATH_MAX], const char * nonnull from,
                     const char * nonnull to);

#endif

/* }}} */

#endif /* IS_LIB_COMMON_STR_IS_H */
