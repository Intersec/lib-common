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

#include <lib-common/unix.h>
#include "iopc.h"

int
iopc_set_path(const char *outdir, const iopc_pkg_t *pkg,
              const char *ext, int max_len, char *path,
              bool only_pkg)
{
    char dpath[max_len];
    int res;

    if (outdir) {
        if (only_pkg) {
            lstr_t sp = LSTR(pretty_path(pkg->name));

            /* pretty path force the .iop extension which may differs of the
             * given extension. */
            sp.len -= strlen(".iop");

            res = snprintf(path, max_len, "%s/%*pM%s", outdir,
                           LSTR_FMT_ARG(sp), ext);
        } else {
            res = snprintf(path, max_len, "%s/%s%s", outdir,
                           pretty_path_base(pkg->name), ext);
        }
        path_dirname(dpath, max_len, path);
        if (mkdir_p(dpath, 0777) < 0) {
            throw_error("cannot create directory `%s`: %m", dpath);
        }
    } else {
        path_dirname(dpath, max_len, pkg->file);
        res = snprintf(path, max_len, "%s/%s%s", dpath,
                       pretty_path_base(pkg->name), ext);
    }
    return res;
}

int iopc_write_file(const sb_t *buf, const char *path)
{
    if (unlink(path) < 0 && errno != ENOENT) {
        throw_error("unable to remove existing file `%s`: %m", path);
    }
    if (sb_write_file(buf, path) < 0 || chmod(path, 0444) < 0) {
        PROTECT_ERRNO(unlink(path));
        throw_error("unable to write file `%s`: %m", path);
    }
    return 0;
}

void iopc_write_buf_init(iopc_write_buf_t *wbuf, sb_t *buf, sb_t *tab)
{
    wbuf->buf = buf;
    wbuf->tab = tab;
    sb_addc(tab, '\n');
}

void iopc_write_buf_tab_inc(const iopc_write_buf_t* wbuf)
{
    sb_addc(wbuf->tab, '\t');
}

void iopc_write_buf_tab_dec(const iopc_write_buf_t* wbuf)
{
    sb_shrink(wbuf->tab, 1);
}
