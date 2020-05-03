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

#include <lib-common/z.h>
#include <lib-common/farch.h>
#include "zchk-farch.fc.c"

Z_GROUP_EXPORT(farch)
{
    static const char *farch_filenames[] = {
        "test-data/farch/zchk-farch-intersec.txt",
        "test-data/farch/zchk-farch-five-intersec.txt",
        "test-data/farch/zchk-farch-lorem.txt",
    };

    MODULE_REQUIRE(farch);

    Z_TEST(farch, "") {
        for (int i = 0; i < countof(farch_filenames); i++) {
            t_scope;
            const farch_entry_t *entry = &farch_test[i];
            char ffilename[FARCH_MAX_FILENAME];
            const char *filename = farch_filenames[i];
            const char *path;
            lstr_t fcontents;
            lstr_t fcontents_persist;
            lstr_t contents;

            path = t_fmt("%*pM/%s", LSTR_FMT_ARG(z_cmddir_g), filename);
            Z_ASSERT_ZERO(lstr_init_from_file(&contents, path, PROT_READ,
                                              MAP_SHARED));

            /* test get_filename */
            Z_ASSERT_P(farch_get_filename(entry, ffilename));
            Z_ASSERT_STREQUAL(ffilename, filename);

            /* test get_data */
            fcontents = t_farch_get_data(farch_test, filename);
            Z_ASSERT_LSTREQUAL(fcontents, contents);
            fcontents = t_farch_get_data(entry, NULL);
            Z_ASSERT_LSTREQUAL(fcontents, contents);

            /* test get_data_persist */
            fcontents = farch_get_data_persist(farch_test, filename);
            Z_ASSERT_LSTREQUAL(fcontents, contents);
            fcontents_persist = farch_get_data_persist(entry, NULL);
            Z_ASSERT_LSTREQUAL(fcontents_persist, contents);
            Z_ASSERT(fcontents_persist.s == fcontents.s);

            lstr_wipe(&contents);
        }
    } Z_TEST_END;

    MODULE_RELEASE(farch);
} Z_GROUP_END
