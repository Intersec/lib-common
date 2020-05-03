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

#include <glob.h>

#include <lib-common/el.h>
#include <lib-common/file-log.h>
#include <lib-common/z.h>

struct {
    int events[ LOG_FILE_DELETE + 1 ];
} log_files_calls_g;
#define _G log_files_calls_g


static void on_cb(struct log_file_t *file, enum log_file_event event,
                  const char *fpath, void *priv)
{
    _G.events[event]++;
}

static int z_check_file_permission(const char *prefix, uint32_t mode)
{
    glob_t globbuf;
    char buf[PATH_MAX];
    struct stat st;

    snprintf(buf, sizeof(buf), "%s_????????_??????.log", prefix);
    Z_ASSERT_EQ(glob(buf, GLOB_BRACE, NULL, &globbuf), 0);

    Z_ASSERT_EQ(globbuf.gl_pathc, 1u);

    Z_ASSERT_EQ(stat(globbuf.gl_pathv[0], &st), 0);
    Z_ASSERT_EQ(st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO), mode);

    Z_ASSERT_EQ(unlink(globbuf.gl_pathv[0]), 0);

    globfree(&globbuf);

    Z_HELPER_END;
}

Z_GROUP_EXPORT(file_log)
{
#define RANDOM_DATA_SIZE  (2 << 20)
#define NB_FILES          10

    Z_TEST(file_log_max_file_size, "check max_file_size") {
        t_scope;
        lstr_t      path = t_lstr_fmt("%*pMtmp_log",
                                      LSTR_FMT_ARG(z_tmpdir_g));
        char       *data = t_new_raw(char, RANDOM_DATA_SIZE);
        log_file_t *cfg;
        bool        waiting = true;

        Z_TEST_FLAGS("redmine_43539");

        /* read random stuff to avoid perfect compression */
        {
            int fd = open("/dev/urandom", O_RDONLY);
            Z_ASSERT_EQ(read(fd, data, RANDOM_DATA_SIZE), RANDOM_DATA_SIZE);
            close(fd);
        }

        /* create dummy log file */
        for(int i = 0; i < NB_FILES; i++) {
            lstr_t name = t_lstr_fmt("%s_19700101_%06d.log", path.s, i);
            file_t *file = file_open(name.s, FILE_WRONLY | FILE_CREATE, 0666);

            file_write(file, data, RANDOM_DATA_SIZE);
            IGNORE(file_close(&file));
        }

        cfg = log_file_new(path.s, LOG_FILE_COMPRESS);
        log_file_set_maxtotalsize(cfg, 1);
        log_file_set_file_cb(cfg, on_cb, NULL);

        Z_ASSERT_EQ(log_file_open(cfg, false), 0);
        Z_ASSERT_EQ(log_file_close(&cfg), 0);

        /* for each gz file, check if uncompressed file is still here */
        while (waiting) {
            glob_t globbuf;
            char buf[PATH_MAX];
            char **fv;
            int fc, ret;
            struct stat st;

            snprintf(buf, sizeof(buf), "%s_????????_??????.log.gz", path.s);
            ret = glob(buf, GLOB_BRACE, NULL, &globbuf);
            if (ret == GLOB_NOMATCH) {
                continue;
            }
            Z_ASSERT_EQ(ret, 0);

            fv = globbuf.gl_pathv;
            fc = globbuf.gl_pathc;
            if (fc == NB_FILES - 1) {
                waiting = false;
                for (int i = 0;  i < fc; i++) {
                    fv[i][strlen(fv[i]) - 3] = '\0';
                    if (stat(fv[i], &st) == 0) {
                        waiting = true;
                        break;
                    }
                }
            }
            globfree(&globbuf);
        }

        /* by calling log_file_open, we are sure that log_check_invariants
         * is called  */
        cfg = log_file_new(path.s, LOG_FILE_COMPRESS);
        log_file_set_maxtotalsize(cfg, 1);
        log_file_set_file_cb(cfg, on_cb, NULL);

        Z_ASSERT_EQ(log_file_open(cfg, false), 0);
        Z_ASSERT_EQ(log_file_close(&cfg), 0);

        /* last file may be reused */
        Z_ASSERT_GE(_G.events[LOG_FILE_DELETE], NB_FILES - 1);

        /* Properly wait for gzip children termination. */
        el_loop();
    } Z_TEST_END;
#undef RANDOM_DATA_SIZE
#undef NB_FILES

    Z_TEST(file_log_mode, "check file permissions") {
        t_scope;
        lstr_t path;
        log_file_t *log_file;

        Z_TEST_FLAGS("redmine_52590");

        path = t_lstr_fmt("%*pMtmp_log_mode", LSTR_FMT_ARG(z_tmpdir_g));

        log_file = log_file_new(path.s, 0);
        Z_ASSERT_EQ(log_file_open(log_file, false), 0);
        Z_ASSERT_EQ(log_file_close(&log_file), 0);

        /* check default perm of file */
        Z_HELPER_RUN(z_check_file_permission(path.s, 0644u));

        log_file = log_file_new(path.s, 0);
        log_file_set_mode(log_file, 0640);
        Z_ASSERT_EQ(log_file_open(log_file, false), 0);
        Z_ASSERT_EQ(log_file_close(&log_file), 0);

        Z_HELPER_RUN(z_check_file_permission(path.s, 0640u));
    } Z_TEST_END;
} Z_GROUP_END
