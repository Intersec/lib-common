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

#include <lib-common/datetime.h>
#include <lib-common/http.h>

static void mime_put_http_ctype(outbuf_t *ob, const char *path)
{
    t_scope;
    const char *fpart, *ext;
    bool has_enc = false;
    static const struct {
        int         elen;
        const char *ext;
        const char *ct;
        const char *ce;
    } map[] = {
#define E2(ext, ct, ce)  { sizeof(ext) - 1, ext, ct, ce }
#define E(ext, ct)       E2(ext, ct, NULL)
        E("dbg",   "text/plain"),
        E("cfg",   "text/plain"),
        E("err",   "text/plain"),
        E("log",   "text/plain"),
        E("lst",   "text/plain"),
        E("txt",   "text/plain"),

        E("wsdl",  "text/xml"),
        E("xml",   "text/xml"),
        E("xsd",   "text/xml"),
        E("xsl",   "text/xml"),

        E("htm",   "text/html"),
        E("html",  "text/html"),

        E("pcap",  "application/x-pcap"),

        E("pdf",   "application/pdf"),
        E("csv",   "application/csv"),

        E("tar",   "application/x-tar"),
        E2("tgz",  "application/x-tar", "gzip"),
        E2("tbz2", "application/x-tar", "bzip2"),

        E("rar",   "application/rar"),
        E("zip",   "application/zip"),
#undef E2
#undef E
    };

    fpart = path_filepart(path);
    fpart = t_dupz(fpart, strlen(fpart));
    ext   = path_extnul(fpart);
    if (strequal(ext, ".gz")) {
        ob_adds(ob, "Content-Encoding: gzip\r\n");
        has_enc = true;
        *(char *)ext = '\0';
        ext = path_extnul(fpart);
    } else
    if (strequal(ext, ".Z")) {
        ob_adds(ob, "Content-Encoding: compress\r\n");
        has_enc = true;
        *(char *)ext = '\0';
        ext = path_extnul(fpart);
    } else
    if (strequal(ext, ".bz2")) {
        ob_adds(ob, "Content-Encoding: bzip2\r\n");
        has_enc = true;
        *(char *)ext = '\0';
        ext = path_extnul(fpart);
    }

    if (*ext++ == '.') {
        int extlen = strlen(ext);

        for (int i = 0; i < countof(map); i++) {
            if (map[i].elen == extlen && !strcasecmp(map[i].ext, ext)) {
                ob_addf(ob, "Content-Type: %s\r\n", map[i].ct);
                if (!has_enc && map[i].ce)
                    ob_addf(ob, "Content-Encoding: %s\r\n", map[i].ce);
                return;
            }
        }
    }
    ob_adds(ob, "Content-Type: application/octet-stream\r\n");
}
static void httpd_query_reply_make_index_(httpd_query_t *q, int dfd,
                                         const struct stat *st, bool head)
{
    DIR *dir = fdopendir(dfd);
    outbuf_t *ob;

    if (!dir) {
        httpd_reject(q, NOT_FOUND, "");
        return;
    }

    ob = httpd_reply_hdrs_start(q, HTTP_CODE_OK, false);
    httpd_put_date_hdr(ob, "Last-Modified", st->st_mtime);
    ob_adds(ob, "Content-Type: text/html\r\n");
    httpd_reply_hdrs_done(q, -1, false);
    if (!head) {
        struct dirent *de;

        ob_adds(ob, "<html><body><h1>Index</h1>");

        rewinddir(dir);
        while ((de = readdir(dir))) {
            struct stat tmp;

            if (de->d_name[0] == '.')
                continue;
            if (fstatat(dfd, de->d_name, &tmp, AT_SYMLINK_NOFOLLOW))
                continue;
            if (S_ISDIR(tmp.st_mode)) {
                ob_addf(ob, "<a href=\"%s/\">%s/</a><br>", de->d_name, de->d_name);
            } else
            if (S_ISREG(tmp.st_mode)) {
                ob_addf(ob, "<a href=\"%s\">%s</a><br>", de->d_name, de->d_name);
            }
        }
        closedir(dir);

        ob_adds(ob, "</body></html>\r\n");
    }
    httpd_reply_done(q);
}

void httpd_reply_make_index(httpd_query_t *q, int dfd, bool head)
{
    struct stat st;

    if (fstat(dfd, &st)) {
        httpd_reject(q, NOT_FOUND, "");
    } else {
        httpd_query_reply_make_index_(q, dup(dfd), &st, head);
    }
}

void httpd_reply_file(httpd_query_t *q, int dfd, const char *file, bool head)
{
    int fd = openat(dfd, file, O_RDONLY);
    struct stat st;
    outbuf_t *ob;
    void *map = NULL;

    if (fd < 0)
        goto ret404;

    if (fstat(fd, &st))
        goto ret404;
    if (S_ISDIR(st.st_mode)) {
        if (file[strlen(file) - 1] != '/')
            goto ret404;
        httpd_reply_make_index(q, fd, head);
        return;
    }
    if (!S_ISREG(st.st_mode))
        goto ret404;
    if (!head && st.st_size > (16 << 10)) {
        map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            httpd_reject(q, INTERNAL_SERVER_ERROR, "mmap failed: %m");
            close(fd);
            return;
        }
        madvise(map, st.st_size, MADV_SEQUENTIAL);
    }

    ob = httpd_reply_hdrs_start(q, HTTP_CODE_OK, false);
    httpd_put_date_hdr(ob, "Last-Modified", st.st_mtime);
    if (st.st_mtime >= lp_getsec() - 10) {
        ob_addf(ob, "ETag: W/\"%jx-%jxx-%lx\"\r\n",
                (int64_t)st.st_ino, st.st_size, st.st_mtime);
    } else {
        ob_addf(ob, "ETag: \"%jx-%jxx-%lx\"\r\n",
                (int64_t)st.st_ino, st.st_size, st.st_mtime);
    }
    mime_put_http_ctype(ob, file);
    httpd_reply_hdrs_done(q, st.st_size, false);
    if (!head) {
        if (map) {
            ob_add_memmap(ob, map, st.st_size);
        } else {
            ob_xread(ob, fd, st.st_size);
        }
    }
    httpd_reply_done(q);
    close(fd);
    return;

  ret404:
    if (fd >= 0)
        close(fd);
    httpd_reject(q, NOT_FOUND, "");
}


typedef struct httpd_trigger__dir_t {
    httpd_trigger_t cb;
    int  dirlen;
    char dirpath[];
} httpd_trigger__dir_t;

static void httpd_trigger__dir_cb(httpd_trigger_t *cb, httpd_query_t *q,
                                  const httpd_qinfo_t *req)
{
    t_scope;
    httpd_trigger__dir_t *cb2 = container_of(cb, httpd_trigger__dir_t, cb);
    char *buf;

    buf = t_new_raw(char, cb2->dirlen + ps_len(&req->query) + 1);
    memcpyz(mempcpy(buf, cb2->dirpath, cb2->dirlen),
            req->query.s, ps_len(&req->query));
    httpd_reply_file(q, AT_FDCWD, buf, req->method == HTTP_METHOD_HEAD);
}

httpd_trigger_t *httpd_trigger__static_dir_new(const char *path)
{
    int len   = strlen(path);
    httpd_trigger__dir_t *cbdir = p_new_extra(httpd_trigger__dir_t, len + 1);

    while (len > 0 && path[len - 1] == '/')
        len--;
    cbdir->cb.cb  = &httpd_trigger__dir_cb;
    cbdir->dirlen = len;
    memcpy(cbdir->dirpath, path, len + 1);
    return &cbdir->cb;
}
