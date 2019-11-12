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
    TYPE_REF,
    TYPE_ARRAY,
    TYPE_OBJECT,
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

typedef struct schema_object_t schema_object_t;
typedef struct schema_prop_t {
    lstr_t field_name;
    schema_object_t *schema;
} schema_prop_t;
qvector_t(schema_props, schema_prop_t);

struct schema_object_t {
    /* For TYPE_REF, this is the name of the referenced schema.
     * For TYPE_ARRAY, this is a reference name to the schema of the items.
     * For other types, this is the name of the schema.
     */
    lstr_t name;

    openapi_type_t type;

    /* for object */
    qv_t(lstr) required;
    qv_t(schema_props) properties;

    /* for enum */
    qv_t(lstr) enum_values;

    /* for number */
    opt_i64_t minimum;
    opt_i64_t maximum;
    bool _nullable;
};
qvector_t(schema_object, schema_object_t);
qh_kvec_t(schemas, lstr_t, qhash_lstr_hash, qhash_lstr_equal);

static schema_object_t * nonnull
add_schema_object(lstr_t name, openapi_type_t type,
                  qv_t(schema_object) * nonnull schemas)
{
    schema_object_t *obj = qv_growlen0(schemas, 1);

    obj->name = name;
    obj->type = type;

    return obj;
}

static void
t_iop_enum_to_schema_object(const iop_enum_t *en,
                            qh_t(schemas) *existing_schemas,
                            qv_t(schema_object) *schemas)
{
    schema_object_t *obj;

    if (qh_add(schemas, existing_schemas, &en->fullname) < 0) {
        return;
    }

    obj = add_schema_object(en->fullname, TYPE_STRING, schemas);
    t_qv_init(&obj->enum_values, en->enum_len);
    for (int i = 0; i < en->enum_len; i++) {
        qv_append(&obj->enum_values, en->names[i]);
    }
}

static void
t_iop_struct_to_schema_object(const iop_struct_t *st,
                              qh_t(schemas) *existing_schemas,
                              qv_t(schema_object) *schemas);

static schema_object_t *
t_iop_field_to_schema_object(const iop_field_t *desc,
                             qh_t(schemas) *existing_schemas,
                             qv_t(schema_object) *schemas)
{
    schema_object_t *schema;

    schema = t_new(schema_object_t, 1);
    schema->type = desc->repeat == IOP_R_REPEATED ? TYPE_ARRAY : TYPE_REF;

    switch (desc->type) {
      case IOP_T_I8:     schema->name = LSTR("iop:i8"); break;
      case IOP_T_U8:     schema->name = LSTR("iop:u8"); break;
      case IOP_T_I16:    schema->name = LSTR("iop:i16"); break;
      case IOP_T_U16:    schema->name = LSTR("iop:u16"); break;
      case IOP_T_I32:    schema->name = LSTR("iop:i32"); break;
      case IOP_T_U32:    schema->name = LSTR("iop:u32"); break;
      case IOP_T_I64:    schema->name = LSTR("iop:i64"); break;
      case IOP_T_U64:    schema->name = LSTR("iop:u64"); break;
      case IOP_T_BOOL:   schema->name = LSTR("iop:bool"); break;
      case IOP_T_DOUBLE: schema->name = LSTR("iop:double"); break;
      case IOP_T_VOID:   schema->name = LSTR("iop:void"); break;
      case IOP_T_DATA:   schema->name = LSTR("iop:byte"); break;
      case IOP_T_STRING: case IOP_T_XML:
        schema->name = LSTR("iop:string"); break;

      case IOP_T_ENUM:
        t_iop_enum_to_schema_object(desc->u1.en_desc, existing_schemas,
                                    schemas);
        schema->name = desc->u1.en_desc->fullname;
        break;

      case IOP_T_UNION:
      case IOP_T_STRUCT:
        t_iop_struct_to_schema_object(desc->u1.st_desc, existing_schemas,
                                      schemas);
        schema->name = desc->u1.st_desc->fullname;
        break;
    }

    return schema;
}

static bool field_is_required(const iop_field_t *desc)
{
    switch (desc->repeat) {
      case IOP_R_OPTIONAL:
      case IOP_R_DEFVAL:
        return false;
      case IOP_R_REPEATED:
        /* TODO: check minOccurs ? */
        return false;
      case IOP_R_REQUIRED:
        return desc->type != IOP_T_STRUCT
            || !iop_struct_is_optional(desc->u1.st_desc, true);
    }

    assert (false);
    return false;
}

static void
t_iop_struct_to_schema_object(const iop_struct_t *st,
                              qh_t(schemas) *existing_schemas,
                              qv_t(schema_object) *schemas)
{
    schema_object_t *obj;

    if (qh_add(schemas, existing_schemas, &st->fullname) < 0) {
        return;
    }

    if (iop_struct_is_class(st)) {
        /* TODO: handle class */
        return;
    }

    obj = add_schema_object(st->fullname, TYPE_OBJECT, schemas);
    t_qv_init(&obj->required, st->fields_len);
    t_qv_init(&obj->properties, st->fields_len);

    iop_struct_for_each_field(field_desc, field_st, st) {
        schema_prop_t *prop = qv_growlen0(&obj->properties, 1);

        prop->field_name = field_desc->name;
        prop->schema = t_iop_field_to_schema_object(field_desc,
                                                    existing_schemas,
                                                    schemas);

        if (field_is_required(field_desc)) {
            qv_append(&obj->required, prop->field_name);
        }
    }
}

static void
t_schema_object_to_yaml(const schema_object_t * nonnull obj,
                        yaml_data_t * nonnull out)
{
    static lstr_t types[] = {
        [TYPE_OBJECT] = LSTR_IMMED("object"),
        [TYPE_STRING] = LSTR_IMMED("string"),
        [TYPE_BYTE]   = LSTR_IMMED("string"),
        [TYPE_BOOL]   = LSTR_IMMED("bool"),
        [TYPE_DOUBLE] = LSTR_IMMED("number"),
        [TYPE_INT32]  = LSTR_IMMED("integer"),
        [TYPE_INT64]  = LSTR_IMMED("integer"),
        [TYPE_UINT32] = LSTR_IMMED("integer"),
        [TYPE_UINT64] = LSTR_IMMED("integer"),
    };
    static lstr_t formats[] = {
        [TYPE_OBJECT] = LSTR_NULL,
        [TYPE_STRING] = LSTR_NULL,
        [TYPE_BYTE]   = LSTR_IMMED("byte"),
        [TYPE_BOOL]   = LSTR_NULL,
        [TYPE_DOUBLE] = LSTR_IMMED("double"),
        [TYPE_INT32]  = LSTR_IMMED("int32"),
        [TYPE_INT64]  = LSTR_IMMED("int64"),
        [TYPE_UINT32] = LSTR_IMMED("uint32"),
        [TYPE_UINT64] = LSTR_IMMED("uint64"),
    };
    yaml_data_t data;

    t_yaml_data_new_obj(out, 2);

    if (obj->type == TYPE_REF) {
        yaml_data_set_string(&data, t_lstr_fmt("#/components/schemas/%pL",
                                               &obj->name));
        yaml_obj_add_field(out, LSTR("$ref"), data);
    } else
    if (obj->type == TYPE_ARRAY) {
        yaml_data_t items;

        yaml_data_set_string(&data, LSTR("array"));
        yaml_obj_add_field(out, LSTR("type"), data);

        t_yaml_data_new_obj(&items, 1);
        yaml_data_set_string(&data, t_lstr_fmt("#/components/schemas/%pL",
                                               &obj->name));
        yaml_obj_add_field(&items, LSTR("$ref"), data);
        yaml_obj_add_field(out, LSTR("items"), items);
    } else {
        if (types[obj->type].s) {
            yaml_data_set_string(&data, types[obj->type]);
            yaml_obj_add_field(out, LSTR("type"), data);
        }

        if (formats[obj->type].s) {
            yaml_data_set_string(&data, formats[obj->type]);
            yaml_obj_add_field(out, LSTR("format"), data);
        }
    }

    if (obj->required.len > 0) {
        t_yaml_data_new_seq(&data, obj->required.len);
        tab_for_each_entry(name, &obj->required) {
            yaml_data_t elem;

            yaml_data_set_string(&elem, name);
            yaml_seq_add_data(&data, elem);
        }
        yaml_obj_add_field(out, LSTR("required"), data);
    }

    if (obj->properties.len > 0) {
        t_yaml_data_new_obj(&data, obj->properties.len);
        tab_for_each_ptr(prop, &obj->properties) {
            yaml_data_t elem;

            t_schema_object_to_yaml(prop->schema, &elem);
            yaml_obj_add_field(&data, prop->field_name, elem);
        }
        yaml_obj_add_field(out, LSTR("properties"), data);
    }

    if (obj->enum_values.len > 0) {
        t_yaml_data_new_seq(&data, obj->enum_values.len);
        tab_for_each_entry(name, &obj->enum_values) {
            yaml_data_t elem;

            yaml_data_set_string(&elem, name);
            yaml_seq_add_data(&data, elem);
        }
        yaml_obj_add_field(out, LSTR("enum"), data);
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
    qh_t(schemas) existing_schemas;
} components_object_t;

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
    t_qh_init(schemas, &obj->existing_schemas, 0);
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

void t_iop_openapi_add_struct(iop_openapi_t *openapi, const iop_struct_t *st)
{
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
#include "iop/tstiop.iop.h"

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

static int
z_check_schemas(const qv_t(schema_object) *schemas, const char *filename)
{
    t_scope;
    const char *path;
    yaml_data_t data;
    lstr_t file;
    SB_1k(sb);

    t_yaml_data_new_obj(&data, schemas->len);
    tab_for_each_ptr(schema, schemas) {
        yaml_data_t elem;

        t_schema_object_to_yaml(schema, &elem);
        yaml_obj_add_field(&data, schema->name, elem);
    }

    yaml_pack_sb(&data, &sb);

    path = t_fmt("%pL/test-data/openapi/%s", &z_cmddir_g, filename);
    Z_ASSERT_N(lstr_init_from_file(&file, path, PROT_READ, MAP_SHARED));
    /* remove last newline from file */
    file.len--;

    Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), file);

    Z_HELPER_END;
}

/* }}} */

/* Unit-tests about YAML->AST parsing. */

Z_GROUP_EXPORT(iop_openapi)
{
    IOP_REGISTER_PACKAGES(&tstiop__pkg);
    MODULE_REQUIRE(iop_openapi);

    Z_TEST(doc, "test the whole doc generation") {
        t_scope;
        iop_openapi_t *oa;

        oa = t_new_iop_openapi(LSTR("zoomin"), LSTR("0.2.3"), LSTR("sheo"));
        Z_HELPER_RUN(z_check_yaml(oa, "empty.yml"));
    } Z_TEST_END;

    Z_TEST(iop_struct, "test the schema generation of IOP structs") {
        t_scope;
        qh_t(schemas) existing;
        qv_t(schema_object) schemas;

        t_qh_init(schemas, &existing, 0);
        t_qv_init(&schemas, 0);

        /* simple, no dependencies */
        t_iop_struct_to_schema_object(&tstiop__my_struct_n__s, &existing,
                                      &schemas);
        Z_HELPER_RUN(z_check_schemas(&schemas, "struct_n.yml"));

        qh_clear(schemas, &existing);
        qv_clear(&schemas);

        /* with dependencies on other structs */
        t_iop_struct_to_schema_object(&tstiop__my_struct_m__s, &existing,
                                      &schemas);
        Z_HELPER_RUN(z_check_schemas(&schemas, "struct_m.yml"));
        /* make sure the existing hash deduplicates already added elements */
        t_iop_struct_to_schema_object(&tstiop__my_struct_k__s, &existing,
                                      &schemas);
        Z_HELPER_RUN(z_check_schemas(&schemas, "struct_m.yml"));

        /* with enums */
        qh_clear(schemas, &existing);
        qv_clear(&schemas);
        t_iop_struct_to_schema_object(&tstiop__my_struct_l__s, &existing,
                                      &schemas);
        Z_HELPER_RUN(z_check_schemas(&schemas, "struct_l.yml"));
    } Z_TEST_END;

    MODULE_RELEASE(iop_openapi);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
