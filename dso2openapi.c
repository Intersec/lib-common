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
#include "iop.h"
#include "parseopt.h"

static struct {
    const char *dso_path;
    const char *title;
    const char *version;
    const char *description;
} opts_g;

static iop_dso_t *
handle_args(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);
    bool help = false;
    iop_dso_t *dso;
    SB_1k(err);
    popt_t options[] = {
        OPT_FLAG('h', "help", &help, "show help"),
        OPT_STR('d', "dso", &opts_g.dso_path, "path to IOP dso file"),
        OPT_STR(0, "description", &opts_g.description,
                "Add a description of the openapi app"),
        OPT_END(),
    };

    argc = parseopt(argc, argv, options, 0);
    if (argc < 2 || help) {
        makeusage(!help, arg0, "<name> <version>", NULL, options);
    }

    opts_g.title = NEXTARG(argc, argv);
    opts_g.version = NEXTARG(argc, argv);

    if (!opts_g.dso_path) {
        e_error("A dso file must be provided");
        return NULL;
    }

    dso = iop_dso_open(opts_g.dso_path, LM_ID_BASE, &err);
    if (!dso) {
        e_error("cannot open dso `%s`: %pL", opts_g.dso_path, &err);
        return NULL;
    }

    return dso;
}

static int yaml_pack_write_stdout(void * nullable priv,
                                  const void * nonnull buf, int len)
{
    return printf("%.*s", len, (const char *)buf);
}

int main(int argc, char **argv)
{
    t_scope;
    iop_dso_t *dso;
    iop_openapi_t *oa;
    yaml_data_t yaml;

    dso = handle_args(argc, argv);
    if (!dso) {
        return -1;
    }

    oa = t_new_iop_openapi(LSTR(opts_g.title), LSTR(opts_g.version),
                           opts_g.description ? LSTR(opts_g.description)
                                              : LSTR_NULL_V);

    qm_for_each_value(iop_mod, mod, &dso->mod_h) {
        t_iop_openapi_add_module(oa, mod);
    }

    t_iop_openapi_to_yaml(oa, &yaml);
    yaml_pack(&yaml, yaml_pack_write_stdout, NULL);
    printf("\n");

    iop_dso_close(&dso);
    return 0;
}
