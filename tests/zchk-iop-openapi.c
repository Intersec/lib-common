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

#include <lib-common/iop-openapi.h>
#include <lib-common/z.h>

#include "iop/tstiop.iop.h"
#include "iop/tstiop_dox.iop.h"

/* {{{ Helpers */

static int
t_z_load_openapi_file(const char *filename, lstr_t *file)
{
    const char *path;

    path = t_fmt("%pL/test-data/openapi/%s", &z_cmddir_g, filename);
    Z_ASSERT_N(lstr_init_from_file(file, path, PROT_READ, MAP_SHARED));
    /* remove last newline from file */
    file->len--;

    Z_HELPER_END;
}

static int
z_check_yaml(iop_openapi_t *openapi, const char *filename,
             bool remove_schemas)
{
    t_scope;
    yaml_data_t data;
    yaml_pack_env_t *env;
    lstr_t file;
    SB_1k(sb);
    SB_1k(err);

    Z_ASSERT_N(t_iop_openapi_to_yaml(openapi, &data, &err));
    if (remove_schemas) {
        qv_remove(&data.obj->fields, 4);
    }
    env = t_yaml_pack_env_new();
    Z_ASSERT_N(t_yaml_pack_sb(env, &data, &sb, NULL));

    Z_HELPER_RUN(t_z_load_openapi_file(filename, &file));
    Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), file);

    Z_HELPER_END;
}

/* }}} */

Z_GROUP_EXPORT(iop_openapi)
{
    IOP_REGISTER_PACKAGES(&tstiop__pkg, &tstiop_dox__pkg);
    MODULE_REQUIRE(iop_openapi);

    Z_TEST(doc, "test the whole doc generation") {
        t_scope;
        iop_openapi_t *oa;

        oa = t_new_iop_openapi(LSTR("zoomin"), LSTR("0.2.3"), NULL,
                               LSTR("tes"));
        t_iop_openapi_set_description(oa, LSTR("sheo"));
        t_iop_openapi_set_security(oa, LSTR("my_sec"),
                                   OPENAPI_SECURITY_BASIC_HTTP);
        t_iop_openapi_set_server(oa, LSTR("http://localhost:1337"),
                                 LSTR("server description"));
        Z_HELPER_RUN(z_check_yaml(oa, "basic.yml", false));
    } Z_TEST_END;

    Z_TEST(iop_struct, "test the schema generation of IOP structs") {
        t_scope;
        iop_openapi_t *oa;

        /* simple, no dependencies */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_n__s);
        Z_HELPER_RUN(z_check_yaml(oa, "struct_n.yml", false));

        /* with dependencies on other structs */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_m__s);
        Z_HELPER_RUN(z_check_yaml(oa, "struct_m.yml", false));
        /* make sure the existing hash deduplicates already added elements */
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_m__s);
        Z_HELPER_RUN(z_check_yaml(oa, "struct_m.yml", false));

        /* with enums */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_l__s);
        Z_HELPER_RUN(z_check_yaml(oa, "struct_l.yml", false));

        /* with classes */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__struct_jpack_flags__s);
        /* with a repeated field referencing a class */
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_f__s);
        Z_HELPER_RUN(z_check_yaml(oa, "classes.yml", false));

        /* constraints */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__constraint_u__s);
        t_iop_openapi_add_struct(oa, &tstiop__constraint_d__s);
        Z_HELPER_RUN(z_check_yaml(oa, "constraints.yml", false));

        /* default values */
        oa = t_new_iop_openapi(LSTR("structs"), LSTR("2.3.1"), NULL,
                               LSTR_NULL_V);
        t_iop_openapi_add_struct(oa, &tstiop__my_struct_g__s);
        Z_HELPER_RUN(z_check_yaml(oa, "struct_g.yml", false));
    } Z_TEST_END;

    Z_TEST(iop_mod, "test paths generation of IOP modules") {
        t_scope;
        iop_openapi_t *oa;
        yaml_data_t data;
        SB_1k(err);

        /* check that it also generates schemas */
        oa = t_new_iop_openapi(LSTR("yay"), LSTR("0.0.1"), tstiop__t__modp,
                               LSTR("route"));
        Z_HELPER_RUN(z_check_yaml(oa, "iface_t.yml", false));

        oa = t_new_iop_openapi(LSTR("yay"), LSTR("0.0.1"),
                               tstiop__my_mod_a__modp, LSTR("yay"));
        /* XXX erase schemas, we only want to check the rpcs, without getting
         * flooded by the schemas descriptions */
        Z_HELPER_RUN(z_check_yaml(oa, "iface_a.yml", true));

        oa = t_new_iop_openapi(LSTR("yay"), LSTR("0.0.1"),
                               tstiop__my_mod_a__modp, LSTR("yay"));
        t_iop_openapi_whitelist_rpc(oa, LSTR("tstiop.MyIfaceA.funG"));
        Z_HELPER_RUN(z_check_yaml(oa, "iface_a_filtered.yml", false));

        /* test that an unused whitelist will fail the generation */
        oa = t_new_iop_openapi(LSTR("yay"), LSTR("0.0.1"),
                               tstiop__my_mod_a__modp, LSTR("yay"));
        t_iop_openapi_whitelist_rpc(oa, LSTR("invalid_name"));
        Z_ASSERT_NEG(t_iop_openapi_to_yaml(oa, &data, &err));
        Z_ASSERT_STREQUAL(err.data, "invalid whitelist");

        /* When an interface does not have any whitelisted rpcs, it is not
         * mentioned in the final document. */
        oa = t_new_iop_openapi(LSTR("yay"), LSTR("0.0.1"),
                               tstiop__both_iface__modp, LSTR("route"));
        t_iop_openapi_whitelist_rpc(oa, LSTR("tstiop.Iface.f"));
        Z_HELPER_RUN(z_check_yaml(oa, "iface_t.yml", false));
    } Z_TEST_END;

    Z_TEST(dox, "test inclusion of comments documentation") {
        t_scope;
        iop_openapi_t *oa;

        oa = t_new_iop_openapi(LSTR("tstdox"), LSTR("1.0.1"),
                               tstiop_dox__my_module__modp,
                               LSTR("tstdox"));
        Z_HELPER_RUN(z_check_yaml(oa, "dox.yml", false));
    } Z_TEST_END;

    MODULE_RELEASE(iop_openapi);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
