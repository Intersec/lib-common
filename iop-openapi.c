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

/* {{{ Schema object */

typedef enum openapi_type_t {
    TYPE_STRING,
    TYPE_BYTE,
    TYPE_BOOL,
    TYPE_DOUBLE,

    TYPE_INT32,
    TYPE_INT64,

    /* XXX: these are not standard but common format extensions */
    TYPE_UINT32,
    TYPE_UINT64,
} openapi_type_t;

typedef struct schema_object_t {
    /* Not part of "Schema object" by the spec, but useful to easily create
     * the name -> object mappings. */
    lstr_t name;

    openapi_type_t type;

    opt_i64_t minimum; // nullable
    opt_i64_t maximum; // nullable

    bool _nullable;
} schema_object_t;
qvector_t(schema_object, schema_object_t);

static void
t_schema_object_to_yaml(const schema_object_t * nonnull obj,
                        yaml_data_t * nonnull out)
{
    yaml_data_t data;
    lstr_t type = LSTR_NULL_V;
    lstr_t format = LSTR_NULL_V;

    t_yaml_data_new_obj(out, 2);
    switch (obj->type) {
      case TYPE_STRING:
        type = LSTR("string");
        break;

      case TYPE_BYTE:
        type = LSTR("string");
        format = LSTR("byte");
        break;

      case TYPE_BOOL:
        type = LSTR("bool");
        break;

      case TYPE_DOUBLE:
        type = LSTR("number");
        format = LSTR("double");
        break;

      case TYPE_INT32:
        type = LSTR("integer");
        format = LSTR("int32");
        break;

      case TYPE_INT64:
        type = LSTR("integer");
        format = LSTR("int64");
        break;

      case TYPE_UINT32:
        type = LSTR("integer");
        format = LSTR("uint32");
        break;

      case TYPE_UINT64:
        type = LSTR("integer");
        format = LSTR("uint64");
        break;
    }

    yaml_data_set_string(&data, type);
    yaml_obj_add_field(out, LSTR("type"), data);

    if (format.s) {
        yaml_data_set_string(&data, format);
        yaml_obj_add_field(out, LSTR("format"), data);
    }

    if (OPT_ISSET(obj->minimum)) {
        yaml_data_set_int(&data, OPT_VAL(obj->minimum));
        yaml_obj_add_field(out, LSTR("minimum"), data);
    }

    if (OPT_ISSET(obj->maximum)) {
        yaml_data_set_int(&data, OPT_VAL(obj->maximum));
        yaml_obj_add_field(out, LSTR("maximum"), data);
    }

    if (obj->_nullable) {
        yaml_data_set_bool(&data, true);
        yaml_obj_add_field(out, LSTR("nullable"), data);
    }
}

/* }}} */
/* {{{ Components object */

typedef struct components_object_t {
    qv_t(schema_object) schemas;
} components_object_t;

static schema_object_t * nonnull
add_schema_object(lstr_t name, openapi_type_t type,
                   qv_t(schema_object) * nonnull schemas)
{
    schema_object_t *obj = qv_growlen0(schemas, 1);

    obj->name = name;
    obj->type = type;

    return obj;
}

static void add_iop_primitives_schemas(qv_t(schema_object) *schemas)
{
    schema_object_t *schema;

#define ADD_INTEGER_SCHEMA(_name, _type, _min, _max)                         \
    do {                                                                     \
        schema_object_t *new_schema;                                         \
                                                                             \
        new_schema = add_schema_object(LSTR(_name), TYPE_##_type, schemas);  \
        OPT_SET(new_schema->minimum, (_min));                                \
        OPT_SET(new_schema->maximum, (_max));                                \
    } while(0)

    ADD_INTEGER_SCHEMA("iop:i8",  INT32,   INT8_MIN,   INT8_MAX);
    ADD_INTEGER_SCHEMA("iop:i16", INT32,  INT16_MIN,  INT16_MAX);
    ADD_INTEGER_SCHEMA("iop:i32", INT32,  INT32_MIN,  INT32_MAX);
    ADD_INTEGER_SCHEMA("iop:u8",  UINT32,         0,  UINT8_MAX);
    ADD_INTEGER_SCHEMA("iop:u16", UINT32,         0, UINT16_MAX);
    ADD_INTEGER_SCHEMA("iop:u32", UINT32,         0, UINT32_MAX);
    add_schema_object(LSTR("iop:i64"), TYPE_INT64, schemas);
    schema = add_schema_object(LSTR("iop:u64"), TYPE_UINT64, schemas);
    OPT_SET(schema->minimum, 0);

    add_schema_object(LSTR("iop:bool"), TYPE_BOOL, schemas);
    add_schema_object(LSTR("iop:string"), TYPE_STRING, schemas);
    add_schema_object(LSTR("iop:data"), TYPE_BYTE, schemas);
    add_schema_object(LSTR("iop:xml"), TYPE_STRING, schemas);
    schema = add_schema_object(LSTR("iop:void"), TYPE_BOOL, schemas);
    schema->_nullable = true;
}

static void t_components_object_init(components_object_t *obj)
{
    t_qv_init(&obj->schemas, 0);
    add_iop_primitives_schemas(&obj->schemas);
}

static void
t_components_object_to_yaml(const components_object_t * nonnull obj,
                                  yaml_data_t * nonnull out)
{
    yaml_data_t schemas;

    t_yaml_data_new_obj(out, 1);

    t_yaml_data_new_obj(&schemas, obj->schemas.len);
    tab_for_each_ptr(schema, &obj->schemas) {
        yaml_data_t elem;

        t_schema_object_to_yaml(schema, &elem);
        yaml_obj_add_field(&schemas, schema->name, elem);
    }
    yaml_obj_add_field(out, LSTR("schemas"), schemas);
}

/* }}} */
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
/* {{{ Public API */

struct iop_openapi_t {
    info_object_t info;

    components_object_t components;
};

iop_openapi_t *t_new_iop_openapi(const lstr_t title, const lstr_t version,
                                 const lstr_t description)
{
    iop_openapi_t *oa = t_new(iop_openapi_t, 1);

    oa->info.title = t_lstr_dup(title);
    oa->info.version = t_lstr_dup(version);
    oa->info.description = t_lstr_dup(description);

    t_components_object_init(&oa->components);

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

    t_components_object_to_yaml(&openapi->components, &data);
    yaml_obj_add_field(out, LSTR("components"), data);
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

static int
z_check_yaml(const iop_openapi_t *openapi, const char *filename)
{
    t_scope;
    const char *path;
    yaml_data_t data;
    lstr_t file;
    SB_1k(sb);

    t_iop_openapi_to_yaml(openapi, &data);
    yaml_pack_sb(&data, &sb);

    path = t_fmt("%pL/test-data/openapi/%s", &z_cmddir_g, filename);
    Z_ASSERT_N(lstr_init_from_file(&file, path, PROT_READ, MAP_SHARED));

    /* remove last newline from file */
    file.len--;

    Z_ASSERT_LSTREQUAL(file, LSTR_SB_V(&sb));

    Z_HELPER_END;
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

        oa = t_new_iop_openapi(LSTR("zoomin"), LSTR("0.2.3"), LSTR("sheo"));
        Z_HELPER_RUN(z_check_yaml(oa, "empty.yml"));
    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(iop_openapi);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
