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

#include <lib-common/log-iop.h>

__unused__
static void log_iop_static_checks(void)
{
    /* This code statically checks that some assumptions done in log.c (which
     * cannot use LOG_LEVEL IOP enumeration) are correct. */
    STATIC_ASSERT (LOG_LEVEL_ERR     == LOG_ERR);
    STATIC_ASSERT (LOG_LEVEL_CRIT    == LOG_CRIT);
    STATIC_ASSERT (LOG_LEVEL_DEFAULT == -2);
}

/* {{{ Configuration */

void logger_configure(const core__log_configuration__t *conf)
{
    logger_set_level(LSTR_EMPTY_V, conf->root_level,
                     LOG_MK_FLAGS(conf->force_all, conf->is_silent));

    tab_for_each_ptr(l, &conf->specific) {
        logger_set_level(l->full_name, l->level,
                         LOG_MK_FLAGS(l->force_all, l->is_silent));
    }
}

void IOP_RPC_IMPL(core__core, log, set_root_level)
{
    unsigned flags = LOG_MK_FLAGS(arg->force_all, arg->is_silent);

    ic_reply(ic, slot, core__core, log, set_root_level,
             .level = logger_set_level(LSTR_EMPTY_V, arg->level, flags));
}

void IOP_RPC_IMPL(core__core, log, reset_root_level)
{
    ic_reply(ic, slot, core__core, log, reset_root_level,
             .level = logger_reset_level(LSTR_EMPTY_V));
}

void IOP_RPC_IMPL(core__core, log, set_logger_level)
{
    unsigned flags = LOG_MK_FLAGS(arg->force_all, arg->is_silent);

    ic_reply(ic, slot, core__core, log, set_logger_level,
             .level = logger_set_level(arg->full_name, arg->level, flags));
}

void IOP_RPC_IMPL(core__core, log, reset_logger_level)
{
    ic_reply(ic, slot, core__core, log, reset_logger_level,
             .level = logger_reset_level(arg->full_name));
}

/* }}} */
/* {{{ Accessors */

static void
get_configurations_recursive(logger_t *logger, lstr_t prefix,
                             qv_t(logger_conf) *res)
{
    logger_t *child;
    core__logger_configuration__t conf;

    /* called first as it can force the update of several parameters
     * including the full name (calling __logger_refresh) */
    iop_init(core__logger_configuration, &conf);

    /* Don't use logger_get_level since it takes the update_lock */
    __logger_do_refresh(logger);
    conf.level = MAX(logger->level, LOG_CRIT);

    /* check if the first element in the full name is the prefix */
    if (lstr_startswith(logger->full_name, prefix)) {
        conf.full_name = lstr_dupc(logger->full_name);
        conf.force_all = logger->level_flags & LOG_FORCED;
        conf.is_silent = logger->level_flags & LOG_SILENT;

        qv_append(res, conf);

        /* all children will have the same prefix. No need to check it
         * anymore, we set it to null */
        prefix = LSTR_NULL_V;
    }

    dlist_for_each_entry(child, &logger->children, siblings) {
        get_configurations_recursive(child, prefix, res);
    }
}

void logger_get_all_configurations(lstr_t prefix, qv_t(logger_conf) *confs)
{
    log_spin_lock();
    get_configurations_recursive(logger_get_root(), prefix, confs);
    log_spin_unlock();
}

void IOP_RPC_IMPL(core__core, log, list_loggers)
{
    t_scope;
    qv_t(logger_conf) confs;

    t_qv_init(&confs, 1024);

    logger_get_all_configurations(arg->prefix, &confs);

    ic_reply(ic, slot, core__core, log, list_loggers,
             .loggers = IOP_ARRAY_TAB(&confs));
}

/* }}} */
