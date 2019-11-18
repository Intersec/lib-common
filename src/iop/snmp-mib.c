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

#include <lib-common/iop-snmp.h>
#include <lib-common/log.h>

#include <sysexits.h>
#include <lib-common/parseopt.h>

#define LVL1 "    "
#define LVL2 LVL1 LVL1
#define LVL3 LVL2 LVL1
#define LVL4 LVL2 LVL2
#define LVL5 LVL3 LVL2
#define IMPORT_IF_INTERSEC "enterprises"
#define INTERSEC_OID "32436"

static struct {
    logger_t   logger;
    lstr_t     head;
    bool       head_is_intersec;
    qh_t(lstr) unicity_conformance_objects;
    qh_t(lstr) unicity_conformance_notifs;
    qv_t(lstr) conformance_objects;
    qv_t(lstr) conformance_notifs;
    qh_t(lstr) objects_identifier;
    qv_t(lstr) objects_identifier_parent;

    bool   help;
    const char *output;
} mib_g = {
#define _G  mib_g
    .logger = LOGGER_INIT_INHERITS(NULL, "iop2mib"),
};

/* {{{ Helpers */

static lstr_t t_get_short_name(const lstr_t fullname, bool down)
{
    lstr_t name = t_lstr_dup(fullname);
    pstream_t obj_name = ps_initlstr(&name);
    lstr_t out;

    if (ps_skip_afterlastchr(&obj_name, '.') < 0) {
        logger_fatal(&_G.logger, "fullname `%*pM` should be at least "
                     "composed by `pkg.name`", LSTR_FMT_ARG(fullname));
    }
    out = t_lstr_fmt("%s", obj_name.s);
    /* First letter must be down */
    if (down) {
        out.v[0] = tolower((unsigned char)out.v[0]);
    }

    return out;
}

static lstr_t t_split_on_str(lstr_t name, const char *letter, bool enums)
{
    t_SB(buf, 64);
    qv_t(lstr) parts;
    lstr_t word = t_lstr_dup(name);
    pstream_t ps = ps_initlstr(&word);
    ctype_desc_t dot;

    t_qv_init(&parts, 80);
    ctype_desc_build(&dot, letter);
    ps_split(ps, &dot, 0, &parts);
    if (!parts.len) {
        return LSTR_EMPTY_V;
    }

    tab_enumerate_ptr(i, part, &parts) {
        if (enums) {
            for (int j = i != 0; j < part->len; j++) {
                parts.tab[i].v[j] = tolower(part->v[j]);
            }
        } else
        if (i < parts.len -1) {
            *part = t_lstr_cat(*part, LSTR("\'"));
        }
        sb_add_lstr(&buf, parts.tab[i]);
    }

    return t_lstr_fmt("%*pM", SB_FMT_ARG(&buf));
}

static lstr_t t_mib_put_enum(const iop_enum_t *en)
{
    SB_1k(names);

    sb_add_lstr(&names, LSTR("INTEGER {"));
    for (int i = 0; i < en->enum_len; i++) {
        lstr_t next = i < en->enum_len -1 ? LSTR(",") : LSTR(" }");

        sb_addf(&names, " %*pM(%d)%*pM",
                LSTR_FMT_ARG(t_split_on_str(en->names[i], "_", true)), i,
                LSTR_FMT_ARG(next));
    }
    return t_lstr_fmt("%*pM", SB_FMT_ARG(&names));
}

static lstr_t t_get_type_to_lstr(const iop_field_t *field, bool seq,
                                 bool is_index)
{
    switch (field->type) {
      case IOP_T_STRING:
        return is_index ? LSTR("OCTET STRING (SIZE(0..100))") :
                          LSTR("OCTET STRING");
      case IOP_T_I8:
      case IOP_T_I16:
      case IOP_T_I32:
        return LSTR("Integer32");
      case IOP_T_U32:
        return is_index ? LSTR("Integer32 (1..2147483647)") :
                          LSTR("Integer32");
      case IOP_T_BOOL:
        return LSTR("BOOLEAN");

      case IOP_T_ENUM:
        return seq ? LSTR("INTEGER") : t_mib_put_enum(field->u1.en_desc);

      default:
        logger_panic(&_G.logger, "type not handled");
    }
}


#define T_RETURN_HELP(_attr, _type, _name)  \
    do {                                                                     \
        for (int i = 0; i < attrs->attrs_len; i++) {                         \
            if (_attr[i].type == _type                                       \
            ||  _attr[i].type == _type##_V2)                                 \
            {                                                                \
                const iop_help_t *help = _attr[i].args->v.p;                 \
                lstr_t descri;                                               \
                                                                             \
                descri = t_lstr_cat3(help->brief, help->details,             \
                                     help->warning);                         \
                return t_split_on_str(descri, "\"", false);                  \
            }                                                                \
        }                                                                    \
        logger_fatal(&_G.logger, "each %s needs a description", _name);      \
    } while (0)

static lstr_t t_mib_field_get_help(const iop_field_attrs_t *attrs)
{
    const iop_field_attr_t *attr = attrs->attrs;

    T_RETURN_HELP(attr, IOP_FIELD_ATTR_HELP, "field");
}

static lstr_t t_mib_rpc_get_help(const iop_rpc_attrs_t *attrs)
{
    const iop_rpc_attr_t *attr = attrs->attrs;

    T_RETURN_HELP(attr, IOP_RPC_ATTR_HELP, "rpc");
}

static lstr_t t_mib_tbl_get_help(const iop_struct_attrs_t *attrs)
{
    const iop_struct_attr_t *attr = attrs->attrs;

    T_RETURN_HELP(attr, IOP_STRUCT_ATTR_HELP, "snmpTbl");
}
#undef T_GET_HELP

/* }}} */
/* {{{ Header/Footer */

static void mib_open_banner(sb_t *buf)
{
    t_scope;
    lstr_t up_name;

    if(_G.head_is_intersec) {
        up_name = LSTR_NULL_V;
    } else {
        up_name = t_lstr_fmt("-%*pM", LSTR_FMT_ARG(_G.head));
        lstr_ascii_toupper(&up_name);
    }

    sb_addf(buf, "INTERSEC%*pM-MIB DEFINITIONS ::= BEGIN\n\n",
            LSTR_FMT_ARG(up_name));
}

static void mib_close_banner(sb_t *buf)
{
    sb_adds(buf, "\nEND\n"
            "\n\n-- vim:syntax=mib\n");
}

static void mib_get_head(const qv_t(pkg) *pkgs)
{
    bool resolved = false;

    if (pkgs->len <= 0) {
        logger_fatal(&_G.logger,
                     "a package must be provided to build the MIB");
    }

    tab_for_each_entry(pkg, pkgs) {
        for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
            const iop_struct_t *desc = *it;

            if (!iop_struct_is_snmp_obj(desc)) {
                continue;
            }
            if (qh_add(lstr, &_G.objects_identifier, &desc->fullname) < 0) {
                logger_fatal(&_G.logger, "name `%*pM` already exists",
                             LSTR_FMT_ARG(desc->fullname));
            }
            if (desc->snmp_attrs->parent) {
                qv_append(&_G.objects_identifier_parent,
                          desc->snmp_attrs->parent->fullname);
            }
        }
        for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
            const iop_iface_t *iface = *it;

            if (!iop_iface_is_snmp_iface(iface)) {
                continue;
            }
            if (qh_add(lstr, &_G.objects_identifier, &iface->fullname) < 0) {
                logger_fatal(&_G.logger, "name `%*pM` already exists",
                             LSTR_FMT_ARG(iface->fullname));
            }
            qv_append(&_G.objects_identifier_parent,
                      iface->snmp_iface_attrs->parent->fullname);
        }
    }

    tab_for_each_entry(name, &_G.objects_identifier_parent) {
        if (qh_find(lstr, &_G.objects_identifier, &name) < 0) {
            t_scope;
            lstr_t short_name = t_get_short_name(name, true);

            if (!lstr_equal(short_name, _G.head)) {
                if (resolved) {
                    logger_fatal(&_G.logger, "only one snmpObj parent should "
                                 "be imported");
                }
                lstr_copy(&_G.head, short_name);
            }
            resolved = true;
        }
    }

    if (!resolved) {
        _G.head = LSTR("intersec");
        _G.head_is_intersec = true;
    }
}

/* }}} */
/* {{{ Import */

static void mib_put_imports(sb_t *buf)
{
    sb_adds(buf, "IMPORTS\n");

    if (_G.head_is_intersec) {
        sb_adds(buf, LVL1 "MODULE-IDENTITY, " IMPORT_IF_INTERSEC
                " FROM SNMPv2-SMI;\n\n");
        return;
    }
    sb_addf(buf,
            LVL1 "MODULE-COMPLIANCE, OBJECT-GROUP, "
            "NOTIFICATION-GROUP FROM SNMPv2-CONF\n"
            LVL1 "MODULE-IDENTITY, OBJECT-TYPE, NOTIFICATION-TYPE, "
            "Integer32 FROM SNMPv2-SMI\n"
            LVL1 "%*pM FROM INTERSEC-MIB;\n\n",
            LSTR_FMT_ARG(_G.head));
}

/* }}} */
/* {{{ Identity */

static void mib_put_identity(sb_t *buf, const qv_t(mib_rev) *revisions)
{
    mib_revision_t *last_update = tab_last(revisions);

    sb_addf(buf, "-- {{{ Identity\n"
            "\n%*pM%s MODULE-IDENTITY\n"
            LVL1 "LAST-UPDATED \"%*pM\"\n\n"
            LVL1 "ORGANIZATION \"Intersec\"\n"
            LVL1 "CONTACT-INFO \"postal: Tour W - 102 Terasse Boieldieu\n"
            LVL5 LVL1 "  92085 Paris La Defense - Cedex France\n\n"
            LVL4 "  tel:    +33 1 55 70 33 55\n"
            LVL4 "  email:  contact@intersec.com\n"
            LVL4 "  \"\n\n"
            LVL1 "DESCRIPTION \"For more details see Intersec website "
            "http://www.intersec.com\"\n",
            LSTR_FMT_ARG(_G.head), _G.head_is_intersec ? "" : "Identity",
            LSTR_FMT_ARG(last_update->timestamp));

    tab_for_each_pos_rev(pos, revisions) {
        const mib_revision_t *changmt = &revisions->tab[pos];

        sb_addf(buf, LVL1 "REVISION \"%*pM\"\n",
                LSTR_FMT_ARG(changmt->timestamp));
        sb_addf(buf, LVL1 "DESCRIPTION \"%*pM\"\n",
                LSTR_FMT_ARG(changmt->description));
    }

    if (_G.head_is_intersec) {
        sb_adds(buf,
                LVL1 "::= { " IMPORT_IF_INTERSEC " " INTERSEC_OID " }\n");
    } else {
        sb_addf(buf, LVL1 "::= { %*pM 100 }\n", LSTR_FMT_ARG(_G.head));
    }
    sb_adds(buf, "\n-- }}}\n");
}

/* }}} */
/* {{{ Object Identifier */

static void mib_put_snmp_obj(sb_t *buf, const iop_struct_t *snmp_obj)
{
    t_scope;
    const iop_snmp_attrs_t *snmp_attrs = snmp_obj->snmp_attrs;
    /* Having no 'parent' means that in the IOP code the snmpObj was
     * inherited from 'Intersec' */
    lstr_t parent;

    parent = snmp_attrs->parent ?
        t_get_short_name(snmp_attrs->parent->fullname, true) :
        LSTR("intersec");

    sb_addf(buf,
            "%*pM" LVL1 "OBJECT IDENTIFIER ::= { %*pM %d }\n",
            LSTR_FMT_ARG(t_get_short_name(snmp_obj->fullname, true)),
            LSTR_FMT_ARG(parent), snmp_attrs->oid);
}

static void mib_put_snmp_iface(sb_t *buf, const iop_iface_t *snmp_iface)
{
    t_scope;
    const iop_snmp_attrs_t *snmp_attrs = snmp_iface->snmp_iface_attrs;

    if (!snmp_attrs->parent) {
        logger_fatal(&_G.logger, "any snmpIface should have a parent");
    }

    sb_addf(buf,
            "%*pM" LVL1 "OBJECT IDENTIFIER ::= { %*pM %d }\n",
            LSTR_FMT_ARG(t_get_short_name(snmp_iface->fullname, true)),
            LSTR_FMT_ARG(t_get_short_name(snmp_attrs->parent->fullname, true)),
            snmp_attrs->oid);
}

static void mib_put_object_identifier(sb_t *buf, const qv_t(pkg) *pkgs)
{
    if (pkgs->len) {
        sb_addf(buf, "-- {{{ Top Level Structures\n\n");
    }

    tab_for_each_entry(pkg, pkgs) {
        for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
            const iop_struct_t *desc = *it;

            if (!iop_struct_is_snmp_obj(desc)) {
                continue;
            }
            mib_put_snmp_obj(buf, desc);
        }
        for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
            const iop_iface_t *iface = *it;

            if (!iop_iface_is_snmp_iface(iface)) {
                continue;
            }
            mib_put_snmp_iface(buf, iface);
        }
    }

    if (pkgs->len) {
        sb_addf(buf, "\n-- }}}\n");
    }
}

/* }}} */
/* {{{ SnmpTbl */

static void mib_put_tbl_entries(const iop_struct_t *st, lstr_t name_down,
                                sb_t *buf)
{
    t_scope;

    sb_addf(buf, "\n%*pMEntry ::= SEQUENCE {\n",
            LSTR_FMT_ARG(t_get_short_name(st->fullname, false)));

    for (int i = 0; i < st->fields_len; i++) {
        const iop_field_t field = st->fields[i];

        /* In the sequence, the limits should not be given so the
         * is_index argument of t_get_type_to_lstr is set at false
         */
        sb_addf(buf, LVL1 "%*pM %*pM",
                LSTR_FMT_ARG(field.name),
                LSTR_FMT_ARG(t_get_type_to_lstr(&field, true, false)));

        if (i == st->fields_len - 1) {
            sb_addc(buf, '\n');
        } else {
            sb_adds(buf, ",\n");
        }
    }
    sb_adds(buf, "}\n");
}

static void mib_put_snmp_tbl(const iop_struct_t *st, bool has_index,
                             sb_t *buf)
{
    t_scope;
    const iop_snmp_attrs_t *snmp_attrs;
    lstr_t name_up = t_get_short_name(st->fullname, false);
    lstr_t name_down = t_get_short_name(st->fullname, true);
    lstr_t help;
    int nb = 0;

    assert (iop_struct_is_snmp_tbl(st));

    snmp_attrs = st->snmp_attrs;
    help = t_mib_tbl_get_help(st->st_attrs);

    help = t_split_on_str(help, "\'", false);

    sb_addf(buf,
            "\n%*pMTable OBJECT-TYPE\n"
            LVL1 "SYNTAX SEQUENCE OF %*pMEntry\n"
            LVL1 "MAX-ACCESS not-accessible\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"%*pM\"\n"
            LVL1 "::= { %*pM %d }\n",
            LSTR_FMT_ARG(name_down), LSTR_FMT_ARG(name_up), LSTR_FMT_ARG(help),
            LSTR_FMT_ARG(t_get_short_name(snmp_attrs->parent->fullname, true)),
            snmp_attrs->oid);

    /* Define the table entry that gives global information about table
     * entries */
    sb_addf(buf,
            "\n%*pMEntry OBJECT-TYPE\n"
            LVL1 "SYNTAX %*pMEntry\n"
            LVL1 "MAX-ACCESS not-accessible\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"An entry in the table of %*pM\"\n",
            LSTR_FMT_ARG(name_down), LSTR_FMT_ARG(name_up),
            LSTR_FMT_ARG(name_down));

    sb_adds(buf, LVL1 "INDEX { ");
    for (int i = 0; i < st->fields_len; i++) {
        if (iop_field_is_snmp_index(&st->fields[i])) {
            sb_addf(buf, "%*pM", LSTR_FMT_ARG(st->fields[i].name));
            nb++;

            if (nb < iop_struct_get_nb_snmp_indexes(st)) {
                sb_adds(buf, ", ");
            }
        }
    }
    sb_adds(buf, " }\n");

    sb_addf(buf, LVL1 "::= { %*pMTable 1 }\n", LSTR_FMT_ARG(name_down));

    /* Define the table entries (corresponding to the columns) */
    mib_put_tbl_entries(st, name_down, buf);
}

/* }}} */
/* {{{ SnmpObj fields */

static void mib_put_field(sb_t *buf, lstr_t name, int pos,
                          const iop_struct_t *st, bool from_tbl)
{
    t_scope;
    const iop_field_attrs_t *field_attrs = &st->fields_attrs[pos];
    const iop_field_t *field = &st->fields[pos];
    const iop_snmp_attrs_t *snmp_attrs;
    bool is_index = iop_field_is_snmp_index(field);
    const char *access_str;

    snmp_attrs = iop_get_snmp_attrs(field_attrs);

    access_str = iop_struct_is_snmp_param(snmp_attrs->parent) ?
        "accessible-for-notify" : "read-only";

    sb_addf(buf,
            "\n%*pM OBJECT-TYPE\n"
            LVL1 "SYNTAX %*pM\n"
            LVL1 "MAX-ACCESS %s\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"%*pM\"\n"
            LVL1 "::= { %*pM%s %d }\n",
            LSTR_FMT_ARG(name),
            LSTR_FMT_ARG(t_get_type_to_lstr(field, false, is_index)),
            is_index ? "not-accessible" : access_str,
            LSTR_FMT_ARG(t_mib_field_get_help(field_attrs)),
            LSTR_FMT_ARG(t_get_short_name(snmp_attrs->parent->fullname, true)),
            from_tbl ? "Entry" : "",
            snmp_attrs->oid);

    if (qh_add(lstr, &_G.unicity_conformance_objects, &name) < 0) {
        logger_fatal(&_G.logger,
                     "conflicting field name `%*pM`", LSTR_FMT_ARG(name));
    }
    if (!is_index) {
        qv_append(&_G.conformance_objects, name);
    }
}

static void mib_put_tbl_fields(sb_t *buf, const iop_struct_t *desc)
{
    /* deal with snmp fields */
    for (int i = 0; i < desc->fields_len; i++) {
        const iop_field_t *field = &desc->fields[i];

        if (iop_field_has_snmp_info(field)) {
            mib_put_field(buf, field->name, i, desc, true);
        }
    }
}

static void mib_put_fields_and_tbl(sb_t *buf, const iop_pkg_t *pkg)
{
    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        const iop_struct_t *desc = *it;
        bool has_fields = desc->fields_len > 0;

        if (!iop_struct_is_snmp_st(desc)) {
            continue;
        }

        if (iop_struct_is_snmp_tbl(desc)) {
            t_scope;

            sb_addf(buf, "-- {{{ %*pMTable\n",
                    LSTR_FMT_ARG(t_get_short_name(desc->fullname, false)));


            mib_put_snmp_tbl(desc, iop_struct_get_nb_snmp_indexes(desc) > 0,
                             buf);
            mib_put_tbl_fields(buf, desc);
            sb_addf(buf, "\n-- }}}\n");
            continue;
        }

        if (has_fields) {
            t_scope;

            sb_addf(buf,
                "-- {{{ %*pM\n",
                LSTR_FMT_ARG(t_get_short_name(desc->fullname, false)));
        }

        /* deal with snmp fields */
        for (int i = 0; i < desc->fields_len; i++) {
            const iop_field_t field = desc->fields[i];

            if (!iop_field_has_snmp_info(&field)) {
                continue;
            }
            mib_put_field(buf, field.name, i, desc, false);
        }

        if (has_fields) {
            sb_addf(buf, "\n-- }}}\n");
        }
    }
}

/* }}} */
/* {{{ SnmpIface rpcs */

static void mib_put_rpc(sb_t *buf, int pos, const iop_rpc_t *rpc,
                        const iop_rpc_attrs_t attrs, lstr_t iface_name)
{
    t_scope;
    const iop_struct_t *st = rpc->args;

    sb_addf(buf,
            "\n%*pM NOTIFICATION-TYPE\n"
            LVL1 "OBJECTS { ", LSTR_FMT_ARG(rpc->name));

    for (int i = 0; i < st->fields_len; i++) {
        sb_addf(buf, "%*pM", LSTR_FMT_ARG(st->fields[i].name));
        if (i < st->fields_len - 1) {
            sb_addf(buf, ", ");
        }
    }
    sb_addf(buf, " }\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"%*pM\"\n"
            LVL1 "::= { %*pM %d }\n",
            LSTR_FMT_ARG(t_mib_rpc_get_help(&attrs)),
            LSTR_FMT_ARG(t_get_short_name(iface_name, true)), pos + 1);

    if (qh_add(lstr, &_G.unicity_conformance_notifs, &rpc->name) < 0) {
        logger_fatal(&_G.logger,
                     "conflicting notification name `%*pM`",
                     LSTR_FMT_ARG(rpc->name));
    }
    qv_append(&_G.conformance_notifs, rpc->name);
}

static void mib_put_rpcs(sb_t *buf, const iop_pkg_t *pkg)
{
    for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
        const iop_iface_t *iface = *it;
        bool has_rpcs = iface->funs_len > 0;

        if (!iop_iface_is_snmp_iface(iface)) {
            continue;
        }
        if (has_rpcs) {
            t_scope;

            sb_addf(buf, "-- {{{ %*pM\n",
                    LSTR_FMT_ARG(t_get_short_name(iface->fullname, false)));
        }
        /* deal with snmp rpcs */
        for (int i = 0; i < iface->funs_len; i++) {
            const iop_rpc_t rpc = iface->funs[i];
            const iop_rpc_attrs_t attrs = iface->rpc_attrs[i];

            mib_put_rpc(buf, i, &rpc, attrs, iface->fullname);
        }

        if (has_rpcs) {
            sb_addf(buf, "\n-- }}}\n");
        }
    }
}

/* }}} */
/* {{{ Conformance Groups */

static void mib_put_compliance(sb_t *buf)
{
    sb_addf(buf,
            "\n%*pMCompliance MODULE-COMPLIANCE\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION \"The compliance statement for %*pM entities\"\n"
            LVL1 "MODULE\n"
            LVL2 "MANDATORY-GROUPS { %*pMConformanceObject, "
            "%*pMConformanceNotification }\n"
            LVL1 "::= { %*pMIdentity 1}\n",
            LSTR_FMT_ARG(_G.head), LSTR_FMT_ARG(_G.head),
            LSTR_FMT_ARG(_G.head), LSTR_FMT_ARG(_G.head),
            LSTR_FMT_ARG(_G.head));
}

static void mib_put_objects_conformance(sb_t *buf)
{
    sb_addf(buf,
            "\n%*pMConformanceObject OBJECT-GROUP\n"
            LVL1 "OBJECTS { ", LSTR_FMT_ARG(_G.head));

    tab_for_each_pos(pos, &_G.conformance_objects) {
        sb_addf(buf, "%s%*pM", pos == 0 ? "": "  "LVL3,
                LSTR_FMT_ARG(_G.conformance_objects.tab[pos]));
        if (pos < _G.conformance_objects.len - 1) {
            sb_addf(buf, ",\n");
        }
    }
    sb_addf(buf, " }\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"%*pM conformance objects\"\n"
            LVL1 "::= { %*pMIdentity 81 }\n",
            LSTR_FMT_ARG(_G.head), LSTR_FMT_ARG(_G.head));
}

static void mib_put_notifs_conformance(sb_t *buf)
{
    sb_addf(buf,
            "\n%*pMConformanceNotification NOTIFICATION-GROUP\n"
            LVL1 "NOTIFICATIONS { ", LSTR_FMT_ARG(_G.head));

    tab_for_each_pos(pos, &_G.conformance_notifs) {
        sb_addf(buf, "%s%*pM", pos == 0 ? "": LVL5,
                LSTR_FMT_ARG(_G.conformance_notifs.tab[pos]));
        if (pos < _G.conformance_notifs.len - 1) {
            sb_addf(buf, ",\n");
        }
    }
    sb_addf(buf, " }\n"
            LVL1 "STATUS current\n"
            LVL1 "DESCRIPTION\n"
            LVL2 "\"%*pM conformance notifications\"\n"
            LVL1 "::= { %*pMIdentity 80 }\n",
            LSTR_FMT_ARG(_G.head), LSTR_FMT_ARG(_G.head));
}

static void mib_put_compliance_fold(sb_t *buf)
{
    if (_G.conformance_notifs.len == 0 && _G.conformance_objects.len == 0) {
        return;
    }

    sb_addf(buf, "-- {{{ Compliance\n");
    mib_put_compliance(buf);
    mib_put_notifs_conformance(buf);
    mib_put_objects_conformance(buf);
    sb_addf(buf, "\n-- }}}\n");
}

/* }}} */
/* {{{ Module */

static int iop_mib_initialize(void *arg)
{
    qh_init(lstr, &_G.unicity_conformance_objects);
    qh_init(lstr, &_G.unicity_conformance_notifs);
    qv_init(&_G.conformance_objects);
    qv_init(&_G.conformance_notifs);
    qh_init(lstr, &_G.objects_identifier);
    qv_init(&_G.objects_identifier_parent);
    return 0;
}

static int iop_mib_shutdown(void)
{
    qh_wipe(lstr, &_G.unicity_conformance_objects);
    qh_wipe(lstr, &_G.unicity_conformance_notifs);
    qv_wipe(&_G.conformance_objects);
    qv_wipe(&_G.conformance_notifs);
    qh_wipe(lstr, &_G.objects_identifier);
    qv_wipe(&_G.objects_identifier_parent);
    lstr_wipe(&_G.head);
    _G.head_is_intersec = false;
    return 0;
}

static MODULE_BEGIN(iop_mib)
MODULE_END()

/* }}} */
/* {{{ Usage */

static popt_t popt_g[] = {
    OPT_FLAG('h', "help",   &_G.help,   "show this help"),
    OPT_STR('o',  "output", &_G.output, "define output path (if not defined, "
             "the MIB is printed on stdout)"),
    OPT_END(),
};

static void mib_parseopt(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);

    argc = parseopt(argc, argv, popt_g, 0);
    if (argc != 0 || _G.help) {
        makeusage(_G.help ? EX_OK : EX_USAGE, arg0, "", NULL, popt_g);
    }
}

/* }}} */

void iop_write_mib(sb_t *sb, const qv_t(pkg) *pkgs,
                   const qv_t(mib_rev) *revisions)
{
    SB_8k(buffer);

    MODULE_REQUIRE(iop_mib);

    mib_get_head(pkgs);

    mib_put_object_identifier(&buffer, pkgs);
    tab_for_each_entry(pkg, pkgs) {
        mib_put_fields_and_tbl(&buffer, pkg);
        mib_put_rpcs(&buffer, pkg);
    }

    mib_open_banner(sb);
    mib_put_imports(sb);
    mib_put_identity(sb, revisions);
    mib_put_compliance_fold(sb);

    /* Concat both sb */
    sb_addsb(sb, &buffer);
    mib_close_banner(sb);

    MODULE_RELEASE(iop_mib);
}

int iop_mib(int argc, char **argv, const qv_t(pkg) *pkgs,
            const qv_t(mib_rev) *revisions)
{
    SB_8k(sb);

    mib_parseopt(argc, argv);
    iop_write_mib(&sb, pkgs, revisions);

    if (_G.output) {
        if (sb_write_file(&sb, _G.output) < 0) {
            logger_error(&_G.logger, "couldn't write MIB file `%s`: %m",
                         _G.output);
            return -1;
        }
    } else {
        fprintf(stdout, "%*pM", SB_FMT_ARG(&sb));
    }

    return 0;
}

#undef LVL1
#undef LVL2
#undef LVL3
#undef LVL4
#undef LVL5
#undef IMPORT_IF_INTERSEC
#undef INTERSEC_OID
