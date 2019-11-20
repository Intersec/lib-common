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

/* YES I'm sure: we don't use the ->stat structure in our fts ! */
#undef _LARGEFILE64_SOURCE
#undef _FILE_OFFSET_BITS
#include <fts.h>
#include <lib-common/unix.h>

int rmdir_r(const char *dir, bool only_content)
{
    char * const argv[2] = { (char *)dir, NULL };
    FTS *fts;
    FTSENT *ent;
    int res = 0;

    fts = fts_open(argv, FTS_NOSTAT | FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (!fts) {
        return -1;
    }

    while ((ent = fts_read(fts)) != NULL) {
        if (ent->fts_level <= 0) {
            continue;
        }
        switch (ent->fts_info) {
          case FTS_D:
            break;
          case FTS_DC:
            res = -1;
            /* There is no really appropriate errno for this error, so choose
             * the generic EIO one, and print an error. */
            errno = EIO;
            e_error("rmdir_r: cycle detected with directory `%s` while "
                    "removing `%s`", ent->fts_accpath, dir);
            goto end;
          case FTS_DNR:
          case FTS_ERR:
            res = -1;
            errno = ent->fts_errno;
            goto end;
          case FTS_DP:
            if (rmdir(ent->fts_accpath) < 0) {
                res = -1;
                goto end;
            }
            break;
          default:
            if (unlink(ent->fts_accpath)) {
                res = -1;
                goto end;
            }
            break;
        }
    }


    if (!only_content && rmdir(dir) < 0) {
        res = -1;
    }

  end:
    PROTECT_ERRNO(fts_close(fts));
    return res;
}

int rmdirat_r(int dfd, const char *dir, bool only_content)
{
    char path[PATH_MAX];
    int  dir_len;
    int  path_len;

    if (dfd == AT_FDCWD || *dir == '/') {
        return rmdir_r(dir, only_content);
    }

    dir_len = strlen(dir);
    path_len = RETHROW(fd_get_path(dfd, path, sizeof(path) - dir_len - 1));

    path[path_len++] = '/';
    p_copy(&path[path_len], dir, dir_len + 1);
    return rmdir_r(path, only_content);
}
