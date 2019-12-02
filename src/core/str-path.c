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

#include <lib-common/core.h>

/****************************************************************************/
/* simple file name splits                                                  */
/****************************************************************************/

/* MX: XXX: does not work like the usual libgen `basename`
 *
 * basename("foo////") == "foo" the rightmost '/' are not significant
 * basename("////") == "/"
 */
const char *path_filepart(const char *filename)
{
    const char *base = filename;
    for (;;) {
        filename = strchrnul(filename, '/');
        if (!*filename)
            return base;
        base = ++filename;
    }
}

ssize_t path_dirpart(char *dir, ssize_t size, const char *filename)
{
    return pstrcpymem(dir, size, filename, path_filepart(filename) - filename);
}

const char *path_ext(const char *filename)
{
    const char *base = path_filepart(filename);
    const char *lastdot = NULL;

    while (*base == '.') {
        base++;
    }
    for (;;) {
        base = strchrnul(base, '.');
        if (!*base)
            return lastdot;
        lastdot = base++;
    }
}

const char *path_extnul(const char *filename)
{
    const char *base = path_filepart(filename);
    const char *lastdot = NULL;

    while (*base == '.') {
        base++;
    }
    for (;;) {
        base = strchrnul(base, '.');
        if (!*base)
            return lastdot ? lastdot : base;
        lastdot = base++;
    }
}

/****************************************************************************/
/* libgen like functions                                                    */
/****************************************************************************/

int path_dirname(char *buf, int len, const char *path)
{
    const char *end = path + strlen(path);

    while (end > path && end[-1] == '/')
        --end;
    while (end > path && end[-1] != '/')
        --end;
    while (end > path && end[-1] == '/')
        --end;
    if (end > path)
        return pstrcpymem(buf, len, path, end - path);
    if (*path == '/')
        return pstrcpy(buf, len, "/");
    return pstrcpy(buf, len, ".");
}

int path_basename(char *buf, int len, const char *path)
{
    for (;;) {
        const char *end = strchrnul(path, '/');
        const char *p = end;
        while (*p == '/')
            p++;
        if (!*p)
            return pstrcpymem(buf, len, path, end - path);
        path = p;
    }
}

/****************************************************************************/
/* path manipulations                                                       */
/****************************************************************************/

int path_join(char *buf, int len, const char *path)
{
    int pos = strlen(buf);
    while (pos > 0 && buf[pos - 1] == '/')
        --pos;
    while (*path == '/') {
        path++;
    }
    pos += pstrcpy(buf + pos, len - pos, "/");
    pos += pstrcpy(buf + pos, len - pos, path);
    return pos;
}

/*
 * ^/../   -> ^/
 * /+      -> /
 * /(./)+  -> /
 * aaa/../ -> /
 * //+$    -> $
 */
int path_simplify2(char *in, bool keep_trailing_slash)
{
    bool absolute = *in == '/';
    char *start = in + absolute, *out = in + absolute;
    int atoms = 0;

    if (!*in)
        return -1;

    for (;;) {
        const char *p;

#if 0
        e_info("state: `%.*s` \tin: `%s`", (int)(out - (start - absolute)),
               start - absolute, in);
#endif
        while (*in == '/')
            in++;
        if (*in == '.') {
            if (in[1] == '/') {
                in += 2;
                continue;
            }
            if (in[1] == '.' && (!in[2] || in[2] == '/')) {
                in += 2;
                if (atoms) {
                    atoms--;
                    out--;
                    while (out > start && out[-1] != '/')
                        out--;
                } else {
                    if (!absolute) {
                        out = mempcpy(out, "..", 2);
                        if (*in)
                            *out++ = '/';
                    }
                }
                continue;
            }
        }

        in = strchrnul(p = in, '/');
        memmove(out, p, in - p);
        out += in - p;
        atoms++;
        if (!*in)
            break;
        *out++ = '/';
    }

    start -= absolute;
    if (!keep_trailing_slash && out > start && out[-1] == '/')
        --out;
    if (out == start)
        *out++ = '.';
    *out = '\0';
    return out - start;
}

/* TODO: make our own without the PATH_MAX craziness */
int path_canonify(char *buf, int len, const char *path)
{
    char *out = len >= PATH_MAX ? buf : p_alloca(char, PATH_MAX);

    out = RETHROW_PN(realpath(path, out));
    if (len < PATH_MAX)
        pstrcpy(buf, len, out);
    return strlen(out);
}

static const char *
__path_expand(char *buf, int len, const char *path, bool force_copy)
{
    static char const *env_home = NULL;
    static char const root[] = "/";

    if (path[0] == '~' && path[1] == '/') {
        if (!env_home) {
            env_home = getenv("HOME") ?: root;
        }
        if (env_home != root) {
            snprintf(buf, len, "%s%s", env_home, path + 1);
            return buf;
        }
        path++;
    }

    if (force_copy) {
        pstrcpy(buf, len, path);
        return buf;
    }

    return path;
}

/* Expand '~/' at the start of a path.
 * Ex: "~/tmp" => "getenv($HOME)/tmp"
 *     "~foobar" => "~foo" (don't use nis to get home dir for user foo !)
 * TODO?: expand all environment variables ?
 */
char *path_expand(char *buf, int len, const char *path)
{
    char path_l[PATH_MAX];

    assert (len >= PATH_MAX);

    path = __path_expand(path_l, sizeof(path_l), path, false);

    /* XXX: The use of path_canonify() here is debatable. */
    return path_canonify(buf, len, path) < 0 ? NULL : buf;
}


/* This function checks if the given path try to leave its chroot
 * XXX this do not work with symbolic links in path (but it's a feature ;) )*/
bool path_is_safe(const char *path)
{
    const char *ptr = path;

    /* Get rid of: '/', '../' and '..' */
    if (path[0] == '/') {
        return false;
    } else
    if (path[0] == '.' && path[1] == '.'
    &&  (path[2] == '/' || path[2] == '\0'))
    {
        return false;
    }

    /* Check for `.* '/../' .* | '/..'$` */
    while ((ptr = strchr(ptr, '/')) != NULL) {
        if (ptr[1] == '.' && ptr[2] == '.'
        &&  (ptr[3] == '/' || ptr[3] == '\0'))
        {
            return false;
        }
        ptr++;
    }
    return true;
}

int path_va_extend(char buf[static PATH_MAX], const char *prefix,
                   const char *fmt, va_list args)
{
    int prefix_len;
    int suffix_len;
    va_list cpy;

    prefix_len = pstrcpy(buf, PATH_MAX, prefix);

    if (prefix_len && prefix_len < PATH_MAX && buf[prefix_len - 1] != '/') {
        prefix_len = pstrcat(buf, PATH_MAX, "/");
    }

    if (unlikely(prefix_len >= PATH_MAX - 1)) {
        char temp_buf[PATH_MAX];

        va_copy(cpy, args);
        suffix_len = vsnprintf(temp_buf, 2, fmt, cpy);
        va_end(cpy);

        if (suffix_len < PATH_MAX && temp_buf[0] == '/') {
            return vsnprintf(buf, PATH_MAX, fmt, args);
        }
        if (suffix_len < PATH_MAX && temp_buf[0] == '~') {
            vsnprintf(temp_buf, PATH_MAX, fmt, args);
            __path_expand(buf, PATH_MAX, temp_buf, true);
            return strlen(buf);
        }

        if (!suffix_len && prefix_len == PATH_MAX - 1) {
            return prefix_len;
        }

        return -1;
    }

    va_copy(cpy, args);
    suffix_len = vsnprintf(buf + prefix_len, PATH_MAX - prefix_len, fmt, cpy);
    va_end(cpy);

    if (prefix_len && suffix_len
    &&  unlikely(buf[prefix_len] == '/' || buf[prefix_len] == '~'))
    {
        /* slow path: optimistic prediction failed */
        if (buf[prefix_len] == '~') {
            char temp_buf[PATH_MAX];

            vsnprintf(temp_buf, PATH_MAX, fmt, args);
            __path_expand(buf, PATH_MAX, temp_buf, true);
            return strlen(buf);
        } else
        if (prefix_len + suffix_len < PATH_MAX) {
            p_move(buf, &buf[prefix_len], suffix_len + 1);
            return suffix_len;
        } else {
            suffix_len = vsnprintf(buf, PATH_MAX, fmt, args);
            return suffix_len < PATH_MAX ? suffix_len : -1;
        }
    }
    return prefix_len + suffix_len < PATH_MAX ? prefix_len + suffix_len : -1;
}

int path_extend(char buf[static PATH_MAX],  const char *prefix,
                const char *fmt, ...)
{
    int pos = 0;
    va_list args;

    va_start(args, fmt);
    pos = path_va_extend(buf, prefix, fmt, args);
    va_end(args);

    return pos;
}

static int path_simplified_absolute(const char *path,
                                    bool keep_trailing_slash,
                                    char buf[static PATH_MAX])
{
    if (path[0] == '/') {
        THROW_ERR_IF(pstrcpy(buf, PATH_MAX, path) >= PATH_MAX);
    } else {
        char cwd[PATH_MAX];

        RETHROW_PN(getcwd(cwd, sizeof(cwd)));
        RETHROW(path_extend(buf, cwd, "%s", path));
    }

    return path_simplify2(buf, keep_trailing_slash);
}

int path_relative_to(char buf[static PATH_MAX], const char *from,
                     const char *to)
{
    char simpl_from[PATH_MAX];
    char simpl_to[PATH_MAX];
    const char *rem_from = NULL;
    const char *rem_to = NULL;
    const char *ptr_from = simpl_from;
    const char *ptr_to = simpl_to;
    int buf_len = PATH_MAX;
    int dir_len = 0;
    int cpy_len;

    RETHROW(path_simplified_absolute(from, true, simpl_from));
    RETHROW(path_simplified_absolute(to, false, simpl_to));

    while (*ptr_to && *ptr_from) {
        if (*ptr_from != *ptr_to) {
            break;
        }

        if (*ptr_from == '/') {
            rem_from = ptr_from;
            rem_to = ptr_to;
        }

        ptr_from++;
        ptr_to++;
    }

    assert (rem_from);
    assert (rem_to);

    /* 'from' and 'to' does not refer the same file/directory */
    if (ptr_to[0] != '\0'
    || (ptr_from[0] != '\0' && (ptr_from[0] != '/' || ptr_from[1] != '\0')))
    {
#define BACK_DIR  "../"
#define BACK_DIR_LEN  ((int)strlen(BACK_DIR))

        while ((rem_from = strchr(rem_from + 1, '/'))) {
            THROW_ERR_IF(buf_len <= BACK_DIR_LEN);
            buf = mempcpy(buf, BACK_DIR, BACK_DIR_LEN);
            buf_len -= BACK_DIR_LEN;
            dir_len += BACK_DIR_LEN;
        }

#undef BACK_DIR_LEN
#undef BACK_DIR
    }

    cpy_len = pstrcpy(buf, buf_len, rem_to + 1);
    THROW_ERR_IF(buf_len <= cpy_len);

    return dir_len + cpy_len;
}
