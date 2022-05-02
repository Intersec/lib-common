/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#include <lib-common/iop-snmp.h>
#include <lib-common/log.h>

#include <sysexits.h>
#include <lib-common/parseopt.h>

static struct {
    logger_t   logger;
    bool       help;
} doc_g = {
#define _G  doc_g
    .logger = LOGGER_INIT_INHERITS(NULL, "snmp-doc"),
};

/* {{{ Helpers */

static lstr_t t_split_camelcase_word(lstr_t s)
{
    t_SB(buf, s.len + 1);

    sb_addc(&buf, toupper(s.s[0]));
    for (int i = 1; i < s.len; i++) {
        if (i && isupper(s.s[i])) {
            sb_addc(&buf, ' ');
        }
        sb_addc(&buf, s.s[i]);
    }

    return LSTR_SB_V(&buf);
}

static lstr_t t_get_short_name(const lstr_t fullname)
{
    lstr_t name = t_lstr_dup(fullname);
    pstream_t obj_name = ps_initlstr(&name);

    if (ps_skip_afterlastchr(&obj_name, '.') < 0) {
        logger_panic(&_G.logger, "fullname `%*pM` should be at least "
                     "composed by `pkg.name`", LSTR_FMT_ARG(fullname));
    }
    return LSTR_PS_V(&obj_name);
}

static lstr_t t_get_name_full_up(const lstr_t fullname)
{
    lstr_t out = t_lstr_dup(fullname);

    for (int i = 0; i < out.len ; i++) {
        out.v[i] = toupper((unsigned char)out.v[i]);
    }
    return out;
}

static lstr_t t_field_get_help(const iop_field_attrs_t *attrs)
{
    const iop_field_attr_t *attr = attrs->attrs;

    for (int i = 0; i < attrs->attrs_len; i++) {
        if (attr[i].type == IOP_FIELD_ATTR_HELP
        ||  attr[i].type == IOP_FIELD_ATTR_HELP_V2)
        {
            const iop_help_t *help = attr[i].args->v.p;

            return t_lstr_cat3(help->brief, help->details,
                               help->warning);
        }
    }
    return LSTR_EMPTY_V;
}

static lstr_t t_rpc_get_help(const iop_rpc_attrs_t *attrs)
{
    const iop_rpc_attr_t *attr = attrs->attrs;

    for (int i = 0; i < attrs->attrs_len; i++) {
        if (attr[i].type == IOP_RPC_ATTR_HELP
        ||  attr[i].type == IOP_RPC_ATTR_HELP_V2)
        {
            const iop_help_t *help = attr[i].args->v.p;

            return t_lstr_cat3(help->brief, help->details,
                               help->warning);
        }
    }
    return LSTR_EMPTY_V;
}

static lstr_t t_struct_get_help(const iop_struct_attrs_t *attrs)
{
    const iop_struct_attr_t *attr = attrs->attrs;

    for (int i = 0; i < attrs->attrs_len; i++) {
        if (attr[i].type == IOP_STRUCT_ATTR_HELP
        ||  attr[i].type == IOP_STRUCT_ATTR_HELP_V2)
        {
            const iop_help_t *help = attr[i].args->v.p;

            return t_lstr_cat3(help->brief, help->details,
                               help->warning);
        }
    }
    return LSTR_EMPTY_V;
}

static lstr_t t_field_get_help_without_dot(const iop_field_attrs_t *attrs)
{
    lstr_t help = t_field_get_help(attrs);

    if (lstr_endswithc(help, '.')) {
        t_SB(buf, help.len + 1);

        sb_addc(&buf, tolower(help.s[0]));

        for (int i = 1; i < help.len - 1; i++) {
            sb_addc(&buf, help.s[i]);
        }

        return LSTR_SB_V(&buf);
    }

    return help;
}


static const iop_field_t *iop_get_field_match_oid(const iop_struct_t *st,
                                                  uint8_t tag)
{
    for (int i = 0; i < st->fields_len; i++) {
        const iop_field_t *field = &st->fields[i];

        if (field->tag == tag) {
            return field;
        }
    }
    logger_panic(&_G.logger, "no field matches wanted OID %u", tag);
}


static lstr_t t_struct_build_oid(qv_t(u16) oids,
                                 const iop_struct_t *snmp_obj)
{
    t_SB(sb, 128);

    assert (iop_struct_is_snmp_obj(snmp_obj) ||
            iop_struct_is_snmp_tbl(snmp_obj));

    do {
        qv_append(&oids, snmp_obj->snmp_attrs->oid);
    } while ((snmp_obj = snmp_obj->snmp_attrs->parent));

    tab_for_each_pos_rev(pos, &oids) {
        sb_addf(&sb, ".%d", oids.tab[pos]);
    }
    return LSTR_SB_V(&sb);
}

static lstr_t t_notif_build_oid(const iop_struct_t *notif,
                                const iop_iface_t *parent)
{
    qv_t(u16) oids;

    t_qv_init(&oids, 16);
    qv_append(&oids, notif->snmp_attrs->oid);
    qv_append(&oids, parent->snmp_iface_attrs->oid);

    return t_struct_build_oid(oids, parent->snmp_iface_attrs->parent);
}

static lstr_t t_field_build_oid(const iop_field_t *field,
                                const iop_struct_t *parent)
{
    qv_t(u16) oids;

    t_qv_init(&oids, 16);
    qv_append(&oids, field->tag);

    return t_struct_build_oid(oids, parent);
}

static const
iop_snmp_attrs_t *doc_field_get_snmp_attr(const iop_field_attrs_t attrs)
{
    for (int i = 0; i < attrs.attrs_len; i++) {
        if (attrs.attrs[i].type == IOP_FIELD_SNMP_INFO) {
            iop_field_attr_arg_t const *arg = attrs.attrs[i].args;

            return (iop_snmp_attrs_t*)arg->v.p;
        }
    }
    logger_panic(&_G.logger, "all snmpObj fields should have snmp attribute");
}

/* }}} */
/* {{{ Alarms */

static void doc_put_alarms_header(sb_t *buf, lstr_t name_full_up)
{
    sb_addf(buf,
            "=== +ALM-%*pM+: Alarms generated by the %*pM ===\n\n"
            "[cols=\"1,4<asciidoc\",options=\"header\"]\n"
            "|===\n"
            "|Features No    | Description, Rationale and Notes\n",
            LSTR_FMT_ARG(name_full_up), LSTR_FMT_ARG(name_full_up));
}

static void doc_put_arg_field(sb_t *buf, const iop_field_t *field,
                          const iop_struct_t *parent, uint16_t oid)
{
    t_scope;
    lstr_t oid_str = t_field_build_oid(field, parent);
    lstr_t help;

    help = t_field_get_help_without_dot(
               iop_get_field_attr_match_oid(parent, oid));

    sb_addf(buf,
            "- <<%*pM, %*pM>> (%*pM): %*pM",
            LSTR_FMT_ARG(field->name), LSTR_FMT_ARG(field->name),
            LSTR_FMT_ARG(oid_str), LSTR_FMT_ARG(help));
}

static lstr_t doc_rpc_get_severity(const iop_iface_t *iface,
                                   const iop_rpc_t *rpc)
{
    iop_value_t val;

    if (iop_rpc_get_gen_attr(iface, rpc, LSTR("snmp:severity"), IOP_T_STRING,
                             NULL, &val) >= 0)
    {
        return val.s;
    } else {
        return LSTR("-");
    }
}

static void doc_put_rpc(sb_t *buf, int tag, lstr_t iface_name,
                        const iop_rpc_t *rpc,
                        const iop_iface_t *parent)
{
    t_scope;
    const iop_struct_t *st = rpc->args;
    lstr_t name = rpc->name;
    lstr_t camelcase = t_split_camelcase_word(name);
    lstr_t oid_str = t_notif_build_oid(st, parent);
    lstr_t help = t_rpc_get_help(&parent->rpc_attrs[tag]);
    lstr_t severity = doc_rpc_get_severity(parent, rpc);

    sb_addf(buf,
            "| ALM-%*pM-%u |\n"
            "*%*pM* (%*pM) +\n"
            "\n%*pM +\n"
            "\n*Severity:* %*pM.\n"
            "\n*Parameters*\n\n",
            LSTR_FMT_ARG(iface_name), st->snmp_attrs->oid,
            LSTR_FMT_ARG(camelcase), LSTR_FMT_ARG(oid_str),
            LSTR_FMT_ARG(help),
            LSTR_FMT_ARG(severity));

    if (st->fields_len == 0) {
        sb_adds(buf, "*No parameter*\n");
        return;
    }
    /* Parameters */
    for (int i = 0; i < st->fields_len; i++) {
        const iop_field_t *field = &st->fields[i];
        const iop_snmp_attrs_t *attr;
        const iop_field_t *field_origin;

        if (!iop_field_has_snmp_info(field)) {
            continue;
        }

        if (!(attr = iop_get_snmp_attrs(&st->fields_attrs[i]))) {
            logger_panic(&_G.logger,
                         "no snmp attribute found for field `%*pM`",
                         LSTR_FMT_ARG(st->fields[i].name));
        }
        field_origin = iop_get_field_match_oid(attr->parent, attr->oid);
        doc_put_arg_field(buf, field_origin, attr->parent, attr->oid);

        if (i == st->fields_len - 1) {
            sb_adds(buf, ".\n");
        } else {
            sb_adds(buf, ";\n");
        }
    }
}

static void doc_put_alarms(sb_t *buf, const iop_pkg_t *pkg)
{
    for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
        t_scope;
        const iop_iface_t *iface = *it;
        lstr_t name_full_up = t_get_name_full_up(pkg->name);

        if (!iop_iface_is_snmp_iface(iface)) {
            continue;
        }

        if (iface->funs_len) {
            doc_put_alarms_header(buf, name_full_up);
        }
        for (int i = 0; i < iface->funs_len; i++) {
            doc_put_rpc(buf, i, name_full_up, &iface->funs[i], iface);
        }
        if (iface->funs_len) {
            sb_adds(buf, "|===\n");
        }
    }
}

/* }}} */
/* {{{ Objects */

static void doc_put_field_header(sb_t *buf)
{
    sb_adds(buf,
            "[cols=\"<20s,20d,10d,40a\",options=\"header\"]\n"
            "|===\n"
            "|Object\n"
            "|OID\n"
            "|Type\n"
            "|Description\n\n");
}

static void doc_put_tbl(sb_t *buf, const iop_struct_t *st)
{
    t_scope;
    qv_t(u16) oids;
    lstr_t shortname = t_get_short_name(st->fullname);
    lstr_t help = t_struct_get_help(st->st_attrs);
    lstr_t oid;

    t_qv_init(&oids, 16);
    oid = t_struct_build_oid(oids, st);

    sb_addf(buf,
            "|[[%*pM]]%*pM\n"
            "|32436%*pM\n"
            "|table\n"
            "|%*pM\n\n",
            LSTR_FMT_ARG(shortname), LSTR_FMT_ARG(shortname),
            LSTR_FMT_ARG(oid),
            LSTR_FMT_ARG(help));
}

static void doc_put_field(sb_t *buf, int pos, const iop_struct_t *st)
{
    t_scope;
    const iop_field_attrs_t field_attrs = st->fields_attrs[pos];
    const iop_field_t *field = &st->fields[pos];
    const iop_snmp_attrs_t *snmp_attrs = doc_field_get_snmp_attr(field_attrs);
    lstr_t oid = t_field_build_oid(field, st);
    lstr_t help;

    help = t_field_get_help_without_dot(
               iop_get_field_attr_match_oid(st, snmp_attrs->oid));

    sb_addf(buf,
            "|[[%*pM]]%*pM\n"
            "|32436%*pM\n"
            "|%s\n"
            "|%*pM.\n\n",
            LSTR_FMT_ARG(field->name), LSTR_FMT_ARG(field->name),
            LSTR_FMT_ARG(oid),
            iop_type_get_string_desc(field->type),
            LSTR_FMT_ARG(help));

    if (field->type == IOP_T_ENUM) {
        const iop_enum_t *en = field->u1.en_desc;

        sb_adds(buf, "Possible values:\n\n");
        for (int i = 0; i < en->enum_len; i++) {
            sb_addf(buf, "- %*pM (%d)\n",
                    LSTR_FMT_ARG(en->names[i]), en->values[i]);
        }
        sb_adds(buf, "\n\n");
    }
}

static void doc_put_fields(sb_t *buf, const iop_pkg_t *pkg)
{
    int compt = 0;

    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        const iop_struct_t *desc = *it;

        if (!iop_struct_is_snmp_st(desc)) {
            continue;
        }

        if (iop_struct_is_snmp_tbl(desc)) {
            doc_put_tbl(buf, desc);
        }

        if (desc->fields_len > 0) {
            t_scope;
            lstr_t short_name = t_get_short_name(desc->fullname);

            if (compt == 0) {
                doc_put_field_header(buf);
            }
            compt++;
            sb_addf(buf, "4+^s|*%*pM*\n\n", LSTR_FMT_ARG(short_name));
        }

        /* deal with snmp fields */
        for (int i = 0; i < desc->fields_len; i++) {
            const iop_field_t field = desc->fields[i];

            if (!iop_field_has_snmp_info(&field)) {
                continue;
            }
            doc_put_field(buf, i, desc);
        }
    }
    if (compt > 0) {
        sb_adds(buf, "|===\n");
    }
}

/* }}} */
/* {{{ Parseopt */

static popt_t popt_g[] = {
    OPT_FLAG('h', "help", &_G.help, "show this help"),
    OPT_END(),
};

static void doc_parseopt(int argc, char **argv, lstr_t *output_notif,
                         lstr_t *output_object)
{
    const char *arg0 = NEXTARG(argc, argv);

    argc = parseopt(argc, argv, popt_g, 0);
    if (argc != 2 || _G.help) {
        makeusage(_G.help ? EX_OK : EX_USAGE, arg0,
                  "<output-notifications-file> <output-objects-file>",
                  NULL, popt_g);
    }
    *output_notif  = LSTR(NEXTARG(argc, argv));
    *output_object = LSTR(NEXTARG(argc, argv));
}

/* }}} */

void iop_write_snmp_doc(sb_t *notif_sb, sb_t *object_sb,
                        const qv_t(pkg) *pkgs)
{
    tab_for_each_entry(pkg, pkgs) {
        doc_put_alarms(notif_sb, pkg);
        doc_put_fields(object_sb, pkg);
    }
}

int iop_snmp_doc(int argc, char **argv, const qv_t(pkg) *pkgs)
{
    lstr_t path_notif = LSTR_NULL;
    lstr_t path_object = LSTR_NULL;
    SB_8k(notif_sb);
    SB_8k(object_sb);

    doc_parseopt(argc, argv, &path_notif, &path_object);
    iop_write_snmp_doc(&notif_sb, &object_sb, pkgs);

    if (sb_write_file(&notif_sb, path_notif.s) < 0) {
        logger_error(&_G.logger,
                     "couldn't write SNMP notification doc file `%*pM`: %m",
                     LSTR_FMT_ARG(path_notif));
        return -1;
    }
    if (sb_write_file(&object_sb, path_object.s) < 0) {
        logger_error(&_G.logger,
                     "couldn't write SNMP object doc file `%*pM`: %m",
                     LSTR_FMT_ARG(path_object));
        return -1;
    }

    return 0;
}
