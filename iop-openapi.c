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

#include "iop-openapi.h"

/* XXX: then OpenAPI specification used is 3.0.2.
 *
 * Every *_object_t struct is mapped on its respective object as described in
 * the spec.
 * Please refer to
 * https://github.com/OAI/OpenAPI-Specification/blob/3.0.2/versions/3.0.2.md
 * for a better understanding of the different objects.
 */

/* {{{ Info object */

typedef struct info_object_t {
    lstr_t title;
    lstr_t version;
    lstr_t description;
} info_object_t;

static void t_info_object_to_yaml(const info_object_t * nonnull info,
                                  yaml_data_t * nonnull out)
{
    yaml_data_t data;

    t_yaml_data_new_obj(out, 3);

    yaml_data_set_string(&data, info->title);
    yaml_obj_add_field(out, LSTR("title"), data);

    yaml_data_set_string(&data, info->version);
    yaml_obj_add_field(out, LSTR("version"), data);

    if (info->description.s) {
        yaml_data_set_string(&data, info->description);
        yaml_obj_add_field(out, LSTR("description"), data);
    }
}

/* }}} */
/* {{{ IOP OpenAPI objects */

struct iop_openapi_t {
    info_object_t info;
};

/* }}} */
/* {{{ Public API */

iop_openapi_t *t_new_iop_openapi(const lstr_t title, const lstr_t version,
                                 const lstr_t description)
{
    iop_openapi_t *oa = t_new(iop_openapi_t, 1);

    oa->info.title = t_lstr_dup(title);
    oa->info.version = t_lstr_dup(version);
    oa->info.description = t_lstr_dup(description);

    return oa;
}

void t_iop_openapi_to_yaml(const iop_openapi_t *openapi, yaml_data_t *out)
{
    yaml_data_t data;

    t_yaml_data_new_obj(out, 0);

    yaml_data_set_string(&data, LSTR("3.0.2"));
    yaml_obj_add_field(out, LSTR("openapi"), data);

    t_info_object_to_yaml(&openapi->info, &data);
    yaml_obj_add_field(out, LSTR("info"), data);
}

/* }}} */
/* {{{ Module */

static int iop_openapi_initialize(void *arg)
{
    return 0;
}

static int iop_openapi_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(iop_openapi)
    MODULE_DEPENDS_ON(yaml);
MODULE_END()

/* }}} */
/* {{{ Tests */

/* LCOV_EXCL_START */

#include "z.h"

/* {{{ Helpers */

static const char *
t_z_openapi_to_str(const iop_openapi_t *openapi)
{
    t_SB_1k(sb);
    yaml_data_t data;

    t_iop_openapi_to_yaml(openapi, &data);
    yaml_pack_sb(&data, &sb);

    return sb.data;
}

/* }}} */

/* Unit-tests about YAML->AST parsing. */

Z_GROUP_EXPORT(iop_openapi)
{
    MODULE_REQUIRE(iop_openapi);

    /* {{{ YAML generation */

    Z_TEST(yaml, "test the yaml generation") {
        t_scope;
        iop_openapi_t *oa;

        oa = t_new_iop_openapi(LSTR("zoomin"), LSTR("1.0.0"), LSTR_NULL_V);

        /* unexpected EOF */
        Z_ASSERT_STREQUAL(
            t_z_openapi_to_str(oa),
            "openapi: 3.0.2\n"
            "info:\n"
            "  title: zoomin\n"
            "  version: 1.0.0"
        );

        oa = t_new_iop_openapi(LSTR("juice"), LSTR("0.2.3"), LSTR("sheo"));

        /* unexpected EOF */
        Z_ASSERT_STREQUAL(
            t_z_openapi_to_str(oa),
            "openapi: 3.0.2\n"
            "info:\n"
            "  title: juice\n"
            "  version: 0.2.3\n"
            "  description: sheo"
        );
    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(iop_openapi);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
