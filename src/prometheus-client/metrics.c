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

#include <lib-common/container-qhash.h>

#include "priv.h"

static struct {
    logger_t logger;
} prom_metrics_g = {
#define _G  prom_metrics_g
    .logger = LOGGER_INIT_INHERITS(&prom_logger_g, "metrics"),
};

/* {{{ qm_t(prom_metric) */

static uint32_t qv_lstr_hash(const qhash_t *qh, const qv_t(cstr) *qv)
{
    uint32_t res = 0;

    tab_for_each_entry(str, qv) {
        res ^= qhash_str_hash(qh, str);
    }

    return res;
}

static bool
qv_lstr_equal(const qhash_t *qh,
              const qv_t(cstr) *qv1,
              const qv_t(cstr) *qv2)
{
    if (qv1->len != qv2->len) {
        return false;
    }
    for (int i = 0; i < qv1->len; i++) {
        if (!strequal(qv1->tab[i], qv2->tab[i])) {
            return false;
        }
    }
    return true;
}

qm_kptr_ckey_t(prom_metric, qv_t(cstr), prom_metric_t *,
               qv_lstr_hash, qv_lstr_equal);

/* }}} */
/* {{{ base metric classes */

static prom_metric_t *prom_metric_init(prom_metric_t *self)
{
    dlist_init(&self->children_list);
    dlist_init(&self->siblings_list);
    return self;
}

static void prom_metric_wipe(prom_metric_t *self)
{
    /* Remove self from parent's children. */
    if (self->parent) {
        qm_del_key(prom_metric, self->parent->children_by_labels,
                   &self->label_values);
    }

    /* Wipe */
    lstr_wipe(&self->name);
    lstr_wipe(&self->documentation);

#define cstr_wipe(str)  p_delete((char **)str)
    qv_deep_wipe(&self->label_names, cstr_wipe);
    qv_deep_wipe(&self->label_values, cstr_wipe);
#undef cstr_wipe

    dlist_remove(&self->children_list);
    dlist_remove(&self->siblings_list);

    qm_deep_delete(prom_metric, &self->children_by_labels, IGNORE,
                   obj_delete);
}

int prom_metric_check_name(lstr_t name)
{
    pstream_t ps = ps_initlstr(&name);
    ctype_desc_t desc;
    int c;

    THROW_ERR_IF(ps_done(&ps));

    /* First character must be a letter or _ or : */
    c = __ps_getc(&ps);
    THROW_ERR_IF(!isalpha(c) && c != '_' && c != ':');

    /* Other characters must be alphanumeric or _ or : */
    ctype_desc_build(&desc, "_:");
    ctype_desc_combine(&desc, &desc, &ctype_isalnum);
    ps_skip_span(&ps, &desc);
    THROW_ERR_IF(!ps_done(&ps));

    return 0;
}

int prom_metric_check_label_name(const char *name)
{
    pstream_t ps = ps_initstr(name);
    ctype_desc_t desc;
    int c;

    THROW_ERR_IF(ps_done(&ps));

    /* Label names beginning  with __ are reserved for internal use */
    THROW_ERR_IF(ps_startswithstr(&ps, "__"));

    /* First character must be a letter or _ */
    c = __ps_getc(&ps);
    THROW_ERR_IF(!isalpha(c) && c != '_');

    /* Other characters must be alphanumeric or _  */
    ctype_desc_build(&desc, "_");
    ctype_desc_combine(&desc, &desc, &ctype_isalnum);
    ps_skip_span(&ps, &desc);
    THROW_ERR_IF(!ps_done(&ps));

    return 0;
}

__attr_printf__(3, 4) __attr_noreturn__ __cold
static void prom_metric_panic(prom_metric_t *self, const char *func,
                              const char *fmt, ...)
{
    SB_1k(err);
    va_list args;

    sb_addf(&err, "invalid call to %s() method of metric `%*pM`: ",
            func, LSTR_FMT_ARG(self->name));

    va_start(args, fmt);
    sb_addvf(&err, fmt, args);
    va_end(args);

    logger_panic(&_G.logger, "%*pM", SB_FMT_ARG(&err));
}

static void prom_metric_register(prom_metric_t *self)
{
    /* Consistency checks */
    if (self->parent || self->label_values.len) {
        prom_metric_panic(self, "do_register",
                          "only parent metrics can be registered");
    }
    if (!dlist_is_empty(&self->siblings_list)) {
        prom_metric_panic(self, "do_register",
                          "metric is already registered");
    }
    if (!dlist_is_empty(&self->children_list) || self->children_by_labels) {
        prom_metric_panic(self, "do_register", "metric already has children");
    }
    if (prom_metric_check_name(self->name) < 0) {
        prom_metric_panic(self, "do_register", "invalid metric name `%*pM`",
                          LSTR_FMT_ARG(self->name));
    }
    tab_for_each_entry(label_name, &self->label_names) {
        if (prom_metric_check_label_name(label_name)) {
            prom_metric_panic(self, "do_register",
                              "invalid label name `%s`",
                              label_name);
        }
    }
    if (!self->documentation.len) {
        prom_metric_panic(self, "do_register", "metric has no description");
    }

    /* Register */
    dlist_add_tail(&prom_collector_g, &self->siblings_list);
    if (self->label_names.len) {
        self->children_by_labels = qm_new(prom_metric, 0);
    }
}

static void prom_metric_unregister(prom_metric_t *self)
{
    /* Consistency checks */
    if (self->parent || self->label_values.len) {
        prom_metric_panic(self, "unregister",
                          "only parent metrics can be unregistered");
    }

    /* Unregister */
    dlist_remove(&self->siblings_list);
}

static void prom_metric_check_labels(prom_metric_t *self, const char *func,
                                     const qv_t(cstr) *label_values)
{
    if (!self->label_names.len) {
        prom_metric_panic(self, func, "no label names defined in metric");
    }
    if (label_values->len != self->label_names.len) {
        prom_metric_panic(self, func, "incorrect labels count (%d != %d)",
                          label_values->len, self->label_names.len);
    }
    if (!self->children_by_labels) {
        prom_metric_panic(self, func, "no children_by_labels defined");
    }
}

static prom_metric_t *
(prom_metric_labels)(prom_metric_t *self, const qv_t(cstr) *label_values)
{
    int pos;
    prom_metric_t *child;

    /* Consistency checks */
    prom_metric_check_labels(self, "labels", label_values);

    /* Lookup */
    pos = qm_reserve(prom_metric, self->children_by_labels, label_values, 0);
    if (pos & QHASH_COLLISION) {
        return self->children_by_labels->values[pos ^ QHASH_COLLISION];
    }

    /* Create child */
    child = obj_new_of_class(prom_metric, self->v.as_obj_cls);
    child->parent = self;
    dlist_add_tail(&self->children_list, &child->siblings_list);

    qv_grow(&child->label_values, label_values->len);
    tab_for_each_entry(label, label_values) {
        qv_append(&child->label_values, p_strdup(label));
    }

    self->children_by_labels->keys[pos] = &child->label_values;
    self->children_by_labels->values[pos] = child;

    return child;
}

static void
(prom_metric_remove)(prom_metric_t *self, const qv_t(cstr) *label_values)
{
    int pos;

    /* Consistency checks */
    prom_metric_check_labels(self, "labels", label_values);

    /* Remove child */
    pos = qm_del_key(prom_metric, self->children_by_labels, label_values);
    if (pos >= 0) {
        obj_delete(&self->children_by_labels->values[pos]);
    }
}

static void prom_metric_clear(prom_metric_t *self)
{
    /* Consistency checks */
    if (!self->children_by_labels) {
        prom_metric_panic(self, "clear", "no children_by_labels defined");
    }

    /* Clear */
    qm_deep_clear(prom_metric, self->children_by_labels, IGNORE, obj_delete);
}

OBJ_VTABLE(prom_metric)
    prom_metric.init        = prom_metric_init;
    prom_metric.wipe        = prom_metric_wipe;
    prom_metric.do_register = prom_metric_register;
    prom_metric.unregister  = prom_metric_unregister;
    prom_metric.labels      = prom_metric_labels;
    prom_metric.remove      = prom_metric_remove;
    prom_metric.clear       = prom_metric_clear;
OBJ_VTABLE_END()


static bool is_metric_observable(const prom_metric_t *metric)
{
    return metric->parent || !metric->label_names.len;
}

prom_metric_t *prom_metric_new(const object_class_t *cls,
                               const char *name, const char *documentation,
                               const char **labels, int nb_labels)
{
    prom_metric_t *res;

    /* Create metric */
    res = obj_new_of_class(prom_metric, cls);
    res->name = lstr_dups(name, -1);
    res->documentation = lstr_dups(documentation, -1);

    qv_growlen(&res->label_names, nb_labels);
    for (int i = 0; i < nb_labels; i++) {
        res->label_names.tab[i] = p_strdup(labels[i]);
    }

    /* Register it */
    obj_vcall(res, do_register);

    return res;
}

static void
simple_value_metric_add(prom_simple_value_metric_t *self, double to_add)
{
    spin_lock(&self->value_lock);
    self->value += to_add;
    spin_unlock(&self->value_lock);
}

static double
prom_simple_value_metric_get_value(prom_simple_value_metric_t *self)
{
    double res;

    spin_lock(&self->value_lock);
    res = self->value;
    spin_unlock(&self->value_lock);

    return res;
}

OBJ_VTABLE(prom_simple_value_metric)
    prom_simple_value_metric.get_value = prom_simple_value_metric_get_value;
OBJ_VTABLE_END()

/* }}} */
/* {{{ prom_counter_t */

static void prom_counter_add(prom_counter_t *self, double value)
{
    if (expect(is_metric_observable(&self->super.super) && value >= 0)) {
        simple_value_metric_add(&self->super, value);
    }
}

static void prom_counter_inc(prom_counter_t *self)
{
    if (expect(is_metric_observable(&self->super.super))) {
        simple_value_metric_add(&self->super, 1.);
    }
}

OBJ_VTABLE(prom_counter)
    prom_counter.add = prom_counter_add;
    prom_counter.inc = prom_counter_inc;
OBJ_VTABLE_END()

/* }}} */
/* {{{ prom_gauge_t */

static void prom_gauge_add(prom_gauge_t *self, double value)
{
    if (expect(is_metric_observable(&self->super.super))) {
        simple_value_metric_add(&self->super, value);
    }
}

static void prom_gauge_inc(prom_gauge_t *self)
{
    if (expect(is_metric_observable(&self->super.super))) {
        simple_value_metric_add(&self->super, 1.);
    }
}

static void prom_gauge_sub(prom_gauge_t *self, double value)
{
    if (expect(is_metric_observable(&self->super.super))) {
        simple_value_metric_add(&self->super, -value);
    }
}

static void prom_gauge_dec(prom_gauge_t *self)
{
    if (expect(is_metric_observable(&self->super.super))) {
        simple_value_metric_add(&self->super, -1.);
    }
}

static void prom_gauge_set(prom_gauge_t *self, double value)
{
    if (expect(is_metric_observable(&self->super.super))) {
        spin_lock(&self->value_lock);
        self->value = value;
        spin_unlock(&self->value_lock);
    }
}

OBJ_VTABLE(prom_gauge)
    prom_gauge.add = prom_gauge_add;
    prom_gauge.inc = prom_gauge_inc;
    prom_gauge.sub = prom_gauge_sub;
    prom_gauge.dec = prom_gauge_dec;
    prom_gauge.set = prom_gauge_set;
OBJ_VTABLE_END()

/* }}} */
/* {{{ Bridge function for exposition in text format */

static void bridge_simple_value(prom_simple_value_metric_t *metric, sb_t *out)
{
    lstr_t name = metric->parent ? metric->parent->name : metric->name;
    double value = obj_vcall(metric, get_value);

    sb_add_lstr(out, name);

    if (metric->label_values.len) {
        sb_addc(out, '{');
        for (int i = 0; i < metric->label_values.len; i++) {
            const char *label_name = metric->parent->label_names.tab[i];
            const char *label_value = metric->label_values.tab[i];

            if (i > 0) {
                sb_addc(out, ',');
            }
            sb_addf(out, "%s=\"", label_name);
            sb_adds_slashes(out, label_value, "\\\n", "\\n");
            sb_addc(out, '"');
        }
        sb_addc(out, '}');
    }

    sb_addf(out, " %g\n", value);
}

void prom_collector_bridge(const dlist_t *collector, sb_t *out)
{
    prom_metric_t *metric;

    dlist_for_each_entry(metric, collector, siblings_list) {
        lstr_t metric_type = LSTR_NULL_V;

        /* Skip metrics without samples */
        if (metric->label_names.len
        &&  !qm_len(prom_metric, metric->children_by_labels))
        {
            continue;
        }

        if (out->len) {
            sb_adds(out, "\n");
        }

        /* Add HELP and TYPE */
        sb_addf(out, "# HELP %*pM ", LSTR_FMT_ARG(metric->name));
        sb_add_slashes(out,
                       metric->documentation.s, metric->documentation.len,
                       "\\\n", "\\n");
        sb_addc(out, '\n');

        /* Add TYPE */
        if (obj_is_a(metric, prom_counter)) {
            metric_type = LSTR("counter");
        } else
        if (obj_is_a(metric, prom_gauge)) {
            metric_type = LSTR("gauge");
        } else {
            assert (false);
        }
        sb_addf(out, "# TYPE %*pM %*pM\n",
                LSTR_FMT_ARG(metric->name),
                LSTR_FMT_ARG(metric_type));

        /* Add values */
        if (is_metric_observable(metric)) {
            bridge_simple_value(obj_vcast(prom_simple_value_metric, metric),
                                out);
        } else {
            prom_simple_value_metric_t *child;

            dlist_for_each_entry(child, &metric->children_list,
                                 siblings_list)
            {
                bridge_simple_value(child, out);
            }
        }
    }
}

/* }}} */
