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

#include <lib-common/iop-openapi.h>
#include <lib-common/iop.h>
#include <lib-common/parseopt.h>

static struct {
    bool help;
    const char *dso_path;
    const char *whitelist_path;
    const char *title;
    const char *version;
    const char *route;
    const char *description;
    const char *module;
} opts_g;

static iop_dso_t *
handle_args(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);
    iop_dso_t *dso;
    SB_1k(err);
    popt_t options[] = {
        OPT_FLAG('h', "help", &opts_g.help, "show help"),
        OPT_STR('d', "dso", &opts_g.dso_path, "path to IOP dso file"),
        OPT_STR('m', "module", &opts_g.module,
                "fullname of the IOP module to use"),
        OPT_STR('w', "whitelist", &opts_g.whitelist_path,
                "path to the RPCs whitelist file"),
        OPT_STR(0, "description", &opts_g.description,
                "Add a description of the openapi app"),
        OPT_END(),
    };

    argc = parseopt(argc, argv, options, 0);
    if (argc < 3 || opts_g.help) {
        makeusage(!opts_g.help, arg0, "<name> <version> <route>", NULL,
                  options);
    }

    opts_g.title = NEXTARG(argc, argv);
    opts_g.version = NEXTARG(argc, argv);
    opts_g.route = NEXTARG(argc, argv);

    if (!opts_g.dso_path) {
        e_error("A dso file must be provided");
        return NULL;
    }
    if (!opts_g.module) {
        e_error("The name of the IOP module to use must be provided");
        return NULL;
    }

    dso = iop_dso_open(opts_g.dso_path, LM_ID_BASE, &err);
    if (!dso) {
        e_error("cannot open dso `%s`: %pL", opts_g.dso_path, &err);
        return NULL;
    }

    return dso;
}

static const iop_mod_t * nullable
get_iop_module(const iop_dso_t * nonnull dso)
{
    lstr_t wanted_module = LSTR(opts_g.module);
    const iop_mod_t *module = NULL;

    qm_for_each_value(iop_mod, mod, &dso->mod_h) {
        if (lstr_equal(mod->fullname, wanted_module)) {
            module = mod;
            break;
        }
    }

    if (!module) {
        e_error("Could not find the IOP module `%pL` in the DSO. Here are "
                "the available modules:", &wanted_module);
        qm_for_each_key(iop_mod, mod_name, &dso->mod_h) {
            e_error("  `%pL`", &mod_name);
        }
    }

    return module;
}

static int
t_whitelist_rpcs(iop_openapi_t *oa)
{
    FILE *file;
    SB_1k(sb);
    int res;

    if (!opts_g.whitelist_path) {
        return 0;
    }

    file = fopen(opts_g.whitelist_path, "r");
    if (!file) {
        e_error("cannot open whitelist file `%s`: %m", opts_g.whitelist_path);
        return -1;
    }

    while ((res = sb_getline(&sb, file)) > 0) {
        sb.len--;
        t_iop_openapi_whitelist_rpc(oa, LSTR_SB_V(&sb));
        sb_reset(&sb);
    }
    if (res < 0) {
        e_error("error while reading whitelist file `%s`: %m",
                opts_g.whitelist_path);
        return -1;
    }

    return 0;
}

static int yaml_pack_write_stdout(void * nullable priv,
                                  const void * nonnull buf, int len,
                                  sb_t *err)
{
    return printf("%.*s", len, (const char *)buf);
}

static int
generate_openapi(const iop_mod_t * nonnull module)
{
    t_scope;
    iop_openapi_t *oa;
    yaml_data_t yaml;
    yaml_pack_env_t *env;
    SB_1k(err);

    oa = t_new_iop_openapi(LSTR(opts_g.title), LSTR(opts_g.version),
                           module, LSTR(opts_g.route));
    if (opts_g.description) {
        t_iop_openapi_set_description(oa, LSTR(opts_g.description));
    }

    RETHROW(t_whitelist_rpcs(oa));

    if (t_iop_openapi_to_yaml(oa, &yaml, &err) < 0) {
        e_error("could not generate the OpenAPI application: %pL", &err);
        return -1;
    }

    env = t_yaml_pack_env_new();
    t_yaml_pack(env, &yaml, NULL, yaml_pack_write_stdout, NULL, &err);
    printf("\n");

    return 0;
}

int main(int argc, char **argv)
{
    const iop_mod_t *module;
    iop_dso_t *dso;
    int ret = 0;

    dso = handle_args(argc, argv);
    if (!dso) {
        return -1;
    }

    module = get_iop_module(dso);
    if (!module || generate_openapi(module) < 0) {
        ret = -1;
    }

    iop_dso_close(&dso);
    return ret;
}
