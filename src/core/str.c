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

/** Skips initial blanks as per isspace(c).
 *
 * use vskipspaces for non const parameters
 * @see vskipspaces
 *
 * @return a pointer to the first non white space character in s.
 */
const char *skipspaces(const char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** Go to the next blank as per isspace(c).
 *
 *
 * @return a pointer to the first white space character in s
 * or points to the end of the string.
 *
 */
const char *strnextspace(const char *s)
{
    while (*s && !isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** Skips initial blanks as per isblank(c).
 *
 * use vskipblanks for non const parameters
 * @see vskipblanks
 *
 * @return a pointer to the first non white space character in s.
 */
const char *skipblanks(const char *s)
{
    while (isblank((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** Replaces blank characters at end of string with '\0'.
 *
 * @return a pointer to the \0 at the end of str
 */
char *strrtrim(char *str)
{
    if (str) {
        char *p = str + strlen(str);

        while (p > str && isspace((unsigned char)p[-1])) {
            *--p = '\0';
        }
        return p;
    }

    return NULL;
}

/** Tells whether str begins with p.
 *
 * @param pp if not null and str begins with p, pp is given the address of the
 * first following character in str.
 *
 * @return 1 if a match is found, 0 otherwise.
 */
int strstart(const char *str, const char *p, const char **pp)
{
    if (!str)
        return 0;

    while (*p) {
        if (*str++ != *p++)
            return 0;
    }
    if (pp)
        *pp = str;
    return 1;
}

/** Tells whether str begins with p, case insensitive.
 *
 * @param pp if not null and str begins with p, pp is given the address of the
 * first following character in str.
 *
 * @return 1 if a match is found, 0 otherwise.
 */
int stristart(const char *str, const char *p, const char **pp)
{
    if (!str)
        return 0;

    while (*p) {
        if (tolower((unsigned char)*str++) != tolower((unsigned char)*p++))
            return 0;
    }
    if (pp)
        *pp = str;
    return 1;
}


/** Find the first occurrence of the substring needle in str, case
 *  insensitively.
 *
 * @return a pointer to the beginning of the substring, or NULL if
 * non was found.
 * a final embedded '\0' char in needle can match the final '\0'.
 */
const char *stristrn(const char *str, const char *needle, size_t nlen)
{
    size_t i;
    int c, nc;

    if (!nlen)
        return str;

    nc = toupper((unsigned char)*needle);
    for (;; str++) {
        /* find first char of pattern in str */
        c = toupper((unsigned char)*str);
        if (c != nc) {
            if (c == '\0')
                return NULL;
        } else {
            /* compare the rest of needle */
            for (i = 1;; i++) {
                if (i == nlen)
                    return str;
                if (c == '\0')
                    return NULL;
                c = toupper((unsigned char)str[i]);
                if (c != toupper((unsigned char)needle[i]))
                    break;
            }
        }
    }
}


/* find a word in a list of words separated by sep.
 */
bool strfind(const char *keytable, const char *str, int sep)
{
    int c, len;
    const char *p;

    c = *str;
    len = strlen(str);
    /* need to special case the empty string */
    if (len == 0) {
        char empty[3];
        empty[0] = empty[1] = sep;
        empty[2] = '\0';
        return strstr(keytable, empty) != NULL;
    }

    /* initial and trailing separators are optional */
    /* they do not cause the empty string to match */
    for (p = keytable;;) {
        if (!memcmp(p, str, len) && (p[len] == sep || p[len] == '\0'))
            return true;
        for (;;) {
            p = strchr(p + 1, c);
            if (!p)
                return false;
            if (p[-1] == sep)
                break;
        }
    }
}


/** Increment last counter in a buffer
 *
 * <code>buf</code> points to the start of the buffer.
 * <code>len</code> can be negative, in this case <code>len=strlen(buf)</code>
 *
 * Examples :
 *  "000" => "001"
 *  "999" => "000"
 *  "foobar-0-01" => "foobar-0-02"
 *  "foobar-0-99" => "foobar-0-00"
 *
 * @return 1 if an overflow occurred, 0 otherwise
 */
int buffer_increment(char *buf, int len)
{
    int pos;

    if (!buf) {
        return 1;
    }
    if (len < 0) {
        len = strlen(buf);
    }
    for (pos = len - 1; pos >= 0; pos--) {
        switch (buf[pos]) {
          case '0': case '1': case '2': case '3': case '4': case '5':
          case '6': case '7': case '8':
            buf[pos]++;
            return 0;
          case '9':
            buf[pos] = '0';
            break;
          default:
            return 1;
        }
    }
    return 1;
}


/** Increment last counter in an hexadecimal buffer
 *
 * <code>buf</code> points to the start of the buffer.
 * <code>len</code> can be negative, in this case <code>len=strlen(buf)</code>
 *
 * Examples :
 *  "000" => "001"
 *  "999" => "99A"
 *  "foobar-0-01" => "foobar-0-02"
 *  "foobar-0-FF" => "foobar-0-00"
 *
 * @return 1 if an overflow occurred, 0 otherwise
 */
int buffer_increment_hex(char *buf, int len)
{
    int pos;

    if (!buf) {
        return 1;
    }
    if (len < 0) {
        len = strlen(buf);
    }
    for (pos = len - 1; pos >= 0; pos--) {
        switch (buf[pos]) {
          case '0': case '1': case '2': case '3': case '4': case '5':
          case '6': case '7': case '8':
            buf[pos]++;
            return 0;
          case '9':
            buf[pos] = 'A';
            return 0;
          case 'a': case 'A': case 'b': case 'B': case 'c': case 'C':
          case 'd': case 'D': case 'e': case 'E':
            buf[pos]++;
            return 0;
          case 'F': case 'f':
            buf[pos] = '0';
            break;
          default:
            return 1;
        }
    }
    return 1;
}

/** Put random characters from alphabet in destination buffer, and
 *  terminate with a '\0' character.
 *  If no alphabet given, a 64 bytes alphabet will be used.
 *
 * @param dest the output buffer.
 *
 * @param size the size of the buffer
 *
 * @param alphabet a pointer to a lstr_t containing the alphabet we want
 * to use. If LSTR_NULL a default b64 alphabet will be used
 *
 * @return the number of bytes (excluding '\0') written in the buffer
 */
size_t strrand(char dest[], size_t dest_size, lstr_t alphabet)
{
    char *p = dest;
    char *last = p + dest_size - 1;

    if (!alphabet.s && alphabet.len == 0) {
        alphabet = LSTR("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop"
                        "qrstuvwxyz0123456789+/");
    }

    assert (alphabet.len > 0);
    THROW_ERR_IF(alphabet.len <= 0);

    for (; p < last; p++) {
        *p = *(alphabet.s + rand_range(0, alphabet.len - 1));
    }

    *p = '\0';
    return dest_size - 1;
}

/** Put random hexadecimal digits in destination buffer
 *
 * @return the number of digits set.
 */
ssize_t pstrrand(char *dest, ssize_t size, int offset, ssize_t n)
{
    n = MIN(size - offset, n + 1);
    if (n < 0) {
        return -1;
    }

    /* RFE: This is very naive. Should at least call ha_rand() only every 4
     * bytes. */
    return strrand(dest + offset, n, LSTR("0123456789ABCDEF"));
}

int str_replace(const char search, const char replace, char *subject)
{
    int nb_replace = 0;
    char *p = subject;

    p = strchr(p, search);
    while (p && *p != '\0') {
        *p = replace;
        /* Read replace char */
        p++;
        nb_replace++;
        p = strchr(p, search);
    }
    return nb_replace;
}

size_t pstrcpymem(char *dest, ssize_t size, const void *src, size_t n)
{
    size_t clen = n;

    if (size > 0) {
        if (unlikely(clen > (size_t)size - 1))
            clen = (size_t)size - 1;
        memcpyz(dest, src, clen); /* assuming no overlap */
    }
    return n;
}
