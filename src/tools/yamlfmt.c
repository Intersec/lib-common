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

#include <lib-common/iop.h>
#include <lib-common/iop-yaml.h>
#include <lib-common/parseopt.h>

static struct {
    const char *dso_path;
    bool help;
} opts_g;

static int yaml_pack_write_stdout(void * nullable priv,
                                  const void * nonnull buf, int len,
                                  sb_t *err)
{
    return printf("%.*s", len, (const char *)buf);
}

static const iop_struct_t * nullable
retrieve_iop_type(const iop_dso_t * nonnull dso, const lstr_t tag,
                  sb_t * nonnull err)
{
    const iop_struct_t *st;

    if (!tag.s) {
        sb_setf(err, "document should start with a tag equals to the "
                "fullname of the IOP type serialized");
        return NULL;
    }

    st = iop_dso_find_type(dso, tag);
    if (!st) {
        sb_setf(err, "unknown IOP type `%pL`", &tag);
        return NULL;
    }

    return st;
}


static int
parse_yaml(yaml_parse_t *env, const iop_dso_t * nullable dso,
           yaml_data_t * nonnull data, sb_t * nonnull err)
{
    RETHROW(t_yaml_parse(env, data, err));

    if (dso) {
        const iop_struct_t *st;
        void *out = NULL;

        st = RETHROW_PN(retrieve_iop_type(dso, data->tag, err));
        RETHROW(t_iop_yunpack_ptr_yaml_data(data, st, &out, 0, err));
    }

    return 0;
}

static int
repack_yaml(const char * nullable filename, const iop_dso_t * nullable dso,
            sb_t * nonnull err)
{
    t_scope;
    yaml_pack_env_t *pack_env;
    yaml_parse_t *env;
    yaml_data_t data;
    lstr_t file = LSTR_NULL_V;
    int res = 0;

    env = t_yaml_parse_new(YAML_PARSE_GEN_PRES_DATA);
    if (filename) {
        if (t_yaml_parse_attach_file(env, filename, NULL, err) < 0) {
            res = -1;
            goto end;
        }
    } else {
        if (lstr_init_from_fd(&file, 0, PROT_READ, MAP_SHARED) < 0) {
            sb_setf(err, "cannot read from stdin: %m");
            res = -1;
            goto end;
        }
        yaml_parse_attach_ps(env, ps_initlstr(&file));
    }

    if (parse_yaml(env, dso, &data, err) < 0) {
        res = -1;
        goto end;
    }

    pack_env = t_yaml_pack_env_new();
    yaml_pack_env_set_flags(pack_env, YAML_PACK_NO_SUBFILES);
    res = t_yaml_pack(pack_env, &data, yaml_pack_write_stdout, NULL, err);
    printf("\n");

  end:
    lstr_wipe(&file);
    yaml_parse_delete(&env);
    return res;
}

static const char *description[] = {
    "Validate & reformat a YAML document.",
    "",
    "If a file is not provided, the input is read from stdin.",
    "If an IOP dso is provided, and the document starts with an IOP tag, ",
    "the document will be validated with this IOP type.",
    "",
    "Here are a few examples:",
    "",
    "$ yamlfmt <input.yml # reformat the input",
    "",
    "$ yamlfmt -d iop.so input.yml # validate an IOP-YAML input",
    "",
};

int main(int argc, char **argv)
{
    iop_dso_t *dso = NULL;
    const char *filename = NULL;
    const char *arg0;
    popt_t options[] = {
        OPT_FLAG('h', "help", &opts_g.help, "show help"),
        OPT_STR('d', "dso", &opts_g.dso_path, "Path to IOP dso file"),
        OPT_END(),
    };
    SB_1k(err);
    int ret = EXIT_SUCCESS;

    arg0 = NEXTARG(argc, argv);
    argc = parseopt(argc, argv, options, 0);
    if (opts_g.help) {

        makeusage(!opts_g.help, arg0, "[<file>]", description, options);
    }

    if (argc >= 1) {
        filename = NEXTARG(argc, argv);
    }

    if (opts_g.dso_path) {
        dso = iop_dso_open(opts_g.dso_path, LM_ID_BASE, &err);
        if (!dso) {
            fprintf(stderr, "cannot open dso `%s`: %pL", opts_g.dso_path,
                    &err);
            return EXIT_FAILURE;
        }
    }

    if (repack_yaml(filename, dso, &err) < 0) {
        fprintf(stderr, "%.*s\n", err.len, err.data);
        ret = EXIT_FAILURE;
    }

    iop_dso_close(&dso);
    return ret;
}
