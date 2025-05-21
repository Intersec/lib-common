/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
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

#include <lib-common/iop-json.h>
#include <lib-common/xmlr.h>
#include <lib-common/iop-yaml.h>
#include <lib-common/zbenchmark.h>


static iop_dso_t *z_dso_open(iop_env_t *iop_env, const char *dso_path)
{
    t_scope;
    SB_1k(err);
    char file_path[PATH_MAX];
    char bench_dir[PATH_MAX];
    char root_dir[PATH_MAX];
    char path[PATH_MAX];
    iop_dso_t *dso;

    path_canonify(file_path, sizeof(file_path), __FILE__);
    path_dirname(bench_dir, sizeof(bench_dir), file_path);
    path_dirname(root_dir, sizeof(root_dir), bench_dir);
    path_extend(path, root_dir, "%s", dso_path);
    dso = iop_dso_open(iop_env, path, &err);
    if (dso == NULL) {
        e_fatal("unable to load `%s`, TOOLS repo? (%*pM)",
                path, SB_FMT_ARG(&err));
    }

    return dso;
}

#include "../tests/iop/tstiop.iop.h"

ZBENCH_GROUP_EXPORT(iop_pack) {
    iop_env_t *iop_env;
    iop_dso_t *dso;
    const iop_struct_t *st_sa;
    /* {{{ */

    const iop_struct_t *st_cls2;
    tstiop__my_union_a__t un = IOP_UNION(tstiop__my_union_a, ua, 1);
    tstiop__my_class2__t cls2;
#if 0
    const char *jpacked_sa =
        "{\"a\":42,\"b\":5,\"cOfMyStructA\":120,\"d\":230,\"e\":540,"
        "\"f\":2000,\"g\":10000,\"h\":20000,\"i\":\"Zm9v\","
        "\"j\":\"bar\\u00e9\\u00a9 \\u0022 foo .\","
        "\"xmlField\":\"<foo />\",\"k\":\"B\",\"l\":{\"ub\":42},"
        "\"lr\":{\"ua\":1},\"cls2\":{\"int1\":1,\"int2\":2},"
        "\"m\":3.14159265000000021e+00,\"n\":true,\"p\":46,\"q\":33,"
        "\"r\":42,\"s\":43,\"t\":9}";
#endif
    tstiop__my_struct_a__t sa = {
        .a = 42,
        .b = 5,
        .c_of_my_struct_a = 120,
        .d = 230,
        .e = 540,
        .f = 2000,
        .g = 10000,
        .h = 20000,
        .i = LSTR_IMMED("foo"),
        .j = LSTR_IMMED("baré© \" foo ."),
        .xml_field = LSTR_IMMED("<foo />"),
        .k = MY_ENUM_A_B,
        .l = IOP_UNION(tstiop__my_union_a, ub, 42),
        .lr = &un,
        .cls2 = &cls2,
        .m = 3.14159265,
        .n = true,
        .p = '.',
        .q = '!',
        .r = '*',
        .s = '+',
        .t = '\t',
    };

    iop_env = iop_env_new();
    dso = z_dso_open(iop_env, "tests/iop/zchk-tstiop-plugin" SO_FILEEXT);

    st_cls2 = iop_env_get_struct(iop_env, LSTR("tstiop.MyClass2"));
    iop_init_desc(st_cls2, &cls2);
    cls2.int1 = 1;
    cls2.int2 = 2;

    st_sa = iop_env_get_struct(iop_env, LSTR("tstiop.MyStructA"));

    /* }}} */

    /* json */
    {
        t_scope;
        int res = 0;
        SB_1k(out);

        ZBENCH(jpack) {
            ZBENCH_LOOP() {
                sb_reset(&out);

                ZBENCH_MEASURE() {
                    res = iop_sb_jpack(&out, st_sa, &sa, IOP_JPACK_MINIMAL);
                } ZBENCH_MEASURE_END

                if (res < 0
                /*|| !lstr_equal(LSTR(jpacked_sa), LSTR_SB_V(&out))*/)
                {
                    e_panic("KO");
                }
            } ZBENCH_LOOP_END
        } ZBENCH_END

        ZBENCH(junpack) {
            ZBENCH_LOOP() {
                t_scope;
                pstream_t ps = ps_initsb(&out);
                tstiop__my_struct_a__t *sa2 = NULL;

                ZBENCH_MEASURE() {
                    res = t_iop_junpack_ptr_ps(iop_env, &ps, st_sa,
                                               (void **)&sa2, 0, NULL);
                } ZBENCH_MEASURE_END

                if (res < 0 || !iop_equals_desc(st_sa, &sa, sa2)) {
                    e_panic("KO");
                }
            } ZBENCH_LOOP_END
        } ZBENCH_END
    }
    /* bin */
    {
        t_scope;
        lstr_t out = LSTR_NULL;
        int res = 0;

        ZBENCH(bpack) {
            ZBENCH_LOOP() {
                ZBENCH_MEASURE() {
                    out = t_iop_bpack_struct(st_sa, &sa);
                } ZBENCH_MEASURE_END

                if (!out.s) {
                    e_panic("KO");
                }
            } ZBENCH_LOOP_END
        } ZBENCH_END

        ZBENCH(bpack) {
            ZBENCH_LOOP() {
                t_scope;
                void *sa2 = NULL;

                ZBENCH_MEASURE() {
                    res = iop_bunpack_ptr_flags(t_pool(), iop_env, st_sa,
                                                &sa2, ps_initlstr(&out), 0);
                } ZBENCH_MEASURE_END

                if (res < 0 || !iop_equals_desc(st_sa, &sa, sa2)) {
                    e_panic("KO");
                }
            } ZBENCH_LOOP_END
        } ZBENCH_END
    }
#if 0
    /* {{{ XML */

    /* xml */
    {
        t_scope;
        SB_1k(out);
        int res;

        sb_adds(&out,
                "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
                ">");
        iop_xpack(&out, st_sa, &sa, false, false);
        sb_adds(&out, "</root>\n");

        {
            t_scope;
            xml_reader_t xml_reader;
            void *sa2 = NULL;

            p_clear(&xml_reader, 1);

            res = xmlr_setup(&xml_reader, out.data, out.len);
            assert (res >= 0);

            res = iop_xunpack_ptr(&xml_reader, t_pool(), st_sa, &sa2);

            if (res < 0 || !iop_equals_desc(st_sa, &sa, sa2)) {
                e_panic("KO");
            }
        }
    }

    /* }}} */
#endif

    /* yaml */
    {
        t_scope;
        SB_1k(out);
        int res = 0;

        ZBENCH(ypack) {
            ZBENCH_LOOP() {
                sb_reset(&out);

                ZBENCH_MEASURE() {
                    t_iop_sb_ypack(&out, st_sa, &sa, NULL);
                } ZBENCH_MEASURE_END
            } ZBENCH_LOOP_END
        } ZBENCH_END

        ZBENCH(yunpack) {
            ZBENCH_LOOP() {
                t_scope;
                SB_1k(err);
                void *sa2 = NULL;
                pstream_t ps = ps_initsb(&out);

                ZBENCH_MEASURE() {
                    res = t_iop_yunpack_ptr_ps(iop_env, &ps, st_sa, &sa2, 0,
                                               NULL, &err);
                } ZBENCH_MEASURE_END

                /* FIXME pack/unpack of `.m = 3.14159265,` changes the value
                 */
                if (res < 0 /*|| !iop_equals_desc(st_sa, &sa, sa2)*/) {
                    e_panic("KO");
                }
            } ZBENCH_LOOP_END
        } ZBENCH_END
    }

    iop_dso_close(&dso);
    iop_env_delete(&iop_env);
} ZBENCH_GROUP_END
