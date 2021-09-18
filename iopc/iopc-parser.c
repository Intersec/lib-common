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

#include "iopc-priv.h"
#include "iopctokens.h"

const char *__get_path(const char *file, bool display_prefix)
{
    static char res_path[PATH_MAX];

    if (*file == '/' || !display_prefix || !iopc_g.prefix_dir) {
        return file;
    }

    if (!expect(path_extend(res_path, iopc_g.prefix_dir, "%s", file) >= 0)) {
        return NULL;
    }

    return res_path;
}

typedef struct iopc_parser_t {
    qv_t(iopc_token) tokens;
    struct lexdata *ld;

    qv_t(cstr) *includes;
    const qm_t(iopc_env) *env;
    const char *base;
    iop_cfolder_t *cfolder;
} iopc_parser_t;

qm_kptr_t(enums, char, const iopc_enum_field_t *, qhash_str_hash,
          qhash_str_equal);

static struct {
    qm_t(iopc_pkg)  pkgs;
    qm_t(enums)     enums;
    qm_t(enums)     enums_forbidden;
    qm_t(attr_desc) attrs;
} iopc_parser_g;
#define _G  iopc_parser_g

/* reserved keywords in field names */
static const char * const reserved_keywords[] = {
    /* C keywords */
    "auto", "bool", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while",
    /* Java and C++ keywords */
    "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
    "const", "continue", "default", "do", "double", "else", "enum", "extends",
    "false", "final", "finally", "float", "for", "friend", "goto", "if",
    "implements", "import", "instanceof", "int", "interface", "long",
    "mutable", "namespace", "native", "null", "operator", "package",
    "private", "protected", "public", "return", "short", "static", "strictfp",
    "super", "switch", "synchronized", "template", "this", "throw", "throws",
    "transient", "true", "try", "typename", "virtual", "void", "volatile",
    "while",
    /* Language keywords */
    "in", "null", "out", "throw", "interface", "module", "package",
};

static const char * const avoid_keywords[] = {
    /* sadly already in use */
    "class", "new", "delete", "explicit",
};

static int parse_json_object(iopc_parser_t *pp, sb_t *sb, bool toplevel);

static bool warn(qv_t(iopc_attr) *nullable attrs, const char *category)
{
    lstr_t s = LSTR(category);

    if (!attrs)
        return true;

    tab_for_each_entry(attr, attrs) {
        if (attr->desc->id != IOPC_ATTR_NOWARN) {
            continue;
        }

        tab_for_each_ptr(arg, &attr->args) {
            if (lstr_equal(arg->v.s, s)) {
                return false;
            }
        }
    }
    return true;
}

int iopc_check_name(lstr_t name, qv_t(iopc_attr) *nullable attrs,
                    sb_t *nonnull err)
{
    if (!name.len) {
        sb_sets(err, "empty name");
        return -1;
    }

    if (memchr(name.s, '_', name.len)) {
        sb_setf(err, "%pL contains a _", &name);
        return -1;
    }
    for (int i = 0; i < countof(reserved_keywords); i++) {
        if (lstr_equal(name, LSTR(reserved_keywords[i]))) {
            sb_setf(err, "%pL is a reserved keyword", &name);
            return -1;
        }
    }
    if (warn(attrs, "keyword")) {
        for (int i = 0; i < countof(avoid_keywords); i++) {
            if (lstr_equal(name, LSTR(avoid_keywords[i]))) {
                sb_setf(err, "%pL is a keyword in some languages", &name);
                return -1;
            }
        }
    }

    return 0;
}

static int check_name(const char *name, iopc_loc_t loc,
                      qv_t(iopc_attr) *attrs)
{
    SB_1k(err);

    if (iopc_check_name(LSTR(name), attrs, &err) < 0) {
        throw_loc("%*pM", loc, SB_FMT_ARG(&err));
    }

    return 0;
}

static iopc_pkg_t *
iopc_try_file(iopc_parser_t *pp, const char *dir, iopc_path_t *path)
{
    struct stat st;
    char file[PATH_MAX];
    const char *pkg_name = pretty_path_dot(path);

    snprintf(file, sizeof(file), "%s/%s", dir, pretty_path(path));
    path_simplify(file);

    if (pp->env) {
        const char *data;

        data = qm_get_def_safe(iopc_env, pp->env, pkg_name, NULL);
        if (data) {
            return iopc_parse_file(pp->includes, pp->env, file, data, false);
        }
    }

    if (stat(file, &st) == 0 && S_ISREG(st.st_mode)) {
        return iopc_parse_file(pp->includes, pp->env, file, NULL, false);
    }

    return NULL;
}

/* ----- attributes {{{*/

static const char *type_to_str(unsigned type)
{
    switch (type) {
      case IOPC_ATTR_T_INT:     return "integer";
      case IOPC_ATTR_T_BOOL:    return "boolean";
      case IOPC_ATTR_T_ENUM:    return "enum";
      case IOPC_ATTR_T_DOUBLE:  return "double";
      case IOPC_ATTR_T_STRING:  return "string";
      case IOPC_ATTR_T_DATA:    return "data";
      case IOPC_ATTR_T_UNION:   return "union";
      case IOPC_ATTR_T_STRUCT:  return "struct";
      case IOPC_ATTR_T_XML:     return "xml";
      case IOPC_ATTR_T_RPC:     return "rpc";
      case IOPC_ATTR_T_IFACE:   return "interface";
      case IOPC_ATTR_T_MOD:     return "module";
      case IOPC_ATTR_T_SNMP_IFACE:  return "snmpIface";
      case IOPC_ATTR_T_SNMP_OBJ:    return "snmpObj";
      case IOPC_ATTR_T_SNMP_TBL:    return "snmpTbl";

      case IOPC_ATTR_T_CLASS:
      case IOPC_ATTR_T_CLASS | IOPC_ATTR_T_STRUCT:
        return "class";

      default:
        print_error("invalid type %d", type);
        return NULL;
    }
}

static int check_attr_type_decl(iopc_attr_t *attr, iopc_attr_type_t type)
{
    if (!(attr->desc->flags & IOPC_ATTR_F_DECL)) {
        throw_loc("attribute %*pM does not apply to declarations",
                  attr->loc, LSTR_FMT_ARG(attr->desc->name));
    }

    if (!(attr->desc->types & type)) {
        throw_loc("attribute %*pM does not apply to %s",
                  attr->loc, LSTR_FMT_ARG(attr->desc->name),
                  type_to_str(type));
    }

    switch (attr->desc->id) {
      case IOPC_ATTR_PRIVATE:
        if (!(type & IOPC_ATTR_T_CLASS)) {
            throw_loc("attribute %*pM does not apply to %s",
                      attr->loc, LSTR_FMT_ARG(attr->desc->name),
                      type_to_str(type));
        }
        break;

      default:
        break;
    }

    return 0;
}

static int check_attr_type_field(iopc_attr_t *attr, iopc_field_t *f,
                                 bool tdef)
{
    const char *tstr = tdef ? "typedefs" : "fields";
    iopc_attr_type_t type;

    if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_ALL)) {
        throw_loc("attribute %*pM does not apply to %s", attr->loc,
                  LSTR_FMT_ARG(attr->desc->name), tstr);
    }

    switch (f->kind) {
      case IOP_T_DATA:      type = IOPC_ATTR_T_DATA; break;
      case IOP_T_DOUBLE:    type = IOPC_ATTR_T_DOUBLE; break;
      case IOP_T_STRING:    type = IOPC_ATTR_T_STRING; break;
      case IOP_T_XML:       type = IOPC_ATTR_T_XML; break;
      case IOP_T_STRUCT:    type = IOPC_ATTR_T_STRUCT; break;
      case IOP_T_UNION:     type = IOPC_ATTR_T_UNION; break;
      case IOP_T_ENUM:      type = IOPC_ATTR_T_ENUM; break;
      case IOP_T_BOOL:      type = IOPC_ATTR_T_BOOL; break;
      default:              type = IOPC_ATTR_T_INT; break;
    }

    if (f->kind == IOP_T_STRUCT && !f->struct_def) {
        /* struct or union or enum -> the typer will know the real type and
         * will check this attribute in iopc_check_field_attributes */
        return 0;
    }

    switch (f->repeat) {
      case IOP_R_REQUIRED:
        if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_REQUIRED)) {
            throw_loc("attribute %*pM does not apply to required %s",
                      attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
        }
        break;

      case IOP_R_DEFVAL:
        if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_DEFVAL)) {
            throw_loc("attribute %*pM does not apply to %s "
                      "with default value", attr->loc,
                      LSTR_FMT_ARG(attr->desc->name), tstr);
        }
        break;

      case IOP_R_OPTIONAL:
        if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_OPTIONAL)) {
            throw_loc("attribute %*pM does not apply to optional %s",
                      attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
        }
        break;

      case IOP_R_REPEATED:
        if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_REPEATED)) {
            throw_loc("attribute %*pM does not apply to repeated %s",
                      attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
        }
        break;
    }

    if (!(attr->desc->types & type)) {
        throw_loc("attribute %*pM does not apply to %s",
                  attr->loc,
                  LSTR_FMT_ARG(attr->desc->name),
                  type_to_str(type));
    }

    /* Field snmp specific checks */
    if (attr->desc->id == IOPC_ATTR_SNMP_INDEX && !f->snmp_is_in_tbl) {
        throw_loc("field '%s' does not support @snmpIndex attribute",
                  attr->loc, f->name);
    }

    return 0;
}

int iopc_check_field_attributes(iopc_field_t *f, bool tdef)
{
    const char *tstr = tdef ? "typedefs" : "fields";
    iopc_attr_type_t type;
    unsigned flags = 0;

    switch (f->kind) {
      case IOP_T_DATA:      type = IOPC_ATTR_T_DATA; break;
      case IOP_T_DOUBLE:    type = IOPC_ATTR_T_DOUBLE; break;
      case IOP_T_STRING:    type = IOPC_ATTR_T_STRING; break;
      case IOP_T_XML:       type = IOPC_ATTR_T_XML; break;
      case IOP_T_BOOL:      type = IOPC_ATTR_T_BOOL; break;
      case IOP_T_STRUCT:    type = IOPC_ATTR_T_STRUCT; break;
      case IOP_T_UNION:     type = IOPC_ATTR_T_UNION; break;
      case IOP_T_ENUM:      type = IOPC_ATTR_T_ENUM; break;
      default:              type = IOPC_ATTR_T_INT; break;
    }

    tab_for_each_entry(attr, &f->attrs) {
        if (!(attr->desc->types & type)) {
            throw_loc("attribute %*pM does not apply to %s",
                      attr->loc,
                      LSTR_FMT_ARG(attr->desc->name),
                      type_to_str(type));
        }

        switch (f->repeat) {
          case IOP_R_REQUIRED:
            if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_REQUIRED)) {
                throw_loc("attribute %*pM does not apply to required %s",
                          attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
            }
            break;

          case IOP_R_DEFVAL:
            if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_DEFVAL)) {
                throw_loc("attribute %*pM does not apply to %s "
                          "with default value",
                          attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
            }
            break;

          case IOP_R_OPTIONAL:
            if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_OPTIONAL)) {
                throw_loc("attribute %*pM does not apply to optional %s",
                          attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
            }
            break;

          case IOP_R_REPEATED:
            if (!(attr->desc->flags & IOPC_ATTR_F_FIELD_REPEATED)) {
                throw_loc("attribute %*pM does not apply to repeated %s",
                          attr->loc, LSTR_FMT_ARG(attr->desc->name), tstr);
            }
            break;

          default:
            throw_loc("unknown repeat kind for field `%s`", attr->loc, f->name);
        }

        /* Field specific checks */
        switch (attr->desc->id) {
          case IOPC_ATTR_ALLOW:
          case IOPC_ATTR_DISALLOW:
            SET_BIT(&flags, attr->desc->id);
            if (TST_BIT(&flags, IOPC_ATTR_ALLOW)
            &&  TST_BIT(&flags, IOPC_ATTR_DISALLOW))
            {
                throw_loc("cannot use both @allow and @disallow on the same "
                          "field", attr->loc);
            }

            tab_for_each_ptr(arg, &attr->args) {
                bool found = false;

                if (type == IOPC_ATTR_T_UNION) {
                    tab_for_each_entry(uf, &f->struct_def->fields) {
                        if (strequal(uf->name, arg->v.s.s)) {
                            found = true;
                            break;
                        }
                    }
                } else
                if (type == IOPC_ATTR_T_ENUM) {
                    tab_for_each_entry(ef, &f->enum_def->values) {
                        if (strequal(ef->name, arg->v.s.s)) {
                            found = true;
                            break;
                        }
                    }
                }

                if (!found) {
                    throw_loc("unknown field %*pM in %s",
                              attr->loc, LSTR_FMT_ARG(arg->v.s),
                              f->type_name);
                }
            }
            break;

          default:
            break;
        }
    }

    return 0;
}

static iopc_attr_desc_t *add_attr(iopc_attr_id_t id, const char *name)
{
    iopc_attr_desc_t d;
    int pos;

    iopc_attr_desc_init(&d);
    d.id    = id;
    d.name  = LSTR(name);
    pos = qm_put(attr_desc, &_G.attrs, &d.name, d, 0);

    if (pos & QHASH_COLLISION) {
        print_error("attribute %s already exists", name);
        assert (false);
    }
    return &_G.attrs.values[pos];
}

static void init_attributes(void)
{
    iopc_attr_desc_t *d;

#define ADD_ATTR_ARG(_d, _s, _tok) \
    ({  \
        iopc_arg_desc_t arg;                                    \
        iopc_arg_desc_init(&arg);                               \
        arg.name = LSTR(_s);                                    \
        arg.type = _tok;                                        \
        qv_append(&_d->args, arg);               \
    })

    d = add_attr(IOPC_ATTR_CTYPE, "ctype");
    d->flags |= IOPC_ATTR_F_DECL;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_STRUCT;
    d->types |= IOPC_ATTR_T_UNION;
    d->types |= IOPC_ATTR_T_ENUM;
    ADD_ATTR_ARG(d, "type", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_NOWARN, "nowarn");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_DECL;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_ALL;
    ADD_ATTR_ARG(d, "value", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_PREFIX, "prefix");
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_ENUM;
    ADD_ATTR_ARG(d, "name", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_STRICT, "strict");
    d->flags |= IOPC_ATTR_F_DECL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_ENUM;

    d = add_attr(IOPC_ATTR_MIN, "min");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_INT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_DOUBLE;
    ADD_ATTR_ARG(d, "value", ITOK_DOUBLE);

    d = add_attr(IOPC_ATTR_MAX, "max");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_INT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_DOUBLE;
    ADD_ATTR_ARG(d, "value", ITOK_DOUBLE);

    d = add_attr(IOPC_ATTR_MIN_LENGTH, "minLength");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_STRING;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_DATA;
    ADD_ATTR_ARG(d, "value", ITOK_INTEGER);

    d = add_attr(IOPC_ATTR_MAX_LENGTH, "maxLength");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_STRING;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_DATA;
    ADD_ATTR_ARG(d, "value", ITOK_INTEGER);

    d = add_attr(IOPC_ATTR_LENGTH, "length");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_STRING;
    d->types |= IOPC_ATTR_T_DATA;
    ADD_ATTR_ARG(d, "value", ITOK_INTEGER);

    d = add_attr(IOPC_ATTR_MIN_OCCURS, "minOccurs");
    d->flags |= IOPC_ATTR_F_FIELD_REPEATED;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_ALL;
    ADD_ATTR_ARG(d, "value", ITOK_INTEGER);

    d = add_attr(IOPC_ATTR_MAX_OCCURS, "maxOccurs");
    d->flags |= IOPC_ATTR_F_FIELD_REPEATED;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_ALL;
    ADD_ATTR_ARG(d, "value", ITOK_INTEGER);

    d = add_attr(IOPC_ATTR_CDATA, "cdata");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->types |= IOPC_ATTR_T_STRING;

    d = add_attr(IOPC_ATTR_NON_EMPTY, "nonEmpty");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_STRING;
    d->types |= IOPC_ATTR_T_DATA;
    d->types |= IOPC_ATTR_T_XML;

    d = add_attr(IOPC_ATTR_NON_ZERO, "nonZero");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_INT;
    d->types |= IOPC_ATTR_T_DOUBLE;

    d = add_attr(IOPC_ATTR_PATTERN, "pattern");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->types |= IOPC_ATTR_T_STRING;
    ADD_ATTR_ARG(d, "value", ITOK_STRING);

    d = add_attr(IOPC_ATTR_PRIVATE, "private");
    d->flags |= IOPC_ATTR_F_FIELD_ALL_BUT_REQUIRED;
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_ALL;

    d = add_attr(IOPC_ATTR_ALIAS, "alias");
    d->flags |= IOPC_ATTR_F_DECL;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_RPC;
    ADD_ATTR_ARG(d, "name", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_NO_REORDER, "noReorder");
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_STRUCT;

    d = add_attr(IOPC_ATTR_ALLOW, "allow");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_UNION;
    d->types |= IOPC_ATTR_T_ENUM;
    ADD_ATTR_ARG(d, "field", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_DISALLOW, "disallow");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_CONSTRAINT;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->types |= IOPC_ATTR_T_UNION;
    d->types |= IOPC_ATTR_T_ENUM;
    ADD_ATTR_ARG(d, "field", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_GENERIC, "generic");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->flags |= IOPC_ATTR_F_MULTI;
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_ALL;
    ADD_ATTR_ARG(d, "", ITOK_STRING);

    d = add_attr(IOPC_ATTR_DEPRECATED, "deprecated");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->types |= IOPC_ATTR_T_ALL;
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_SNMP_IFACE;
    d->types |= IOPC_ATTR_T_SNMP_OBJ;
    d->types |= IOPC_ATTR_T_SNMP_TBL;

    d = add_attr(IOPC_ATTR_SNMP_PARAMS_FROM, "snmpParamsFrom");
    d->flags |= IOPC_ATTR_F_MULTI;
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_SNMP_IFACE;
    ADD_ATTR_ARG(d, "param", ITOK_IDENT);

    d = add_attr(IOPC_ATTR_SNMP_PARAM, "snmpParam");
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_SNMP_OBJ;

    d = add_attr(IOPC_ATTR_SNMP_INDEX, "snmpIndex");
    d->flags |= IOPC_ATTR_F_FIELD_ALL;
    d->types |= IOPC_ATTR_T_ALL;

    d = add_attr(IOPC_ATTR_TS_NO_COLL, "typescriptNoCollection");
    d->flags |= IOPC_ATTR_F_DECL;
    d->types |= IOPC_ATTR_T_STRUCT;
    d->types |= IOPC_ATTR_T_UNION;
#undef ADD_ATTR_ARG
}

static int
check_attr_multi(qv_t(iopc_attr) *attrs, iopc_attr_t *attr, int *pos_out)
{
    tab_for_each_pos(pos, attrs) {
        iopc_attr_t *a = attrs->tab[pos];

        if (a->desc == attr->desc) {
            /* Generic attributes share the same desc */
            if (a->desc->id == IOPC_ATTR_GENERIC) {
                if (lstr_equal(a->real_name, attr->real_name)) {
                    throw_loc("generic attribute '%*pM' must be unique for "
                              "each IOP object", attr->loc,
                              LSTR_FMT_ARG(attr->real_name));
                }
                *pos_out = -1;
                return 0;
            }

            if ((attr->desc->flags & IOPC_ATTR_F_MULTI)) {
                *pos_out = pos;
                return 0;
            } else {
                throw_loc("attribute %*pM must be unique", attr->loc,
                          LSTR_FMT_ARG(attr->desc->name));
            }
        }
    }

    *pos_out = -1;
    return 0;
}

int
iopc_attr_check(const qv_t(iopc_attr) *attrs, iopc_attr_id_t attr_id,
                const qv_t(iopc_arg) **out)
{
    tab_for_each_entry(e, attrs) {
        if (e->desc->id == attr_id) {
            if (out) {
                *out = &e->args;
            }
            return 0;
        }
    }
    return -1;
}

int t_iopc_attr_check_prefix(const qv_t(iopc_attr) *attrs, lstr_t *out)
{
    const qv_t(iopc_arg) *args = NULL;

    RETHROW(iopc_attr_check(attrs, IOPC_ATTR_PREFIX, &args));
    *out = t_lstr_dup(args->tab[0].v.s);
    return 0;
}

/*}}}*/
/*----- helpers -{{{-*/

static int __tk(iopc_parser_t *pp, int i, iopc_token_t **out_tk)
{
    qv_t(iopc_token) *tks = &pp->tokens;

    while (i >= tks->len) {
        iopc_token_t *tk;

        RETHROW(iopc_next_token(pp->ld, false, &tk));
        if (!tk) {
            assert (tks->len && tks->tab[tks->len - 1]->token == ITOK_EOF);
            tk = iopc_token_dup(tks->tab[tks->len - 1]);
        }
        qv_append(tks, tk);
    }
    *out_tk = tks->tab[i];
    return 0;
}
#define TK(pp, i, on_error)  ({  \
    iopc_token_t *_res;                                                      \
                                                                             \
    if (__tk(pp, i, &_res) < 0) {                                            \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define TK_N(pp, i)  TK(pp, i, return -1)
#define TK_P(pp, i)  TK(pp, i, return NULL)

static void DROP(iopc_parser_t *pp, int len, int offset)
{
    qv_t(iopc_token) *tks = &pp->tokens;

    assert (offset < tks->len && len <= tks->len);
    for (int i = 0; i < len; i++) {
        iopc_token_delete(tks->tab + offset + i);
    }
    qv_splice(tks, offset, len, NULL, 0);
}
#define DROP(_pp, _len)  ((DROP)(_pp, _len, 0))

static int __check(iopc_parser_t *pp, int i, iopc_tok_type_t token, bool *res)
{
    iopc_token_t *tk = TK_N(pp, i);

    *res = tk->token == token;
    return 0;
}
#define CHECK(pp, i, token, on_error)  ({  \
    bool _res;                                                               \
                                                                             \
    if (__check(pp, i, token, &_res) < 0) {                                  \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define CHECK_N(pp, i, token)  CHECK(pp, i, token, return -1)
#define CHECK_P(pp, i, token)  CHECK(pp, i, token, return NULL)

static int
__check_noeof(iopc_parser_t *pp, int i, iopc_tok_type_t token, bool *res)
{
    iopc_token_t *tk = TK_N(pp, i);

    if (tk->token == ITOK_EOF) {
        throw_loc("unexpected end of file", tk->loc);
    }
    *res = tk->token == token;
    return 0;
}
#define CHECK_NOEOF(pp, i, token, on_error)  ({  \
    bool _res;                                                               \
                                                                             \
    if (__check_noeof(pp, i, token, &_res) < 0) {                            \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define CHECK_NOEOF_N(pp, i, token)  CHECK_NOEOF(pp, i, token, return -1)

static int __check_kw(iopc_parser_t *pp, int i, const char *kw, bool *res)
{
    iopc_token_t *tk = TK_N(pp, i);

    *res = tk->token == ITOK_IDENT && strequal(tk->b.data, kw);
    return 0;
}
#define CHECK_KW(pp, i, kw, on_error)  ({  \
    bool _res;                                                               \
                                                                             \
    if (__check_kw(pp, i, kw, &_res) < 0) {                                  \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define CHECK_KW_N(pp, i, kw)  CHECK_KW(pp, i, kw, return -1)

static int __want(iopc_parser_t *pp, int i, iopc_tok_type_t token)
{
    iopc_token_t *tk = TK_N(pp, i);

    if (tk->token != token) {
        t_scope;

        if (tk->token == ITOK_EOF) {
            throw_loc("unexpected end of file", tk->loc);
        }
        throw_loc("%s expected, but got %s instead",
                  tk->loc, t_pretty_token(token), t_pretty_token(tk->token));
    }

    return 0;
}
#define WANT(pp, i, token)    RETHROW(__want(pp, i, token))
#define WANT_P(pp, i, token)  RETHROW_NP(__want(pp, i, token))

static int __skip(iopc_parser_t *pp, iopc_tok_type_t token, bool *res)
{
    if (CHECK_N(pp, 0, token)) {
        DROP(pp, 1);
        *res = true;
    } else {
        *res = false;
    }
    return 0;
}
#define SKIP(pp, token, on_error)  ({  \
    bool _res;                                                               \
                                                                             \
    if (__skip(pp, token, &_res) < 0) {                                      \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define SKIP_N(pp, token)  SKIP(pp, token, return -1)

static int __skip_kw(iopc_parser_t *pp, const char *kw, bool *res)
{
    if (CHECK_KW_N(pp, 0, kw)) {
        DROP(pp, 1);
        *res = true;
    } else {
        *res = false;
    }
    return 0;
}
#define SKIP_KW(pp, kw, on_error)  ({  \
    bool _res;                                                               \
                                                                             \
    if (__skip_kw(pp, kw, &_res) < 0) {                                      \
        on_error;                                                            \
        assert (false);                                                      \
    }                                                                        \
    _res;                                                                    \
})
#define SKIP_KW_N(pp, kw)  SKIP_KW(pp, kw, return -1)

static int __eat(iopc_parser_t *pp, iopc_tok_type_t token)
{
    WANT(pp, 0, token);
    DROP(pp, 1);
    return 0;
}
#define EAT(pp, token)    RETHROW(__eat(pp, token))
#define EAT_P(pp, token)  RETHROW_NP(__eat(pp, token))

static int __eat_kw(iopc_parser_t *pp, const char *kw)
{
    iopc_token_t *tk = TK_N(pp, 0);

    if (!SKIP_KW_N(pp, kw)) {
        WANT(pp, 0, ITOK_IDENT);
        throw_loc("%s expected, but got %s instead",
                  tk->loc, kw, tk->b.data);
    }
    return 0;
}
#define EAT_KW(pp, token)    RETHROW(__eat_kw(pp, token))
#define EAT_KW_P(pp, token)  RETHROW_NP(__eat_kw(pp, token))

static inline char *dup_ident(iopc_token_t *tk)
{
    return p_dup(tk->b.data, tk->b.len + 1);
}
static inline const char *ident(iopc_token_t *tk)
{
    return tk->b.data;
}

static int
parse_constant_integer(iopc_parser_t *pp, int paren, uint64_t *res,
                       bool *is_signed)
{
    uint64_t num = 0;
    int pos = 0;
    iopc_token_t *tk;
    int nparen = 1;

    for (;;) {
        tk = TK_N(pp, pos++);

        switch (tk->token) {
          case '-': case '+': case '*': case '/': case '~':
          case '&': case '|': case '%': case '^': case '(':
            if (tk->token == '(') {
                nparen++;
            }
            if (iop_cfolder_feed_operator(pp->cfolder,
                                          (iop_cfolder_op_t)tk->token) < 0)
            {
                throw_loc("error when feeding the constant folder with `%c'",
                          tk->loc, tk->token);
            }
            break;

          case ')':
            nparen--;
            /* If we are in a function or in an attribute, check if it is the
             * end paren */
            if (paren == ')' && nparen == 0) {
                goto end;
            }

            if (iop_cfolder_feed_operator(pp->cfolder,
                                          (iop_cfolder_op_t)tk->token) < 0)
            {
                throw_loc("error when feeding the constant folder with `%c'",
                          tk->loc, tk->token);
            }
            break;

          case ITOK_LSHIFT:
            if (iop_cfolder_feed_operator(pp->cfolder, CF_OP_LSHIFT) < 0) {
                throw_loc("error when feeding the constant folder with `<<'",
                          tk->loc);
            }
            break;

          case ITOK_RSHIFT:
            if (iop_cfolder_feed_operator(pp->cfolder, CF_OP_RSHIFT) < 0) {
                throw_loc("error when feeding the constant folder with `>>'",
                          tk->loc);
            }
            break;

          case ITOK_EXP:
            if (iop_cfolder_feed_operator(pp->cfolder, CF_OP_EXP) < 0) {
                throw_loc("error when feeding the constant folder with `**'",
                          tk->loc);
            }
            break;

          case ITOK_INTEGER:
          case ITOK_BOOL:
            if (iop_cfolder_feed_number(pp->cfolder, tk->i,
                                        tk->i_is_signed) < 0)
            {
                if (tk->i_is_signed) {
                    throw_loc("error when feeding the constant folder with "
                              "`%jd'", tk->loc, tk->i);
                } else {
                    throw_loc("error when feeding the constant folder with "
                              "`%ju'", tk->loc, tk->i);
                }
            }
            break;

          case ITOK_IDENT:
            /* check for enum value or stop */
            {
                const iopc_enum_field_t *f;
                int qpos = qm_find(enums, &_G.enums, ident(tk));

                if (qpos < 0) {
                    /* XXX compatibility code which will be removed soon */
                    if ((qpos = qm_find(enums, &_G.enums_forbidden,
                                        ident(tk))) >= 0)
                    {
                        f = _G.enums_forbidden.values[qpos];
                        goto compatibility;
                    }
                    throw_loc("unknown enumeration value `%s'", tk->loc,
                              ident(tk));
                }

                if (qm_find(enums, &_G.enums_forbidden, ident(tk)) >= 0) {
                    warn_loc("enum field identifier `%s` is ambiguous",
                             tk->loc, ident(tk));
                }

                f = _G.enums.values[qpos];

              compatibility:
                /* feed the enum value */
                if (iop_cfolder_feed_number(pp->cfolder, f->value, true) < 0)
                {
                    throw_loc("error when feeding the constant folder with `%d'",
                              tk->loc, f->value);
                }
            }
            break;

          default:
            goto end;
        }
    }

  end:
    /* Let's try to get a result */
    if (iop_cfolder_get_result(pp->cfolder, &num, is_signed) < 0) {
        throw_loc("invalid arithmetic expression", TK_N(pp, 0)->loc);
    }
    DROP(pp, pos - 1);

    *res = num;
    return 0;
}

/*-}}}-*/
/*----- doxygen -{{{-*/

#ifdef NDEBUG
  #define debug_dump_dox(X, Y)
#else
static void debug_dump_dox(qv_t(iopc_dox) comments, const char *name)
{
#define DEBUG_LVL  3

    if (!comments.len)
        return;

    e_trace(DEBUG_LVL, "BUILT DOX COMMENTS for %s", name);

    tab_for_each_ptr(dox, &comments) {
        lstr_t type = iopc_dox_type_to_lstr(dox->type);

        e_trace(DEBUG_LVL, "type: %*pM", LSTR_FMT_ARG(type));
        e_trace(DEBUG_LVL, "desc: %*pM", LSTR_FMT_ARG(dox->desc));
        e_trace(DEBUG_LVL, "----------------------------------------");
    }
    e_trace(DEBUG_LVL, "****************************************");

#undef DEBUG_LVL
}
  #define debug_dump_dox(_c, _n)  ((debug_dump_dox)((_c),           \
      __builtin_types_compatible_p(typeof(_n), iopc_path_t *)       \
      ? pretty_path_dot((iopc_path_t *)(_n)) : (const char *)(_n)))
#endif

typedef enum iopc_dox_arg_dir_t {
    IOPC_DOX_ARG_DIR_IN,
    IOPC_DOX_ARG_DIR_OUT,
    IOPC_DOX_ARG_DIR_THROW,

    IOPC_DOX_ARG_DIR_count,
} iopc_dox_arg_dir_t;

static lstr_t iopc_dox_arg_dir_to_lstr(iopc_dox_arg_dir_t dir)
{
    switch (dir) {
      case IOPC_DOX_ARG_DIR_IN:    return LSTR("in");
      case IOPC_DOX_ARG_DIR_OUT:   return LSTR("out");
      case IOPC_DOX_ARG_DIR_THROW: return LSTR("throw");

      default:
        print_error("invalid doxygen arg dir %d", dir);
        return LSTR_NULL_V;
    }
}

static int iopc_dox_check_param_dir(lstr_t dir_name, iopc_dox_arg_dir_t *out)
{
    for (int i = 0; i < IOPC_DOX_ARG_DIR_count; i++) {
        if (lstr_equal(dir_name, iopc_dox_arg_dir_to_lstr(i))) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

lstr_t iopc_dox_type_to_lstr(iopc_dox_type_t type)
{
    switch (type) {
      case IOPC_DOX_TYPE_BRIEF:   return LSTR("brief");
      case IOPC_DOX_TYPE_DETAILS: return LSTR("details");
      case IOPC_DOX_TYPE_WARNING: return LSTR("warning");
      case IOPC_DOX_TYPE_EXAMPLE: return LSTR("example");
      case IOPC_DOX_TYPE_PARAM:   return LSTR("param");

      default:
        print_error("invalid doxygen type %d", type);
        return LSTR_NULL_V;
    }
}

static int iopc_dox_check_keyword(lstr_t keyword, int * nullable type)
{
    for (int i = 0; i < IOPC_DOX_TYPE_count; i++) {
        if (lstr_equal(keyword, iopc_dox_type_to_lstr(i))) {
            if (type) {
                *type = i;
            }
            return 0;
        }
    }
    return -1;
}

iopc_dox_t *
iopc_dox_find_type(const qv_t(iopc_dox) *comments, iopc_dox_type_t type)
{
    tab_for_each_ptr(p, comments) {
        if (p->type == type)
            return p;
    }
    return NULL;
}

static iopc_dox_t *
iopc_dox_add(qv_t(iopc_dox) *comments, iopc_dox_type_t type)
{
    iopc_dox_t *res = iopc_dox_init(qv_growlen(comments, 1));
    res->type = type;
    return res;
}

static iopc_dox_t *
iopc_dox_find_type_or_create(qv_t(iopc_dox) *comments, iopc_dox_type_t type)
{
    iopc_dox_t *dox = iopc_dox_find_type(comments, type);

    if (!dox) {
        dox = iopc_dox_add(comments, type);
    }
    return dox;
}

static bool
iopc_dox_type_is_related(iopc_dox_type_t dox_type, iopc_attr_type_t attr_type)
{
    return dox_type != IOPC_DOX_TYPE_PARAM || attr_type == IOPC_ATTR_T_RPC;
}

static iopc_field_t *
iopc_dox_arg_find_in_fun(lstr_t name, iopc_dox_arg_dir_t dir,
                         const iopc_fun_t* fun)
{
    switch(dir) {
#define CASE_DIR(X, Y)                                                       \
      case IOPC_DOX_ARG_DIR_##X:                                             \
        if (!fun->Y##_is_anonymous) {                                        \
            if (name.len)                                                    \
                return NULL;                                                 \
            return fun->f##Y;                                                \
        }                                                                    \
        tab_for_each_entry(f, &fun->Y->fields) {                  \
            if (lstr_equal(name, LSTR(f->name)))                             \
                return f;                                                    \
        }                                                                    \
        return NULL;

      CASE_DIR(IN, arg);
      CASE_DIR(OUT, res);
      CASE_DIR(THROW, exn);

      default: return NULL;

#undef CASE_DIR
    }
}

static void dox_chunk_params_args_validate(dox_chunk_t *chunk)
{
    if (chunk->params_args.len == 1 && chunk->paragraphs.len) {
        sb_skip(&chunk->paragraphs.tab[0], chunk->paragraph0_args_len);
        sb_ltrim(&chunk->paragraphs.tab[0]);
        chunk->paragraph0_args_len = 0;
    }
}

static void dox_chunk_autobrief_validate(dox_chunk_t *chunk)
{
    if (chunk->first_sentence_len
    &&  isspace(chunk->paragraphs.tab[0].data[chunk->first_sentence_len]))
    {
        pstream_t tmp = ps_initsb(&chunk->paragraphs.tab[0]);
        sb_t paragraph0_end;

        ps_skip(&tmp, chunk->first_sentence_len);
        ps_ltrim(&tmp);

        sb_init(&paragraph0_end);
        sb_add_ps(&paragraph0_end, tmp);

        sb_clip(&chunk->paragraphs.tab[0], chunk->first_sentence_len);

        qv_insert(&chunk->paragraphs, 1, paragraph0_end);
        chunk->first_sentence_len = 0;
    }
}

static void dox_chunk_push_sb(dox_chunk_t *chunk, sb_t sb)
{
    if (chunk->paragraphs.len) {
        if (chunk->paragraphs.tab[0].len)
            sb_addc(&sb, ' ');
        sb_addsb(&sb, &chunk->paragraphs.tab[0]);
        sb_wipe(&chunk->paragraphs.tab[0]);
        chunk->paragraphs.tab[0] = sb;
    } else {
        qv_append(&chunk->paragraphs, sb);
    }
}

static void dox_chunk_keyword_merge(dox_chunk_t *chunk)
{
    sb_t sb;

    if (!chunk->keyword.len)
        return;

    sb_init(&sb);
    sb_addc(&sb, '\\');
    sb_add_lstr(&sb, chunk->keyword);
    lstr_wipe(&chunk->keyword);
    dox_chunk_push_sb(chunk, sb);
}

static void dox_chunk_params_merge(dox_chunk_t *chunk)
{
    sb_t sb;

    if (!chunk->params.len) {
        qv_deep_clear(&chunk->params_args, lstr_wipe);
        return;
    }

    sb_init(&sb);

    sb_addc(&sb, '[');
    tab_for_each_ptr(s, &chunk->params) {
        sb_add_lstr(&sb, *s);
        if (s != tab_last(&chunk->params)) {
            sb_adds(&sb, ", ");
        }
    }
    sb_addc(&sb, ']');
    dox_chunk_push_sb(chunk, sb);

    qv_deep_wipe(&chunk->params, lstr_wipe);
    qv_deep_wipe(&chunk->params_args, lstr_wipe);
    chunk->paragraph0_args_len = 0;
}

static void dox_chunk_merge(dox_chunk_t *eating, dox_chunk_t *eaten)
{
    if (eaten->keyword.len > 0) {
        dox_chunk_keyword_merge(eaten);
        dox_chunk_params_merge(eaten);
    } else {
        tab_for_each_entry(param, &eaten->params) {
            qv_append(&eating->params, param);
        }
        tab_for_each_entry(arg, &eaten->params_args) {
            qv_append(&eating->params_args, arg);
        }
        if (eating->paragraphs.len <= 1) {
            eating->paragraph0_args_len += eaten->paragraph0_args_len;
        }
    }

    if (eating->paragraphs.len && eaten->paragraphs.len) {
        sb_addc(tab_last(&eating->paragraphs), ' ');
        sb_addsb(tab_last(&eating->paragraphs), &eaten->paragraphs.tab[0]);
        sb_wipe(&eaten->paragraphs.tab[0]);
        qv_skip(&eaten->paragraphs, 1);
    }
    tab_for_each_entry(paragraph, &eaten->paragraphs) {
        qv_append(&eating->paragraphs, paragraph);
    }

    iopc_loc_merge(&eating->loc, eaten->loc);

    lstr_wipe(&eaten->keyword);
    qv_wipe(&eaten->params);
    qv_wipe(&eaten->params_args);
    qv_wipe(&eaten->paragraphs);
}

static int
read_dox(iopc_parser_t *pp, int tk_offset, qv_t(dox_chunk) *chunks, bool back,
         int ignore_token, bool *res)
{
    const iopc_token_t *tk;
    dox_tok_t *dox;

    if (ignore_token && CHECK_N(pp, tk_offset, ignore_token)) {
        tk_offset++;
    }

    tk = TK_N(pp, tk_offset);
    dox = tk->dox;

    if (!CHECK_N(pp, tk_offset, ITOK_DOX_COMMENT)
    ||  (back && !dox->is_back))
    {
        *res = false;
        return 0;
    }

    /* XXX: when reading front, back comments are forced to be front
     *      with first chunk = "<" */
    if (!back && dox->is_back) {
        dox_chunk_t chunk;

        dox->is_back = false;
        dox_chunk_init(&chunk);
        sb_addc(sb_init(qv_growlen(&chunk.paragraphs, 1)), '<');
        chunk.loc = tk->loc;
        chunk.loc.lmax = chunk.loc.lmin;
        qv_insert(&dox->chunks, 0, chunk);
    }

    tab_for_each_ptr(chunk, &dox->chunks) {
        bool force_merge = false;
        dox_chunk_t *last;

        if (chunks->len == 0) {
            goto append;
        }
        last = tab_last(chunks);

        /* force merge if the chunk has a unknown keyword, so that syntax
         * like: "See \ref field" or "\param[in] a The \p ref" works.
         */
        if (chunk->keyword.len > 0
        &&  iopc_dox_check_keyword(chunk->keyword, NULL) < 0)
        {
            force_merge = true;
        }

        /* this test is intended for first chunk of the current token */
        if (force_merge
        ||  (!chunk->keyword.len && chunk->loc.lmin - last->loc.lmax < 2))
        {
            dox_chunk_merge(last, chunk);
            continue;
        }

      append:
        qv_append(chunks, *chunk);
    }
    (DROP)(pp, 1, tk_offset);
    *res = true;
    return 0;
}

static int read_dox_front(iopc_parser_t *pp, qv_t(dox_chunk) *chunks)
{
    int offset = 0;
    bool read_dox_res;

    do {
        /* XXX: we ignore tags when reading doxygen front comments */
        if (CHECK_N(pp, offset, ITOK_INTEGER)
        &&  CHECK_N(pp, offset + 1, ':'))
        {
            offset += 2;
        }
        RETHROW(read_dox(pp, offset, chunks, false, 0, &read_dox_res));
    } while (read_dox_res);

    return 0;
}

static int
read_dox_back(iopc_parser_t *pp, qv_t(dox_chunk) *chunks, int ignore_token)
{
    bool read_dox_res;

    do {
        RETHROW(read_dox(pp, 0, chunks, true, ignore_token, &read_dox_res));
    } while (read_dox_res);

    return 0;
}

static
void iopc_dox_desc_append_paragraphs(lstr_t *desc, const qv_t(sb) *paragraphs)
{
    SB_1k(text);

    sb_add_lstr(&text, *desc);
    tab_for_each_ptr(paragraph, paragraphs) {
        if (text.len && paragraph->len) {
            sb_addc(&text, '\n');
        }
        sb_addsb(&text, paragraph);
    }
    lstr_wipe(desc);
    lstr_transfer_sb(desc, &text, false);
}

static void
iopc_dox_append_paragraphs_to_details(qv_t(iopc_dox) *comments,
                                      const qv_t(sb) *paragraphs)
{
    iopc_dox_t *dox;

    if (!paragraphs->len)
        return;

    dox = iopc_dox_find_type_or_create(comments, IOPC_DOX_TYPE_DETAILS);
    iopc_dox_desc_append_paragraphs(&dox->desc, paragraphs);
}

static void
iopc_dox_split_paragraphs(const qv_t(sb) *paragraphs,
                          qv_t(sb) *first, qv_t(sb) *others)
{
    if (first)
        qv_init_static(first, paragraphs->tab, 1);

    if (others)
        qv_init_static(others, paragraphs->tab + 1, paragraphs->len - 1);
}

static void
iopc_dox_append_paragraphs(qv_t(iopc_dox) *comments, lstr_t *desc,
                           const qv_t(sb) *paragraphs)
{
    qv_t(sb) first;
    qv_t(sb) others;

    if (!paragraphs->len)
        return;

    iopc_dox_split_paragraphs(paragraphs, &first, &others);

    iopc_dox_desc_append_paragraphs(desc, &first);
    iopc_dox_append_paragraphs_to_details(comments, &others);
}

/* XXX: the first paragraph of a chunk could be empty
 * and it is the sole paragraph of a chunk that can be empty
 * in case it is empty we must append the paragraphs to 'details'
 * but only if there are others paragraphs in order to avoid an empty
 * 'details'
 */
static int
iopc_dox_check_paragraphs(qv_t(iopc_dox) *comments,
                          const qv_t(sb) *paragraphs)
{
    if (!paragraphs->len)
        return -1;

    if (paragraphs->tab[0].len)
        return 0;

    if (paragraphs->len > 1) {
        iopc_dox_append_paragraphs_to_details(comments, paragraphs);
    }
    return -1;
}

static int
build_dox_param(const iopc_fun_t *owner, qv_t(iopc_dox) *res,
                dox_chunk_t *chunk)
{
    iopc_dox_arg_dir_t dir;

    if (!chunk->params.len) {
        throw_loc("doxygen param direction not specified", chunk->loc);
    }

    if (chunk->params.len > 1) {
        throw_loc("more than one doxygen param direction", chunk->loc);
    }

    if (iopc_dox_check_param_dir(chunk->params.tab[0], &dir) < 0)
    {
        throw_loc("unsupported doxygen param direction: `%*pM`", chunk->loc,
                  LSTR_FMT_ARG(chunk->params.tab[0]));
    }

#define TEST_ANONYMOUS(X, Y)  \
    (dir == IOPC_DOX_ARG_DIR_##X && !owner->Y##_is_anonymous)

    if (TEST_ANONYMOUS(IN, arg) || TEST_ANONYMOUS(OUT, res)
    ||  TEST_ANONYMOUS(THROW, exn))
    {
        qv_deep_wipe(&chunk->params_args, lstr_wipe);
        qv_append(&chunk->params_args, LSTR_EMPTY_V);
        chunk->paragraph0_args_len = 0;
    }
#undef TEST_ANONYMOUS

    dox_chunk_params_args_validate(chunk);

    if (iopc_dox_check_paragraphs(res, &chunk->paragraphs) < 0) {
        return 0;
    }

    tab_for_each_pos(i, &chunk->params_args) {
        lstr_t arg = chunk->params_args.tab[i];
        iopc_field_t *arg_field;
        qv_t(sb) arg_paragraphs;
        qv_t(sb) object_paragraphs;

        for (int j = i + 1; j < chunk->params_args.len; j++) {
            if (lstr_equal(arg, chunk->params_args.tab[j])) {
                throw_loc("doxygen duplicated `%*pM` argument `%*pM`",
                          chunk->loc, LSTR_FMT_ARG(chunk->params.tab[0]),
                          LSTR_FMT_ARG(arg));
            }
        }

        arg_field = iopc_dox_arg_find_in_fun(arg, dir, owner);
        if (!arg_field) {
            throw_loc("doxygen unrelated `%*pM` argument `%*pM` for RPC `%s`",
                      chunk->loc, LSTR_FMT_ARG(chunk->params.tab[0]),
                      LSTR_FMT_ARG(arg), owner->name);
        }

        iopc_dox_split_paragraphs(&chunk->paragraphs,
                                  &arg_paragraphs, &object_paragraphs);

        iopc_dox_append_paragraphs_to_details(&arg_field->comments,
                                              &arg_paragraphs);
        debug_dump_dox(arg_field->comments, arg_field->name);

        iopc_dox_append_paragraphs_to_details(res, &object_paragraphs);
    }

    return 0;
}

static int build_dox_(qv_t(dox_chunk) *chunks, const void *owner,
                      int attr_type, qv_t(iopc_dox) *comments)
{
    SB(sb, 256);

    qv_init(comments);

    tab_for_each_ptr(chunk, chunks) {
        iopc_dox_t *dox = NULL;
        int type = -1;

        if (chunk->keyword.len
        &&  iopc_dox_check_keyword(chunk->keyword, &type) >= 0
        &&  !iopc_dox_type_is_related(type, attr_type))
        {
            error_loc("unrelated doxygen keyword: `%*pM`", chunk->loc,
                      LSTR_FMT_ARG(chunk->keyword));
            goto error;
        }

        if (type == -1) {
            dox_chunk_params_merge(chunk);
            dox_chunk_keyword_merge(chunk);
        }

        if (iopc_dox_check_paragraphs(comments, &chunk->paragraphs) < 0) {
            continue;
        }

        if (type == IOPC_DOX_TYPE_PARAM) {
            if (build_dox_param(owner, comments, chunk) < 0) {
                goto error;
            }
            continue;
        }

        if (type >= 0) {
            dox = iopc_dox_find_type_or_create(comments, type);
            iopc_dox_append_paragraphs(comments, &dox->desc,
                                       &chunk->paragraphs);
        } else
        if (iopc_dox_find_type(comments, IOPC_DOX_TYPE_BRIEF)) {
            iopc_dox_append_paragraphs_to_details(comments,
                                                  &chunk->paragraphs);
        } else {
            dox = iopc_dox_add(comments, IOPC_DOX_TYPE_BRIEF);
            dox_chunk_autobrief_validate(chunk);
            iopc_dox_append_paragraphs(comments, &dox->desc,
                                       &chunk->paragraphs);
        }

        if (type == IOPC_DOX_TYPE_EXAMPLE) {
            t_scope;
            const qv_t(log_buffer) *logs;
            int res;
            iopc_loc_t loc = chunk->loc;
            lstr_t name = t_lstr_fmt("%s[%d-%d]",
                                     loc.file, loc.lmin, loc.lmax);
            iopc_parser_t pp = {
                .cfolder  = iop_cfolder_new(),
                .ld = iopc_lexer_new(name.s, dox->desc.s, IOPC_FILE_BUFFER)
            };

            log_start_buffering(false);

            sb_reset(&sb);
            sb_addc(&sb, '{');
            res = parse_json_object(&pp, &sb, true);
            sb_addc(&sb, '}');

            logs = log_stop_buffering();

            qv_deep_wipe(&pp.tokens, iopc_token_delete);
            iopc_lexer_delete(&pp.ld);
            iop_cfolder_delete(&pp.cfolder);
            if (res < 0) {
                if (logs->len > 0) {
                    print_error("error: %*pM",
                                LSTR_FMT_ARG(logs->tab[0].msg));
                } else {
                    print_error("json parsing error");
                }
                goto error;
            }
            lstr_transfer_sb(&dox->desc, &sb, false);
        }

    }

    qv_deep_clear(chunks, dox_chunk_wipe);
    return 0;

  error:
    qv_deep_clear(chunks, dox_chunk_wipe);
    return -1;
}

#define build_dox(_chunks, _owner, _attr_type)                             \
    ({                                                                     \
        int _res = build_dox_(_chunks, _owner, _attr_type,                 \
                              &(_owner)->comments);                        \
                                                                           \
        debug_dump_dox((_owner)->comments, (_owner)->name);                \
        _res;                                                              \
    })

#define build_dox_check_all(_chunks, _owner)  \
    ({ build_dox(_chunks, _owner, -1); })

static iopc_attr_t *parse_attr(iopc_parser_t *pp);

static int iopc_add_attr(qv_t(iopc_attr) *attrs, iopc_attr_t **attrp)
{
    iopc_attr_t *attr = *attrp;
    int pos = 0;

    RETHROW(check_attr_multi(attrs, attr, &pos));

    if (pos < 0 || attr->desc->args.len != 1) {
        qv_append(attrs, attr);
    } else {
        tab_for_each_entry(arg, &attr->args) {
            *qv_growlen(&attrs->tab[pos]->args, 1) =
                iopc_arg_dup(&arg);
        }
        iopc_attr_delete(attrp);
    }

    return 0;
}

int iopc_field_add_attr(iopc_field_t *f, iopc_attr_t **attrp, bool tdef)
{
    RETHROW(check_attr_type_field(*attrp, f, tdef));
    RETHROW(iopc_add_attr(&f->attrs, attrp));
    return 0;
}

static int
check_dox_and_attrs(iopc_parser_t *pp, qv_t(dox_chunk) *chunks,
                    qv_t(iopc_attr) *attrs, int attr_type)
{
    qv_clear(attrs);
    qv_deep_clear(chunks, dox_chunk_wipe);

    for (;;) {
        if (CHECK_N(pp, 0, ITOK_ATTR)) {
            iopc_attr_t *attr = RETHROW_PN(parse_attr(pp));

            if (attr_type >= 0) {
                if (check_attr_type_decl(attr, attr_type) < 0) {
                    iopc_attr_delete(&attr);
                    return -1;
                }
            }
            if (iopc_add_attr(attrs, &attr) < 0) {
                iopc_attr_delete(&attr);
                return -1;
            }
        } else {
            bool read_dox_res;

            RETHROW(read_dox(pp, 0, chunks, false, 0, &read_dox_res));
            if (!read_dox_res) {
                break;
            }
        }
    }

    return read_dox_front(pp, chunks);
}
#define check_dox_and_attrs(_pp, _chunks, _attrs)  \
    ((check_dox_and_attrs)((_pp), (_chunks), (_attrs), -1))

/*-}}}-*/
/*----- recursive descent parser -{{{-*/

static char *iopc_upper_ident(iopc_parser_t *pp)
{
    iopc_token_t *tk = TK_P(pp, 0);
    char *res;

    WANT_P(pp, 0, ITOK_IDENT);
    if (!isupper((unsigned char)ident(tk)[0])) {
        throw_loc_p("first character must be uppercase (got `%s')",
                    tk->loc, ident(tk));
    }
    res = dup_ident(tk);
    DROP(pp, 1);
    return res;
}

static char *iopc_aupper_ident(iopc_parser_t *pp)
{
    iopc_token_t *tk = TK_P(pp, 0);
    char *res;

    WANT_P(pp, 0, ITOK_IDENT);
    for (const char *s = ident(tk); *s; s++) {
        if (isdigit((unsigned char)*s) || *s == '_') {
            continue;
        }
        if (isupper((unsigned char)*s)) {
            continue;
        }
        throw_loc_p("this token should be all uppercase", tk->loc);
    }
    res = dup_ident(tk);
    DROP(pp, 1);
    return res;
}

static char *iopc_lower_ident(iopc_parser_t *pp)
{
    iopc_token_t *tk = TK_P(pp, 0);
    char *res;

    WANT_P(pp, 0, ITOK_IDENT);
    if (!islower((unsigned char)ident(tk)[0])) {
        throw_loc_p("first character must be lowercase (got `%s')",
                    tk->loc, ident(tk));
    }
    res = dup_ident(tk);
    DROP(pp, 1);
    return res;
}

static iopc_pkg_t *
check_path_exists(iopc_parser_t *pp, iopc_path_t *path)
{
    iopc_pkg_t *pkg;

    pkg = qm_get_def(iopc_pkg, &_G.pkgs, pretty_path_dot(path), NULL);
    if (pkg) {
        return pkg;
    }

    if (pp->base) {
        if ((pkg = iopc_try_file(pp, pp->base, path))) {
            return pkg;
        }
    }
    if (pp->includes) {
        tab_for_each_entry(include, pp->includes) {
            if ((pkg = iopc_try_file(pp, include, path))) {
                return pkg;
            }
        }
    }
    throw_loc_p("unable to find file `%s` in the include path",
                path->loc, pretty_path(path));
}

static iopc_path_t *parse_path_aux(iopc_parser_t *pp, iopc_pkg_t **modp)
{
    iopc_path_t *path;
    iopc_token_t *tk0 = TK_P(pp, 0);
    char *lowered;

    path = iopc_path_new();
    path->loc = tk0->loc;
    if (!(lowered = iopc_lower_ident(pp))) {
        goto error;
    }
    qv_append(&path->bits, lowered);

    while (CHECK(pp, 0, '.',        goto error)
    &&     CHECK(pp, 1, ITOK_IDENT, goto error))
    {
        iopc_token_t *tk1 = TK(pp, 1, goto error);

        if (!islower((unsigned char)ident(tk1)[0])) {
            break;
        }
        qv_append(&path->bits, dup_ident(tk1));
        iopc_loc_merge(&path->loc, tk1->loc);
        DROP(pp, 2);
    }

    if (modp && !(*modp = check_path_exists(pp, path))) {
        goto error;
    }
    return path;

  error:
    iopc_path_delete(&path);
    return NULL;
}

static iopc_path_t *parse_pkg_stmt(iopc_parser_t *pp)
{
    iopc_path_t *path;

    EAT_KW_P(pp, "package");
    path = RETHROW_P(parse_path_aux(pp, NULL));

    if (CHECK(pp, 0, '.', goto error)) {
        if (__want(pp, 1, ITOK_IDENT) < 0) {
            goto error;
        }
    }

    if (__eat(pp, ';') < 0) {
        goto error;
    }

    return path;

  error:
    iopc_path_delete(&path);
    return NULL;
}

iop_type_t iop_get_type(lstr_t name)
{
    int v = iopc_get_token_lstr(name);

    if (v == IOPC_TK_unknown) {
        return IOP_T_STRUCT;
    }
    for (const char *p = name.s; p < name.s + name.len; p++) {
        if (isupper(*p)) {
            return IOP_T_STRUCT;
        }
    }
    switch (v) {
      case IOPC_TK_BYTE:   return IOP_T_I8;
      case IOPC_TK_UBYTE:  return IOP_T_U8;
      case IOPC_TK_SHORT:  return IOP_T_I16;
      case IOPC_TK_USHORT: return IOP_T_U16;
      case IOPC_TK_INT:    return IOP_T_I32;
      case IOPC_TK_UINT:   return IOP_T_U32;
      case IOPC_TK_LONG:   return IOP_T_I64;
      case IOPC_TK_ULONG:  return IOP_T_U64;
      case IOPC_TK_BOOL:   return IOP_T_BOOL;

      case IOPC_TK_BYTES:  return IOP_T_DATA;
      case IOPC_TK_DOUBLE: return IOP_T_DOUBLE;
      case IOPC_TK_STRING: return IOP_T_STRING;
      case IOPC_TK_XML:    return IOP_T_XML;
      case IOPC_TK_VOID:   return IOP_T_VOID;
      default:
        return IOP_T_STRUCT;
    }
}

static iop_type_t get_type_kind(iopc_token_t *tk)
{
    return iop_get_type(LSTR_SB_V(&tk->b));
}

static int parse_struct_type(iopc_parser_t *pp, iopc_pkg_t **type_pkg,
                             iopc_path_t **path, char **name)
{
    iopc_token_t *tk = TK_N(pp, 0);

    WANT(pp, 0, ITOK_IDENT);
    if (islower((unsigned char)ident(tk)[0])) {
        *path = RETHROW_PN(parse_path_aux(pp, type_pkg));
        EAT(pp, '.');
        WANT(pp, 0, ITOK_IDENT);
        TK_N(pp, 0);
    }
    *name = RETHROW_PN(iopc_upper_ident(pp));
    return 0;
}

int iopc_check_field_type(const iopc_field_t *f, sb_t *err)
{
    if (f->repeat == IOP_R_OPTIONAL) {
        if (f->is_static) {
            sb_sets(err, "optional static members are forbidden");
            return -1;
        }
    } else
    if (f->is_ref) {
        if (f->is_static) {
            sb_sets(err, "referenced static members are forbidden");
            return -1;
        }
        if (f->kind != IOP_T_STRUCT) {
            sb_sets(err,
                    "references can only be applied to structures or unions");
            return -1;
        }
        if (f->repeat != IOP_R_REQUIRED) {
            sb_sets(err, "references can only be applied to required fields");
            return -1;
        }
    } else
    if (f->repeat == IOP_R_REPEATED) {
        if (f->is_static) {
            sb_sets(err, "repeated static members are forbidden");
            return -1;
        }
        if (f->kind == IOP_T_VOID) {
            sb_sets(err, "repeated void types are forbidden");
            return -1;
        }
    }

    return 0;
}

static int parse_field_type(iopc_parser_t *pp, iopc_struct_t *st,
                            iopc_field_t *f)
{
    SB_1k(err);

    WANT(pp, 0, ITOK_IDENT);
    f->kind = get_type_kind(TK_N(pp, 0));

    /* in case of snmpObj structure, some field type are not handled */
    if (f->kind == IOP_T_STRUCT) {
        RETHROW(parse_struct_type(pp, &f->type_pkg, &f->type_path,
                                  &f->type_name));
    } else {
        DROP(pp, 1);
    }

    switch (TK_N(pp, 0)->token) {
      case '?':
        f->repeat = IOP_R_OPTIONAL;
        DROP(pp, 1);
        break;

      case '&':
        f->repeat = IOP_R_REQUIRED;
        f->is_ref = true;
        DROP(pp, 1);
        break;

      case '[':
        WANT(pp, 1, ']');
        f->repeat = IOP_R_REPEATED;
        DROP(pp, 2);
        break;

      default:
        f->repeat = IOP_R_REQUIRED;
        break;
    }

    if (iopc_check_field_type(f, &err) < 0) {
        throw_loc("%*pM", f->loc, SB_FMT_ARG(&err));
    }

    return 0;
}

static int parse_field_defval(iopc_parser_t *pp, iopc_field_t *f, int paren)
{
    iopc_token_t *tk;

    EAT(pp, '=');
    tk = TK_N(pp, 0);

    if (f->repeat != IOP_R_REQUIRED) {
        throw_loc("default values for non required fields makes no sense",
                  tk->loc);
    }
    f->repeat = IOP_R_DEFVAL;

    if (tk->b_is_char) {
        WANT(pp, 0, ITOK_STRING);
        f->defval.u64 = (uint64_t)tk->b.data[0];
        f->defval_type = IOPC_DEFVAL_INTEGER;
        DROP(pp, 1);
    } else
    if (CHECK_N(pp, 0, ITOK_STRING)) {
        f->defval.ptr = p_strdup(tk->b.data);
        f->defval_type = IOPC_DEFVAL_STRING;
        DROP(pp, 1);
    } else
    if (CHECK_N(pp, 0, ITOK_DOUBLE)) {
        f->defval.d = tk->d;
        f->defval_type = IOPC_DEFVAL_DOUBLE;
        DROP(pp, 1);
    } else {
        bool is_signed;

        RETHROW(parse_constant_integer(pp, paren, &f->defval.u64,
                                       &is_signed));
        f->defval_is_signed = is_signed;
        f->defval_type = IOPC_DEFVAL_INTEGER;
    }
    return 0;
}

int iopc_check_tag_value(int tag, sb_t *err)
{
    if (tag < 1) {
        sb_setf(err, "tag is too small (must be >= 1, got %d)", tag);
        return -1;
    }
    if (tag >= 0x8000) {
        sb_setf(err, "tag is too large (must be < 0x8000, got 0x%x)", tag);
        return -1;
    }

    return 0;
}

static iopc_field_t *
parse_field_stmt(iopc_parser_t *pp, iopc_struct_t *st, qv_t(iopc_attr) *attrs,
                 qm_t(iopc_field) *fields, qv_t(i32) *tags, int *next_tag,
                 int paren, bool is_snmp_iface, bool is_rpc_arg)
{
    iopc_loc_t    name_loc;
    iopc_field_t *f = NULL;
    iopc_token_t *tk;
    int           tag;

    f = iopc_field_new();
    f->loc = TK(pp, 0, goto error)->loc;
    f->snmp_is_in_tbl = iopc_is_snmp_tbl(st->type);

    if (SKIP_KW(pp, "static", goto error)) {
        if (!iopc_is_class(st->type)) {
            error_loc("static keyword is only authorized for class fields",
                      f->loc);
            goto error;
        }
        f->is_static = true;
    } else {
        SB_1k(err);

        /* Tag */
        if (CHECK(pp, 0, ITOK_INTEGER, goto error)) {
            if (__want(pp, 1, ':') < 0) {
                goto error;
            }
            tk = TK(pp, 0, goto error);
            f->tag = tk->i;
            *next_tag = f->tag + 1;
            DROP(pp, 2);
            if (CHECK_KW(pp, 0, "static", goto error)) {
                error_loc("tag is not authorized for static class fields",
                          TK(pp, 0, goto error)->loc);
                goto error;
            }
        } else {
            f->tag = (*next_tag)++;
        }

        if (iopc_check_tag_value(f->tag, &err) < 0) {
            error_loc("%*pM", TK(pp, 0, goto error)->loc, SB_FMT_ARG(&err));
            goto error;
        }
    }

    /* If the field is contained by a snmpIface rpc struct, it will have no
     * type (so no need to parse the type), and the flag snmp_is_from_param
     * needs to be set at true */
    if (is_snmp_iface) {
        f->snmp_is_from_param = true;
    } else {
        if (parse_field_type(pp, st, f) < 0) {
            goto error;
        }
        if (is_rpc_arg && f->kind == IOP_T_VOID
        &&  f->repeat == IOP_R_REQUIRED)
        {
            error_loc("required void types are forbidden for rpc arguments",
                      TK(pp, 0, goto error)->loc);
            goto error;
        }
    }

    if (__want(pp, 0, ITOK_IDENT) < 0) {
        goto error;
    }
    f->name = dup_ident(TK(pp, 0, goto error));
    if (strchr(f->name, '_')) {
        error_loc("identifier '%s' contains a _",
                  TK(pp, 0, goto error)->loc, f->name);
        goto error;
    }
    if (!islower(f->name[0])) {
        error_loc("first character must be lowercased (got %s)",
                  TK(pp, 0, goto error)->loc, f->name);
        goto error;
    }

    name_loc = TK(pp, 0, goto error)->loc;
    DROP(pp, 1);

    if (CHECK(pp, 0, '=', goto error)) {
        if (st->type == STRUCT_TYPE_UNION) {
            error_loc("default values are forbidden in union types", f->loc);
            goto error;
        }
        if (f->kind == IOP_T_VOID) {
            error_loc("default values are forbidden for void types", f->loc);
            goto error;
        }
        if (parse_field_defval(pp, f, paren) < 0) {
            goto error;
        }
        assert (f->defval_type);
    } else
    if (f->is_static && !st->is_abstract) {
        error_loc("static fields of non-abstract classes must have a "
                  "default value", f->loc);
        goto error;
    }

    /* XXX At this point, the default value (if there is one) has been read,
     * so the type of field is correct. If you depend on this type (for
     * example for check_attr_type_field()), your code should be below this
     * line. */

    tab_for_each_pos(pos, attrs) {
        iopc_attr_t *attr = attrs->tab[pos];

        if (check_attr_type_field(attr, f, false) < 0) {
            for (; pos < attrs->len; pos++) {
                iopc_attr_delete(&attrs->tab[pos]);
            }
            goto error;
        }
        qv_append(&f->attrs, attr);
    }

    /* Looks for blacklisted keyword (after attribute has been parsed) */
    if (check_name(f->name, name_loc, &f->attrs) < 0) {
        goto error;
    }

    iopc_loc_merge(&f->loc, TK(pp, 0, goto error)->loc);

    tag = f->tag;
    if (qm_add(iopc_field, fields, f->name, f)) {
        error_loc("field name `%s` is already in use", f->loc, f->name);
        goto error;
    }

    if (f->is_static) {
        qv_append(&st->static_fields, f);
        return f;
    }

    tab_for_each_entry(t, tags) {
        if (t == tag) {
            error_loc("tag %d is used twice", f->loc, tag);
            goto error;
        }
    }
    qv_append(tags, tag);
    qv_append(&st->fields, f);
    return f;

  error:
    iopc_field_delete(&f);
    return NULL;
}

static int check_snmp_brief(qv_t(iopc_dox) comments, iopc_loc_t loc,
                            char *name, const char *type)
{
    tab_for_each_pos(pos, &comments) {
        if (comments.tab[pos].type == IOPC_DOX_TYPE_BRIEF) {
            return 0;
        }
    }
    throw_loc("%s `%s` needs a brief that would be used as a "
              "description in the generated MIB", loc, type, name);
}

static int check_snmp_tbl_has_index(iopc_struct_t *st)
{
    bool has_index = false;

    tab_for_each_entry(field, &st->fields) {
        tab_for_each_entry(attr, &field->attrs) {
            if (attr->desc->id == IOPC_ATTR_SNMP_INDEX) {
                has_index = true;
            }
        }
    }

    if (!has_index) {
        throw_loc("each snmp table must contain at least one field that has "
                  "attribute @snmpIndex of type 'uint' or 'string'", st->loc);
    }

    return 0;
}

static int parse_struct(iopc_parser_t *pp, iopc_struct_t *st, int sep,
                        int paren, bool is_snmp_iface, bool is_rpc_arg)
{
    int res = 0;
    qm_t(iopc_field) fields = QM_INIT_CACHED(field, fields);
    int next_tag = 1;
    int next_field_pos = 0;
    int next_static_field_pos = 0;
    bool previous_static = true;
    qv_t(i32) tags;
    qv_t(iopc_attr) attrs;
    qv_t(dox_chunk) chunks;

    qv_inita(&tags, 1024);
    qv_inita(&attrs, 16);
    qv_init(&chunks);

    while (!CHECK_NOEOF(pp, 0, paren, goto error)) {
        iopc_field_t *f;

        if (check_dox_and_attrs(pp, &chunks, &attrs) < 0
        ||  !(f = parse_field_stmt(pp, st, &attrs, &fields, &tags, &next_tag,
                                   paren, is_snmp_iface, is_rpc_arg)))
        {
            goto error;
        }

        if (!previous_static && f->is_static) {
            error_loc("all static attributes must be declared first",
                      TK(pp, 0, goto error)->loc);
            goto error;
        }
        previous_static = f->is_static;

        if (f->is_static) {
            f->field_pos = next_static_field_pos++;
        } else {
            f->field_pos = next_field_pos++;
        }
        RETHROW(read_dox_back(pp, &chunks, sep));
        RETHROW(build_dox_check_all(&chunks, f));

        if (iopc_is_snmp_st(st->type)
        &&  check_snmp_brief(f->comments, f->loc, f->name, "field") < 0)
        {
            goto error;
        }

        if (CHECK(pp, 0, paren, goto error)) {
            break;
        }
        if (__eat(pp, sep) < 0) {
            goto error;
        }
    }

    if (st->type == STRUCT_TYPE_UNION && qm_len(iopc_field, &fields) == 0) {
        error_loc("a union must contain at least one field",
                  TK(pp, 0, goto error)->loc);
        goto error;
    }

    if (iopc_is_snmp_tbl(st->type) && check_snmp_tbl_has_index(st) < 0) {
        goto error;
    }

    iopc_loc_merge(&st->loc, TK(pp, 1, goto error)->loc);

  end:
    qm_wipe(iopc_field, &fields);
    qv_wipe(&tags);
    qv_wipe(&attrs);
    qv_deep_wipe(&chunks, dox_chunk_wipe);
    return res;

  error:
    res = -1;
    goto end;
}

static int
check_class_or_snmp_obj_id_range(iopc_parser_t *pp, int struct_id,
                                 int min, int max)
{
    if (struct_id < min) {
        throw_loc("id is too small (must be >= %d, got %d)",
                  TK_N(pp, 0)->loc, min, struct_id);
    }
    if (struct_id > max) {
        throw_loc("id is too large (must be <= %d, got %d)",
                  TK_N(pp, 0)->loc, max, struct_id);
    }
    return 0;
}

static int parse_handle_class_snmp(iopc_parser_t *pp, iopc_struct_t *st,
                                   bool is_main_pkg)
{
    iopc_token_t *tk;
    bool is_class = iopc_is_class(st->type);

    assert (is_class || iopc_is_snmp_st(st->type));

    /* Parse struct id; This is optional for a struct without parent, and
     * in this case default is 0. */
    if (SKIP_N(pp, ':')) {
        int id, pkg_min, pkg_max, global_min;

        WANT(pp, 0, ITOK_INTEGER);
        tk = TK_N(pp, 0);
        st->class_id = tk->i; /* so st->snmp_obj_id is also set to tk->i */

        if (is_class) {
            id = st->class_id;
            pkg_min = iopc_g.class_id_min;
            pkg_max = iopc_g.class_id_max;
            global_min = 0;
        } else {
            id = st->oid;
            pkg_min = SNMP_OBJ_OID_MIN;
            pkg_max = SNMP_OBJ_OID_MAX;
            global_min = 1;
        }

        if (is_main_pkg) {
            RETHROW(check_class_or_snmp_obj_id_range(pp, id,
                                                     pkg_min, pkg_max));
        } else {
            RETHROW(check_class_or_snmp_obj_id_range(pp, id,
                                                     global_min, 0xFFFF));
        }

        DROP(pp, 1);

        /* Parse parent */
        if (SKIP_N(pp, ':')) {
            iopc_extends_t *xt = iopc_extends_new();

            qv_append(&st->extends, xt);
            xt->loc = TK_N(pp, 0)->loc;
            RETHROW(parse_struct_type(pp, &xt->pkg, &xt->path, &xt->name));
            iopc_loc_merge(&xt->loc, TK_N(pp, 0)->loc);

            /* Check if snmpObj parent is Intersec */
            xt->is_snmp_root = strequal(xt->name, "Intersec");

            if (SKIP_N(pp, ',')) {
                throw_loc("multiple inheritance is not supported",
                          TK_N(pp, 0)->loc);
            }
        } else
        if (iopc_is_snmp_st(st->type)) {
            throw_loc("%s `%s` needs a snmpObj parent", TK_N(pp, 0)->loc,
                      iopc_struct_type_to_str(st->type), st->name);
        }
    } else
    if (iopc_is_snmp_st(st->type)) {
        throw_loc("%s `%s` needs a snmpObj parent", TK_N(pp, 0)->loc,
                  iopc_struct_type_to_str(st->type), st->name);
    }

    return 0;
}

static int
parse_struct_class_union_snmp_stmt(iopc_parser_t *pp,
                                   iopc_struct_type_t type,
                                   bool is_abstract, bool is_local,
                                   bool is_main_pkg, iopc_struct_t *out)
{
    out->is_visible = true;
    out->type = type;
    out->name = RETHROW_PN(iopc_upper_ident(pp));
    out->loc = TK_N(pp, 0)->loc;
    out->is_abstract = is_abstract;
    out->is_local = is_local;

    RETHROW(check_name(out->name, out->loc, &out->attrs));

    if (iopc_is_class(out->type) || iopc_is_snmp_st(out->type)) {
        RETHROW(parse_handle_class_snmp(pp, out, is_main_pkg));
    }

    if (!iopc_is_class(out->type)) {
        if (is_abstract) {
            throw_loc("only classes can be abstract", TK_N(pp, 0)->loc);
        }
        if (is_local) {
            throw_loc("only classes can be local", TK_N(pp, 0)->loc);
        }
    }

    EAT(pp, '{');
    RETHROW(parse_struct(pp, out, ';', '}', false, false));
    EAT(pp, '}');
    EAT(pp, ';');
    return 0;
}

static int __parse_enum_stmt(iopc_parser_t *pp, const qv_t(iopc_attr) *attrs,
                             qv_t(i32) *values, qv_t(dox_chunk) *chunks,
                             iopc_enum_t *out)
{
    t_scope;
    int64_t next_value = 0;
    lstr_t ns;
    lstr_t prefix = LSTR_NULL;

    out->is_visible = true;
    out->loc = TK_N(pp, 0)->loc;

    EAT_KW(pp, "enum");
    out->name = RETHROW_PN(iopc_upper_ident(pp));
    RETHROW(check_name(out->name, out->loc, &out->attrs));
    EAT(pp, '{');

    t_iopc_attr_check_prefix(attrs, &prefix);
    lstr_ascii_toupper(&prefix);

    ns = t_camelcase_to_c(LSTR(out->name));
    lstr_ascii_toupper(&ns);

    if (lstr_equal(ns, prefix)) {
        prefix = LSTR_NULL_V;
    }

    while (!CHECK_NOEOF_N(pp, 0, '}')) {
        iopc_enum_field_t *f = iopc_enum_field_new();
        char *ename;

        if (check_dox_and_attrs(pp, chunks, &f->attrs) < 0
        ||  !(f->name = iopc_aupper_ident(pp)))
        {
            goto error;
        }

        f->loc = TK(pp, 0, goto error)->loc;

        if (SKIP(pp, '=', goto error)) {
            if (parse_constant_integer(pp, '}', (uint64_t *)&next_value,
                                       NULL) < 0)
            {
                goto error;
            }
        }

        tab_for_each_entry(attr, &f->attrs) {
            switch(attr->desc->id) {
              case IOPC_ATTR_GENERIC:
                break;
              case IOPC_ATTR_ALIAS:
                tab_for_each_entry(alias, &attr->args) {
                    ename = asprintf("%*pM_%*pM", LSTR_FMT_ARG(ns),
                                     LSTR_FMT_ARG(alias.v.s));
                    if (qm_add(enums, &_G.enums, ename, f)) {
                        p_delete(&ename);
                        error_loc("enum field alias `%*pM` is used twice",
                                  f->loc, LSTR_FMT_ARG(alias.v.s));
                        goto error;
                    }
                }
                break;
              default:
                error_loc("invalid attribute %s on enum field", f->loc,
                          attr->desc->name.s);
                goto error;
            }
        }

        /* handle properly prefixed enums */
        ename = asprintf("%*pM_%s", LSTR_FMT_ARG(ns), f->name);
        if (prefix.s) {
            uint32_t qpos = qm_put(enums, &_G.enums_forbidden, ename, f, 0);

            if (qpos & QHASH_COLLISION) {
                p_delete(&ename);
            }
            ename = asprintf("%*pM_%s", LSTR_FMT_ARG(prefix), f->name);
        }

        /* Checks for name uniqueness */
        if (qm_add(enums, &_G.enums, ename, f)) {
            p_delete(&ename);
            error_loc("enum field name `%s` is used twice", f->loc, f->name);
            goto error;
        }

        f->value = next_value++;
        qv_append(&out->values, f);
        tab_for_each_entry(v, values) {
            if (v == f->value) {
                throw_loc("value %d is used twice", f->loc, f->value);
            }
        }
        qv_append(values, f->value);

        RETHROW(read_dox_back(pp, chunks, ','));
        RETHROW(build_dox_check_all(chunks, f));

        if (SKIP_N(pp, ',')) {
            continue;
        }

        throw_loc("`,` expected on every line", f->loc);

      error:
        iopc_enum_field_delete(&f);
        return -1;
    }

    iopc_loc_merge(&out->loc, TK_N(pp, 1)->loc);

    WANT(pp, 1, ';');
    DROP(pp, 2);
    return 0;
}

static int parse_enum_stmt(iopc_parser_t *pp, const qv_t(iopc_attr) *attrs,
                           iopc_enum_t *out)
{
    int res;
    qv_t(i32) values;
    qv_t(dox_chunk) chunks;

    qv_inita(&values, 1024);
    qv_inita(&chunks, 16);

    res = __parse_enum_stmt(pp, attrs, &values, &chunks, out);

    qv_wipe(&values);
    qv_deep_wipe(&chunks, dox_chunk_wipe);
    return res;
}

static int parse_typedef_stmt(iopc_parser_t *pp, iopc_field_t *out)
{
    EAT_KW(pp, "typedef");

    out->loc = TK_N(pp, 0)->loc;
    out->is_visible = true;
    RETHROW(parse_field_type(pp, NULL, out));

    out->name = RETHROW_PN(iopc_upper_ident(pp));
    if (strchr(out->name, '_')) {
        throw_loc("identifer '%s' contains a _", TK_N(pp, 0)->loc, out->name);
    }
    EAT(pp, ';');

    return 0;
}

enum {
    IOP_F_ARGS = 0,
    IOP_F_RES  = 1,
    IOP_F_EXN  = 2,
};

static int parse_function_desc(iopc_parser_t *pp, int what, iopc_fun_t *fun,
                               qv_t(dox_chunk) *chunks,
                               iopc_iface_type_t iface_type, bool *res)
{
    static char const * const type_names[] = { "Args", "Res", "Exn", };
    static char const * const tokens[]     = { "in",   "out", "throw", };
    const char *type_name = type_names[what];
    const char *token = tokens[what];
    iopc_struct_t **sptr;
    iopc_field_t  **fptr;
    bool is_snmp_iface = iopc_is_snmp_iface(iface_type);

    *res = false;

    RETHROW(read_dox_front(pp, chunks));

    if (!CHECK_KW_N(pp, 0, token)) {
        return 0;
    }

    if (fun->fun_is_async && (what == IOP_F_EXN)) {
        throw_loc("async functions cannot throw", TK_N(pp, 0)->loc);
    }
    if (is_snmp_iface && (what == IOP_F_EXN || what == IOP_F_RES)) {
        throw_loc("snmpIface cannot out and/or throw", TK_N(pp, 0)->loc);
    }

    DROP(pp, 1);
    if (SKIP_N(pp, '(')) {
        switch (what) {
          case IOP_F_ARGS:
            sptr = &fun->arg;
            fun->arg_is_anonymous = true;
            break;
          case IOP_F_RES:
            sptr = &fun->res;
            fun->res_is_anonymous = true;
            break;
          case IOP_F_EXN:
            sptr = &fun->exn;
            fun->exn_is_anonymous = true;
            break;
          default:
            abort();
        }

        *sptr = iopc_struct_new();
        (*sptr)->name = asprintf("%s%s", fun->name, type_name);
        (*sptr)->loc = TK_N(pp, 0)->loc;
        RETHROW(parse_struct(pp, *sptr, ',', ')', is_snmp_iface, true));
        EAT(pp, ')');
        RETHROW(read_dox_back(pp, chunks, 0));
        RETHROW(build_dox_check_all(chunks, *sptr));
    } else                          /* fname in void ... */
    if (CHECK_KW_N(pp, 0, "void")) {
        if (is_snmp_iface) {
            throw_loc("void is not supported by snmpIface RPCs",
                      TK_N(pp, 0)->loc);
        }
        DROP(pp, 1);
    } else
    if (is_snmp_iface && SKIP_KW_N(pp, "null")) {
        throw_loc("null is not supported by snmpIface RPCs",
                  TK_N(pp, 0)->loc);
    } else
    if ((what == IOP_F_RES) && SKIP_KW_N(pp, "null")) {
        fun->fun_is_async = true;
    } else
    if (is_snmp_iface) {
        throw_loc("snmpIface RPC argument must be anonymous. example "
                  "`in (a, b, c);`", TK_N(pp, 0)->loc);
    } else {                        /* fname in Type ... */
        iop_type_t type = get_type_kind(TK_N(pp, 0));
        iopc_field_t *f;

        if (type != IOP_T_STRUCT) {
            throw_loc("a structure (or a union) type was expected here"
                      " (got %s)", TK_N(pp, 0)->loc, ident(TK_N(pp, 0)));
        }

        /* We use a field to store the type of the function argument. */
        switch (what) {
          case IOP_F_ARGS:
            fptr = &fun->farg;
            fun->arg_is_anonymous = false;
            break;
          case IOP_F_RES:
            fptr = &fun->fres;
            fun->res_is_anonymous = false;
            break;
          case IOP_F_EXN:
            fptr = &fun->fexn;
            fun->exn_is_anonymous = false;
            break;
          default:
            abort();
        }

        f = iopc_field_new();
        *fptr = f;

        f->name = asprintf("%s%s", fun->name, type_name);
        f->loc = TK_N(pp, 0)->loc;
        f->kind = IOP_T_STRUCT;

        RETHROW(parse_struct_type(pp, &f->type_pkg, &f->type_path,
                                  &f->type_name));

        RETHROW(read_dox_back(pp, chunks, 0));
        RETHROW(build_dox_check_all(chunks, f));

        iopc_loc_merge(&f->loc, TK_N(pp, 0)->loc);
    }

    qv_deep_clear(chunks, dox_chunk_wipe);
    *res = true;
    return 0;
}

static iopc_fun_t *
parse_function_stmt(iopc_parser_t *pp, qv_t(iopc_attr) *attrs,
                    qv_t(i32) *tags, int *next_tag,
                    iopc_iface_type_t type)
{
    SB_1k(err);
    iopc_fun_t *fun = iopc_fun_new();
    iopc_token_t *tk;
    int tag;
    qv_t(dox_chunk) fun_chunks, arg_chunks;
    bool res_res;
    bool exn_res;

    qv_init(&fun_chunks);
    qv_init(&arg_chunks);

    if ((check_dox_and_attrs)(pp, &fun_chunks, attrs, IOPC_ATTR_T_RPC) < 0) {
        goto error;
    }

    fun->loc = TK(pp, 0, goto error)->loc;
    if (CHECK(pp, 0, ITOK_INTEGER, goto error)) {
        if (__want(pp, 1, ':') < 0) {
            goto error;
        }
        tk = TK(pp, 0, goto error);
        fun->tag = tk->i;
        *next_tag = fun->tag + 1;
        DROP(pp, 2);
    } else {
        fun->tag = (*next_tag)++;
    }
    if (iopc_check_tag_value(fun->tag, &err) < 0) {
        error_loc("%*pM", TK(pp, 0, goto error)->loc, SB_FMT_ARG(&err));
        goto error;
    }

    qv_splice(&fun->attrs, fun->attrs.len, 0,
              attrs->tab, attrs->len);

    if (!(fun->name = iopc_lower_ident(pp))
    ||  check_name(fun->name, TK(pp, 0, goto error)->loc, &fun->attrs) < 0
    ||  read_dox_back(pp, &fun_chunks, 0) < 0)
    {
        goto error;
    }

    /* Parse function desc */
    if (parse_function_desc(pp, IOP_F_ARGS, fun, &arg_chunks, type,
                            &res_res) < 0)
    {
        goto error;
    }
    if (parse_function_desc(pp, IOP_F_RES, fun, &arg_chunks, type,
                            &res_res) < 0
    ||  parse_function_desc(pp, IOP_F_EXN, fun, &arg_chunks, type,
                            &exn_res) < 0)
    {
        goto error;
    }
    if (!res_res && !exn_res && !iopc_is_snmp_iface(type)) {
        error_loc("no `out` nor `throw` for function `%s`",
                  fun->loc, fun->name);
        goto error;
    }

    if (__eat(pp, ';') < 0) {
        goto error;
    }

    tag = fun->tag;
    for (int i = 0; i < tags->len; i++) {
        if (tags->tab[i] == tag) {
            error_loc("tag %d is used twice", fun->loc, tag);
            goto error;
        }
    }
    qv_append(tags, tag);

    if (build_dox(&fun_chunks, fun, IOPC_ATTR_T_RPC) < 0) {
        goto error;
    }
    if (iopc_is_snmp_iface(type)) {
        if (check_snmp_brief(fun->comments, fun->loc, fun->name,
                             "notification") < 0)
        {
            goto error;
        }
    }

    qv_deep_wipe(&fun_chunks, dox_chunk_wipe);
    qv_deep_wipe(&arg_chunks, dox_chunk_wipe);
    return fun;

  error:
    iopc_fun_delete(&fun);
    qv_deep_wipe(&fun_chunks, dox_chunk_wipe);
    qv_deep_wipe(&arg_chunks, dox_chunk_wipe);
    return NULL;
}

static int parse_snmp_iface_parent(iopc_parser_t *pp, iopc_iface_t *iface,
                                   bool is_main_pkg)
{
   iopc_token_t *tk;

    /* Check OID */
    if (SKIP_N(pp, ':')) {
        WANT(pp, 0, ITOK_INTEGER);
        tk = TK_N(pp, 0);

        iface->oid = tk->i;

        if (is_main_pkg) {
            RETHROW(check_class_or_snmp_obj_id_range(pp, iface->oid,
                                                     SNMP_IFACE_OID_MIN,
                                                     SNMP_IFACE_OID_MAX));
        } else {
            RETHROW(check_class_or_snmp_obj_id_range(pp, iface->oid,
                                                     0, 0xFFFF));
        }
        DROP(pp, 1);
    }

    /* Parse parent */
    if (SKIP_N(pp, ':')) {
        iopc_extends_t *xt = iopc_extends_new();

        qv_append(&iface->extends, xt);

        xt->loc = TK_N(pp, 0)->loc;
        RETHROW(parse_struct_type(pp, &xt->pkg, &xt->path, &xt->name));
        iopc_loc_merge(&xt->loc, TK_N(pp, 0)->loc);

        if (SKIP_N(pp, ',')) {
            throw_loc("multiple inheritance is not supported",
                      TK_N(pp, 0)->loc);
        }
    } else {
        throw_loc("snmpIface `%s` needs a snmpObj parent", TK_N(pp, 0)->loc,
                  iface->name);
    }
    return 0;
}

static iopc_iface_t *parse_iface_stmt(iopc_parser_t *pp,
                                      iopc_iface_type_t type,
                                      const char *name, bool is_main_pkg)
{
    qm_t(iopc_fun) funs = QM_INIT_CACHED(fun, funs);
    qv_t(i32) tags;
    qv_t(iopc_attr) attrs;
    int next_tag = 1;
    iopc_iface_t *iface = iopc_iface_new();

    qv_inita(&tags, 1024);
    qv_inita(&attrs, 16);

    iface->loc = TK(pp, 0, goto error)->loc;
    iface->type = type;

    if (__eat_kw(pp, name) < 0
    ||  !(iface->name = iopc_upper_ident(pp)))
    {
        goto error;
    }

    if (check_name(iface->name, iface->loc, &iface->attrs) < 0) {
        goto error;
    }

    if (iopc_is_snmp_iface(type)
    &&  parse_snmp_iface_parent(pp, iface, is_main_pkg) < 0)
    {
        goto error;
    }

    if (__eat(pp, '{') < 0) {
        goto error;
    }

    while (!CHECK_NOEOF(pp, 0, '}', goto error)) {
        iopc_fun_t *fun;

        fun = parse_function_stmt(pp, &attrs, &tags, &next_tag, iface->type);
        if (!fun) {
            goto error;
        }
        qv_append(&iface->funs, fun);
        fun->pos = iface->funs.len;
        if (qm_add(iopc_fun, &funs, fun->name, fun)) {
            error_loc("a function `%s` already exists", fun->loc, fun->name);
            goto error;
        }
    }

    iopc_loc_merge(&iface->loc, TK(pp, 1, goto error)->loc);
    if (__want(pp, 1, ';') < 0) {
        goto error;
    }
    DROP(pp, 2);

    qm_wipe(iopc_fun, &funs);
    qv_wipe(&tags);
    qv_wipe(&attrs);
    return iface;

  error:
    qm_wipe(iopc_fun, &funs);
    qv_wipe(&tags);
    qv_wipe(&attrs);
    iopc_iface_delete(&iface);
    return NULL;
}

static iopc_field_t *
parse_mod_field_stmt(iopc_parser_t *pp, iopc_struct_t *mod,
                     qm_t(iopc_field) *fields, qv_t(i32) *tags, int *next_tag)
{
    SB_1k(err);
    iopc_field_t *f = NULL;
    iopc_token_t *tk;
    int tag;

    f = iopc_field_new();
    qv_append(&mod->fields, f);
    f->loc = TK_P(pp, 0)->loc;

    if (CHECK_P(pp, 0, ITOK_INTEGER)) {
        WANT_P(pp, 1, ':');
        tk = TK_P(pp, 0);
        f->tag = tk->i;
        *next_tag = f->tag + 1;
        DROP(pp, 2);
    } else {
        f->tag = (*next_tag)++;
    }

    if (iopc_check_tag_value(f->tag, &err) < 0) {
        throw_loc_p("%*pM", TK_P(pp, 0)->loc, SB_FMT_ARG(&err));
    }

    RETHROW_NP(parse_struct_type(pp, &f->type_pkg, &f->type_path,
                                 &f->type_name));
    f->name = RETHROW_P(iopc_lower_ident(pp));
    if (strchr(f->name, '_')) {
        throw_loc_p("identifier '%s' contains a _",
                    TK_P(pp, 0)->loc, f->name);
    }

    iopc_loc_merge(&f->loc, TK_P(pp, 0)->loc);

    tag = f->tag;
    if (qm_add(iopc_field, fields, f->name, f)) {
        throw_loc_p("field name `%s` is already in use", f->loc, f->name);
    }
    for (int i = 0; i < tags->len; i++) {
        if (tags->tab[i] == tag) {
            throw_loc_p("tag %d is used twice", f->loc, tag);
        }
    }
    qv_append(tags, tag);
    return f;
}

static iopc_struct_t *parse_module_stmt(iopc_parser_t *pp)
{
    int next_tag = 1;
    qm_t(iopc_field) fields = QM_INIT_CACHED(field, fields);
    qv_t(i32) tags;
    iopc_struct_t *mod = iopc_struct_new();
    qv_t(dox_chunk) chunks;

    qv_inita(&chunks, 16);
    qv_inita(&tags, 1024);

    mod->loc = TK(pp, 0, goto error)->loc;

    if (__eat_kw(pp, "module") < 0
    ||  !(mod->name = iopc_upper_ident(pp)))
    {
        goto error;
    }

    if (SKIP(pp, ':', goto error)) {
        do {
            iopc_extends_t *xt = iopc_extends_new();

            qv_append(&mod->extends, xt);
            xt->loc  = TK(pp, 0, goto error)->loc;
            if (parse_struct_type(pp, &xt->pkg, &xt->path, &xt->name) < 0) {
                goto error;
            }
            iopc_loc_merge(&xt->loc, TK(pp, 0, goto error)->loc);
        } while (SKIP(pp, ',', goto error));
        if (CHECK(pp, 0, ';', goto error)) {
            goto empty_body;
        }
    }
    if (__eat(pp, '{') < 0) {
        goto error;
    }

    while (!CHECK_NOEOF(pp, 0, '}', goto error)) {
        iopc_field_t *f;

        if (read_dox_front(pp, &chunks) < 0
        ||  !(f = parse_mod_field_stmt(pp, mod, &fields, &tags, &next_tag))
        ||  read_dox_back(pp, &chunks, ';') < 0
        ||  build_dox_check_all(&chunks, f) < 0)
        {
            goto error;
        }
        if (__eat(pp, ';') < 0) {
            goto error;
        }
    }
    DROP(pp, 1);

  empty_body:
    iopc_loc_merge(&mod->loc, TK(pp, 0, goto error)->loc);
    if (__eat(pp, ';') < 0) {
        goto error;
    }
  end:
    qm_wipe(iopc_field, &fields);
    qv_wipe(&tags);
    qv_deep_wipe(&chunks, dox_chunk_wipe);
    return mod;

  error:
    iopc_struct_delete(&mod);
    goto end;
}

static int parse_json_array(iopc_parser_t *pp, sb_t *sb);

static int parse_json_value(iopc_parser_t *pp, sb_t *sb)
{
    SB_1k(tmp);
    iopc_token_t *tk = TK_N(pp, 0);

    switch (tk->token) {
      case ITOK_STRING:
        sb_add_slashes(&tmp, tk->b.data, tk->b.len,
                       "\a\b\e\t\n\v\f\r\"", "abetnvfr\"");
        sb_addf(sb, "\"%*pM\"",  SB_FMT_ARG(&tmp));
        break;

      case ITOK_INTEGER:
        if (tk->i_is_signed) {
            sb_addf(sb, "%jd", tk->i);
        } else {
            sb_addf(sb, "%ju", tk->i);
        }
        break;

      case ITOK_DOUBLE:
        sb_addf(sb, DOUBLE_FMT, tk->d);
        break;

      case ITOK_LBRACE:
        return parse_json_object(pp, sb, false);

      case ITOK_LBRACKET:
        return parse_json_array(pp, sb);

      case ITOK_BOOL:
        sb_addf(sb, "%s", tk->i ? "true" : "false");
        break;

      case ITOK_IDENT:
        if (CHECK_KW_N(pp, 0, "null")) {
            sb_adds(sb, "null");
        } else {
            throw_loc("invalid identifier when parsing json value", tk->loc);
        }
        break;

      default:
        throw_loc("invalid token when parsing json value", tk->loc);
        break;
    }

    DROP(pp, 1);
    return 0;
}

static int parse_json_array(iopc_parser_t *pp, sb_t *sb)
{
    EAT(pp, '[');
    sb_addc(sb, '[');

    if (CHECK_NOEOF_N(pp, 0, ']')) {
        goto end;
    }
    for (;;) {
        RETHROW(parse_json_value(pp, sb));

        if (!CHECK_N(pp, 0, ',')) {
            break;
        }
        DROP(pp, 1);
        if (CHECK_NOEOF_N(pp, 0, ']')) {
            break;
        }
        sb_addc(sb, ',');
    }
  end:
    EAT(pp, ']');
    sb_addc(sb, ']');
    return 0;
}

static int parse_json_object(iopc_parser_t *pp, sb_t *sb, bool toplevel)
{
    char end = toplevel ? ')' : '}';

    if (!toplevel) {
        EAT(pp, '{');
        sb_addc(sb, '{');
    }
    if (CHECK_NOEOF_N(pp, 0, end)) {
        goto end;
    }
    for (;;) {
        iopc_token_t *tk = TK_N(pp, 0);

        if (!CHECK_N(pp, 0, ITOK_IDENT)) {
            WANT(pp, 0, ITOK_STRING);
        }
        sb_addf(sb, "\"%*pM\"",  SB_FMT_ARG(&tk->b));
        DROP(pp, 1);

        if (CHECK_N(pp, 0, '=')) {
            DROP(pp, 1);
        } else {
            EAT(pp, ':');
        }
        sb_addc(sb, ':');

        RETHROW(parse_json_value(pp, sb));

        if (!CHECK_N(pp, 0, ',')) {
            break;
        }
        DROP(pp, 1);
        if (CHECK_NOEOF_N(pp, 0, end)) {
            break;
        }
        sb_addc(sb, ',');
    }
  end:
    if (!toplevel) {
        EAT(pp, '}');
        sb_addc(sb, '}');
    }
    return 0;
}

static int parse_gen_attr_arg(iopc_parser_t *pp, iopc_attr_t *attr,
                              iopc_arg_desc_t *desc, lstr_t *out)
{
    SB_1k(sb);
    iopc_arg_t arg;
    iopc_token_t *tk;

    assert (IOPC_ATTR_REPEATED_MONO_ARG(attr->desc));
    if (!expect(desc)) {
        return -1;
    }

    iopc_arg_init(&arg);
    arg.desc = desc;
    arg.loc  = TK_N(pp, 0)->loc;

    WANT(pp, 0, ITOK_GEN_ATTR_NAME);
    *out = lstr_dups(TK_N(pp, 0)->b.data, -1);
    DROP(pp, 1);

    if (!CHECK_N(pp, 0, ',')) {
        /* Consider @(name) as an empty JSON */
        arg.type = ITOK_IDENT;
        arg.v.s = LSTR("{}");
        goto append;
    }
    EAT(pp, ',');

    tk = TK_N(pp, 0);
    arg.type = tk->token;

    if (CHECK_N(pp, 1, ':') || CHECK_N(pp, 1, '=')) {
        arg.type = ITOK_IDENT;
        sb_addc(&sb, '{');
        RETHROW(parse_json_object(pp, &sb, true));
        sb_addc(&sb, '}');
        lstr_transfer_sb(&arg.v.s, &sb, false);
        goto append;
    }

    switch (arg.type) {
      case ITOK_STRING:
        arg.v.s = lstr_dups(tk->b.data, -1);
        break;

      case ITOK_DOUBLE:
        arg.v.d = tk->d;
        break;

      case ITOK_INTEGER:
      case ITOK_BOOL:
        arg.v.i64 = tk->i;
        break;

      default:
        throw_loc("unable to parse value for generic argument '%*pM'",
                  TK_N(pp, 0)->loc, LSTR_FMT_ARG(*out));
    }

    DROP(pp, 1);

  append:
    qv_append(&attr->args, arg);
    return 0;
}

static int check_snmp_from(const qv_t(lstr) *words)
{
    if (words->len <= 1) {
        return -1;
    }
    tab_for_each_entry(word, words) {
        if (!word.len) {
            return -1;
        }
    }
    return 0;
}

static int parse_struct_snmp_from(iopc_parser_t *pp, iopc_pkg_t **pkg,
                                  iopc_path_t **path, char **name)
{
    t_scope;
    pstream_t ps = ps_initstr(TK_N(pp, 0)->b.data);
    ctype_desc_t sep;
    iopc_path_t *path_new;
    qv_t(lstr) words;

    ctype_desc_build(&sep, ".");

    if (!ps_has_char_in_ctype(&ps, &sep)) {
        return parse_struct_type(pp, pkg, path, name);
    }

    path_new = iopc_path_new();
    path_new->loc = TK_N(pp, 0)->loc;
    t_qv_init(&words, 2);

    /* Split the token */
    ps_split(ps, &sep, 0, &words);

    if (check_snmp_from(&words) < 0) {
        error_loc("invalid snmpParamsFrom `%*pM`", path_new->loc,
                  PS_FMT_ARG(&ps));
        goto error;
    }

    /* Get the path */
    for (int i = 0; i < words.len - 1; i++) {
        qv_append(&path_new->bits, lstr_dup(words.tab[i]).v);
    }
    if (pkg) {
        *pkg = check_path_exists(pp, path_new);
        if (!(*pkg)) {
            goto error;
        }
    }

    *path = path_new;
    *name = lstr_dup(words.tab[words.len - 1]).v;

    DROP(pp, 1);
    return 0;

  error:
    iopc_path_delete(&path_new);
    return -1;
}

static int parse_snmp_attr_arg(iopc_parser_t *pp, iopc_attr_t *attr,
                               iopc_arg_desc_t *desc)
{
    iopc_arg_t arg;

    iopc_arg_init(&arg);
    arg.desc = desc;
    arg.loc  = TK_N(pp, 0)->loc;
    arg.v.s = lstr_dup(LSTR(TK_N(pp, 0)->b.data));
    e_trace(1, "%s=(id)%s", desc->name.s, arg.v.s.s);

    WANT(pp, 0, ITOK_IDENT);
    arg.type = ITOK_IDENT;
    qv_append(&attr->args, arg);

    do {
        iopc_extends_t *xt = iopc_extends_new();

        qv_append(&attr->snmp_params_from, xt);
        xt->loc = TK_N(pp, 0)->loc;
        RETHROW(parse_struct_snmp_from(pp, &xt->pkg, &xt->path, &xt->name));
        iopc_loc_merge(&xt->loc, TK_N(pp, 0)->loc);
    } while (SKIP_N(pp, ','));

    return 0;
}

static int parse_attr_arg(iopc_parser_t *pp, iopc_attr_t *attr,
                          iopc_arg_desc_t *desc)
{
    iopc_arg_t arg;

    if (!desc) {
        /* expect named argument: arg=val */
        lstr_t  str;
        bool    found = false;

        WANT(pp, 0, ITOK_IDENT);
        str = LSTR(TK_N(pp, 0)->b.data);

        tab_for_each_ptr(d, &attr->desc->args) {
            if (lstr_equal(str, d->name)) {
                desc  = d;
                found = true;
                break;
            }
        }
        if (!found) {
            throw_loc("incorrect argument name", TK_N(pp, 0)->loc);
        }
        DROP(pp, 1);
        EAT(pp, '=');
    }

    if (!IOPC_ATTR_REPEATED_MONO_ARG(attr->desc)) {
        tab_for_each_ptr(a, &attr->args) {
            if (a->desc == desc) {
                throw_loc("duplicated argument", TK_N(pp, 0)->loc);
            }
        }
    }

    iopc_arg_init(&arg);
    arg.desc = desc;
    arg.loc  = TK_N(pp, 0)->loc;

    if (desc->type == ITOK_DOUBLE) {
        if (CHECK_N(pp, 0, desc->type)) {
            arg.type = desc->type;
        } else {
            WANT(pp, 0, ITOK_INTEGER);
            arg.type = ITOK_INTEGER;
        }
    } else {
        WANT(pp, 0, desc->type);
        arg.type = desc->type;
    }

    switch (arg.type) {
      case ITOK_STRING:
        arg.v.s = lstr_dup(LSTR(TK_N(pp, 0)->b.data));
        e_trace(1, "%s=(str)%s", desc->name.s, arg.v.s.s);
        DROP(pp, 1);
        break;

      case ITOK_DOUBLE:
        arg.v.d = TK_N(pp,0)->d;
        e_trace(1, "%s=(double)%f", desc->name.s, arg.v.d);
        DROP(pp, 1);
        break;

      case ITOK_IDENT:
        arg.v.s = lstr_dup(LSTR(TK_N(pp, 0)->b.data));
        e_trace(1, "%s=(id)%s", desc->name.s, arg.v.s.s);
        DROP(pp, 1);
        break;

      case ITOK_INTEGER:
      case ITOK_BOOL:
        RETHROW(parse_constant_integer(pp, ')', (uint64_t *)&arg.v.i64,
                                       NULL));
        e_trace(1, "%s=(i64)%jd", desc->name.s, arg.v.i64);
        break;

      default:
        throw_error("incorrect type for argument %*pM",
                    LSTR_FMT_ARG(desc->name));
    }

    qv_append(&attr->args, arg);
    return 0;
}

static int parse_attr_args(iopc_parser_t *pp, iopc_attr_t *attr, lstr_t *out)
{
    bool             explicit = false;
    iopc_arg_desc_t *desc = NULL;
    int i = 0;

    *out = LSTR_NULL_V;

    iopc_lexer_push_state_attr(pp->ld);

    if (CHECK_N(pp, 1, '=')) {
        explicit = true;
    }

    while (!CHECK_NOEOF_N(pp, 0, ')')) {
        if (!explicit) {
            if (IOPC_ATTR_REPEATED_MONO_ARG(attr->desc)) {
                desc = &attr->desc->args.tab[0];
            } else
            if (i >= attr->desc->args.len) {
                throw_loc("too many arguments", attr->loc);
            } else {
                desc = &attr->desc->args.tab[i++];
            }
        }

        if (attr->desc->id == IOPC_ATTR_GENERIC) {
            if (explicit) {
                throw_loc("invalid name for generic attribute: "
                          "`=` is forbidden", attr->loc);
            }
            RETHROW(parse_gen_attr_arg(pp, attr, desc, out));
            WANT(pp, 0, ')');
            break;
        } else
        if (attr->desc->id == IOPC_ATTR_SNMP_PARAMS_FROM) {
            RETHROW(parse_snmp_attr_arg(pp, attr, desc));
            WANT(pp, 0, ')');
            break;
        } else {
            RETHROW(parse_attr_arg(pp, attr, desc));
            if (CHECK_N(pp, 0, ')')) {
                break;
            }
            EAT(pp, ',');
        }
    }
    iopc_lexer_pop_state(pp->ld);
    DROP(pp, 1);

    if (IOPC_ATTR_REPEATED_MONO_ARG(attr->desc) && !attr->args.len) {
        throw_loc("attribute %*pM expects at least one argument", attr->loc,
                  LSTR_FMT_ARG(attr->desc->name));
    }
    if (!IOPC_ATTR_REPEATED_MONO_ARG(attr->desc)
    &&  attr->args.len != attr->desc->args.len)
    {
        throw_loc("attribute %*pM expects %d arguments, got %d", attr->loc,
                  LSTR_FMT_ARG(attr->desc->name), attr->desc->args.len,
                  attr->args.len);
    }

    if (attr->desc->id == IOPC_ATTR_MIN_OCCURS
    ||  attr->desc->id == IOPC_ATTR_MAX_OCCURS
    ||  attr->desc->id == IOPC_ATTR_MIN_LENGTH
    ||  attr->desc->id == IOPC_ATTR_MAX_LENGTH
    ||  attr->desc->id == IOPC_ATTR_LENGTH)
    {
        if (!attr->args.tab[0].v.i64) {
            throw_loc("zero value invalid for attribute %*pM", attr->loc,
                      LSTR_FMT_ARG(attr->desc->name));
        }
    }

    if (attr->desc->id == IOPC_ATTR_CTYPE) {
        tab_for_each_ptr(arg, &attr->args) {
            lstr_t ctype = arg->v.s;

            if (!lstr_endswith(ctype, LSTR("__t"))) {
                throw_loc("invalid ctype `%*pM`: missing __t suffix",
                          attr->loc, LSTR_FMT_ARG(ctype));
            }
        }
    }

    return 0;
}

static iopc_attr_t *parse_attr(iopc_parser_t *pp)
{
    iopc_attr_t     *attr;
    lstr_t           name;
    int              pos;

    WANT_P(pp, 0, ITOK_ATTR);

    attr = iopc_attr_new();
    attr->loc = TK(pp, 0, goto error)->loc;

    name = LSTR(ident(TK(pp, 0, goto error)));
    pos = qm_find(attr_desc, &_G.attrs, &name);
    if (pos < 0) {
        error_loc("incorrect attribute name", attr->loc);
        goto error;
    }
    attr->desc = &_G.attrs.values[pos];
    DROP(pp, 1);

    /* Generic attributes */
    if (attr->desc->id == IOPC_ATTR_GENERIC) {
        assert (attr->desc->args.len == 1);

        if (parse_attr_args(pp, attr, &attr->real_name) < 0
        ||  !attr->real_name.s)
        {
            goto error;
        }
        return attr;
    }

    if (!SKIP(pp, '(', goto error)) {
        if (attr->desc->args.len > 0) {
            error_loc("attribute arguments missing", attr->loc);
            goto error;
        }
        return attr;
    }
    if (attr->desc->args.len == 0) {
        error_loc("attribute should not have arguments", attr->loc);
        goto error;
    }

    if (parse_attr_args(pp, attr, &name) < 0) {
        lstr_wipe(&name);
        goto error;
    }
    return attr;

  error:
    iopc_attr_delete(&attr);
    return NULL;
}


static int check_pkg_path(iopc_parser_t *pp, iopc_path_t *path,
                          const char *base)
{
    int fd = iopc_lexer_fd(pp->ld);
    struct stat st1, st2;
    char buf[PATH_MAX];

    snprintf(buf, sizeof(buf), "%s/%s", base, pretty_path(path));
    path_simplify(buf);
    if (stat(get_full_path(buf), &st1)) {
        throw_loc("incorrect package name", path->loc);
    }
    if (fstat(fd, &st2)) {
        throw_loc("fstat error on fd %d", path->loc, fd);
    }
    if (st1.st_dev != st2.st_dev || st1.st_ino != st2.st_ino) {
        throw_loc("incorrect package name", path->loc);
    }

    return 0;
}

static int add_iface(iopc_pkg_t *pkg, iopc_iface_t *iface,
                      qm_t(iopc_struct) *mod_inter, const char *obj)
{
    qv_append(&pkg->ifaces, iface);
    if (qm_add(iopc_struct, mod_inter, iface->name, (iopc_struct_t *)iface)) {
        throw_loc("%s named `%s` already exists", iface->loc,
                  obj, iface->name);
    }
    return 0;
}

/* Force struct, enum and union to have distinguished name (things qm)*/
/* Force module and interface to have distinguished name   (mod_inter qm)*/
static iopc_pkg_t *parse_package(iopc_parser_t *pp, char *file,
                                 iopc_file_t type, bool is_main_pkg)
{
    iopc_pkg_t *pkg = iopc_pkg_new();
    qm_t(iopc_struct) things = QM_INIT_CACHED(struct, things);
    qm_t(iopc_struct) mod_inter = QM_INIT_CACHED(struct, mod_inter);
    qv_t(iopc_attr) attrs;
    qv_t(dox_chunk) chunks;

    qv_inita(&attrs, 16);
    qv_inita(&chunks, 16);

    pkg->file = file;

    if (read_dox_front(pp, &chunks) < 0
    ||  !(pkg->name = parse_pkg_stmt(pp))
    ||  read_dox_back(pp, &chunks, 0) < 0
    ||  build_dox_check_all(&chunks, pkg) < 0)
    {
        goto error;
    }

    if (type != IOPC_FILE_STDIN) {
        char base[PATH_MAX];

        path_dirname(base, sizeof(base), file);
        for (int i = 0; i < pkg->name->bits.len - 1; i++) {
            path_join(base, sizeof(base), "..");
        }
        path_simplify(base);
        if (type == IOPC_FILE_FD) {
            if (check_pkg_path(pp, pkg->name, base) < 0) {
                goto error;
            }
        }
        pp->base = pkg->base = p_strdup(base);
        qm_add(iopc_pkg, &_G.pkgs, pretty_path_dot(pkg->name), pkg);
        if (is_main_pkg && pp->includes) {
            qv_push(pp->includes, pkg->base);
        }
    }

    while (!CHECK(pp, 0, ITOK_EOF, goto error)) {
        const char *id;
        bool is_abstract = false;
        bool is_local = false;

        if (check_dox_and_attrs(pp, &chunks, &attrs) < 0) {
            goto error;
        }
        if (!attrs.len && CHECK(pp, 0, ITOK_EOF, goto error)) {
            break;
        }

        if (!CHECK(pp, 0, ITOK_IDENT, goto error)) {
            error_loc("expected identifier", TK(pp, 0, goto error)->loc);
            goto error;
        }

#define SET_ATTRS_AND_COMMENTS(_o, _t)                       \
        do {                                                 \
            tab_for_each_pos(pos, &attrs) {        \
                iopc_attr_t *_attr = attrs.tab[pos];         \
                                                             \
                if (check_attr_type_decl(_attr, _t) < 0) {   \
                    qv_skip(&attrs, pos);         \
                    goto error;                              \
                }                                            \
                qv_append(&_o->attrs, _attr);     \
            }                                                \
            qv_clear(&attrs);                     \
            if (read_dox_back(pp, &chunks, 0) < 0            \
            ||  build_dox(&chunks, _o, _t) < 0)              \
            {                                                \
                goto error;                                  \
            }                                                \
        } while (0)

        for (int i = 0; i < 2; i++) {
            if (SKIP_KW(pp, "abstract", goto error)) {
                if (is_abstract) {
                    error_loc("repetition of `abstract` keyword",
                              TK(pp, 0, goto error)->loc);
                    goto error;
                }
                is_abstract = true;
            } else
            if (SKIP_KW(pp, "local", goto error)) {
                if (is_local) {
                    error_loc("repetition of `local` keyword",
                              TK(pp, 0, goto error)->loc);
                    goto error;
                }
                is_local = true;
            } else {
                break;
            }
        }

        id = ident(TK(pp, 0, goto error));

#define PARSE_STRUCT(_id, _type, _attr)  \
        if (strequal(id, _id)) {                                             \
            iopc_struct_t *st = iopc_struct_new();                           \
                                                                             \
            qv_append(&pkg->structs, st);                       \
            SKIP_KW(pp, _id, goto error);                                    \
            if (parse_struct_class_union_snmp_stmt(pp, _type, is_abstract,   \
                                                   is_local,                 \
                                                   is_main_pkg, st) < 0)     \
            {                                                                \
                goto error;                                                  \
            }                                                                \
            SET_ATTRS_AND_COMMENTS(st, _attr);                               \
            if (iopc_is_snmp_tbl(_type)) {                                   \
                if (check_snmp_brief(st->comments, st->loc, st->name,        \
                                     _id) < 0)                               \
                {                                                            \
                    goto error;                                              \
                }                                                            \
            }                                                                \
                                                                             \
            if (qm_add(iopc_struct, &things, st->name, st)) {                \
                error_loc("something named `%s` already exists",             \
                          st->loc, st->name);                                \
                goto error;                                                  \
            }                                                                \
            continue;                                                        \
        }

        PARSE_STRUCT("struct", STRUCT_TYPE_STRUCT, IOPC_ATTR_T_STRUCT);
        PARSE_STRUCT("class",  STRUCT_TYPE_CLASS,
                     IOPC_ATTR_T_STRUCT | IOPC_ATTR_T_CLASS);
        PARSE_STRUCT("snmpObj", STRUCT_TYPE_SNMP_OBJ, IOPC_ATTR_T_SNMP_OBJ);
        PARSE_STRUCT("snmpTbl", STRUCT_TYPE_SNMP_TBL, IOPC_ATTR_T_SNMP_TBL);
        PARSE_STRUCT("union",  STRUCT_TYPE_UNION,  IOPC_ATTR_T_UNION);
#undef PARSE_STRUCT


        if (strequal(id, "enum")) {
            iopc_enum_t *en = iopc_enum_new();

            qv_append(&pkg->enums, en);
            if (parse_enum_stmt(pp, &attrs, en) < 0) {
                goto error;
            }
            SET_ATTRS_AND_COMMENTS(en, IOPC_ATTR_T_ENUM);

            if (qm_add(iopc_struct, &things, en->name, (iopc_struct_t *)en)) {
                error_loc("something named `%s` already exists",
                          en->loc, en->name);
                goto error;
            }
            continue;
        }

        if (strequal(id, "interface")) {
            iopc_iface_t *iface;
            const char *obj = "interface";

            if (!(iface = parse_iface_stmt(pp, IFACE_TYPE_IFACE, obj,
                                           is_main_pkg)))
            {
                goto error;
            }
            if (add_iface(pkg, iface, &mod_inter, obj) < 0) {
                goto error;
            }
            SET_ATTRS_AND_COMMENTS(iface, IOPC_ATTR_T_IFACE);
            continue;
        }

        if (strequal(id, "snmpIface")) {
            iopc_iface_t *iface;
            const char *obj = "snmpIface";

            if (!(iface = parse_iface_stmt(pp, IFACE_TYPE_SNMP_IFACE, obj,
                                           is_main_pkg)))
            {
                goto error;
            }
            if (add_iface(pkg, iface, &mod_inter, obj) < 0) {
                goto error;
            }
            SET_ATTRS_AND_COMMENTS(iface, IOPC_ATTR_T_SNMP_IFACE);
            continue;
        }

        if (strequal(id, "module")) {
            iopc_struct_t *mod = parse_module_stmt(pp);

            if (!mod) {
                goto error;
            }
            qv_append(&pkg->modules, mod);
            SET_ATTRS_AND_COMMENTS(mod, IOPC_ATTR_T_MOD);

            if (qm_add(iopc_struct, &mod_inter, mod->name,
                       (iopc_struct_t *)mod))
            {
                error_loc("something named `%s` already exists",
                          mod->loc, mod->name);
                goto error;
            }
            continue;
        }

        if (strequal(id, "typedef")) {
            iopc_field_t *tdef = iopc_field_new();

            qv_append(&pkg->typedefs, tdef);

            if (parse_typedef_stmt(pp, tdef) < 0) {
                goto error;
            }

            tab_for_each_pos(pos, &attrs) {
                iopc_attr_t *attr = attrs.tab[pos];

                if (check_attr_type_field(attr, tdef, true) < 0) {
                    qv_skip(&attrs, pos);
                    goto error;
                }
                qv_append(&tdef->attrs, attr);
            }
            qv_clear(&attrs);
            if (qm_add(iopc_struct, &things, tdef->name,
                       (iopc_struct_t *)tdef))
            {
                error_loc("something named `%s` already exists",
                          tdef->loc, tdef->name);
                goto error;
            }
            continue;
        }

        error_loc("unexpected keyword `%s`", TK(pp, 0, goto error)->loc, id);
        goto error;
#undef SET_ATTRS_AND_COMMENTS
    }

    if (__eat(pp, ITOK_EOF) < 0) {
        goto error;
    }

    qv_wipe(&attrs);
    qv_deep_wipe(&chunks, dox_chunk_wipe);
    qm_wipe(iopc_struct, &things);
    qm_wipe(iopc_struct, &mod_inter);

    return pkg;

  error:
    qv_deep_wipe(&attrs, iopc_attr_delete);
    qv_deep_wipe(&chunks, dox_chunk_wipe);
    qm_wipe(iopc_struct, &things);
    qm_wipe(iopc_struct, &mod_inter);

    if (pkg->name) {
        qm_del_key(iopc_pkg, &_G.pkgs, pretty_path_dot(pkg->name));
    }
    iopc_pkg_delete(&pkg);
    return NULL;
}

/*-}}}-*/

iopc_loc_t iopc_loc_merge2(iopc_loc_t l1, iopc_loc_t l2)
{
    assert (l1.file == l2.file);
    return (iopc_loc_t){
        .file = l1.file,
        .lmin = MIN(l1.lmin, l2.lmin),
        .cmin = MIN(l1.cmin, l2.cmin),
        .lmax = MAX(l1.lmax, l2.lmax),
        .cmax = MAX(l1.cmax, l2.cmax),
    };
}

void iopc_loc_merge(iopc_loc_t *l1, iopc_loc_t l2)
{
    *l1 = iopc_loc_merge2(*l1, l2);
}

iopc_pkg_t *iopc_parse_file(qv_t(cstr) *includes,
                            const qm_t(iopc_env) *env,
                            const char *file, const char *data,
                            bool is_main_pkg)
{
    iopc_pkg_t *pkg = NULL;
    iopc_file_t type;

    if (data) {
        type = IOPC_FILE_BUFFER;
    } else
    if (strequal(file, "-")) {
        type = IOPC_FILE_STDIN;
    } else {
        type = IOPC_FILE_FD;
    }

    if (!pkg) {
        char *path;
        iopc_parser_t pp = {
            .includes = includes,
            .env      = env,
            .cfolder  = iop_cfolder_new(),
        };

        if (type == IOPC_FILE_STDIN) {
            path = p_strdup("<stdin>");
        } else {
            path = p_strdup(file);
        }

        if ((pp.ld = iopc_lexer_new(path, data, type))) {
            pkg = parse_package(&pp, path, type, is_main_pkg);
        } else {
            p_delete(&path);
        }

        qv_deep_wipe(&pp.tokens, iopc_token_delete);
        iopc_lexer_delete(&pp.ld);
        iop_cfolder_delete(&pp.cfolder);
    }

    if (pkg) {
        qm_for_each_pos(iopc_pkg, pos, &_G.pkgs) {
            iopc_pkg_t *p = _G.pkgs.values[pos];

            if (p != pkg) {
                qh_add(iopc_pkg, &pkg->deps, p);
            }
        }
    }

    return pkg;
}

void iopc_parser_initialize(void)
{
    qm_init_cached(iopc_pkg, &_G.pkgs);
    qm_init_cached(enums, &_G.enums);
    qm_init_cached(enums, &_G.enums_forbidden);
    qm_init_cached(attr_desc, &_G.attrs);
    init_attributes();
}

void iopc_parser_shutdown(void)
{
    qm_deep_wipe(iopc_pkg, &_G.pkgs, IGNORE, iopc_pkg_delete);
    qm_deep_wipe(enums, &_G.enums, p_delete, IGNORE);
    qm_deep_wipe(enums, &_G.enums_forbidden, p_delete, IGNORE);
    qm_deep_wipe(attr_desc, &_G.attrs, IGNORE, iopc_attr_desc_wipe);
}
