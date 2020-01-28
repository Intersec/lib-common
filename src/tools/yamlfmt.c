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
#include <lib-common/iop-json.h>
#include <lib-common/iop-yaml.h>
#include <lib-common/parseopt.h>

static struct {
    const char *dso_path;
    const char *type_name;
    bool json_input;
    bool help;
} opts_g;

static int yaml_pack_write_stdout(void * nullable priv,
                                  const void * nonnull buf, int len,
                                  sb_t *err)
{
    return printf("%.*s", len, (const char *)buf);
}

static const iop_struct_t * nullable
get_iop_type(const iop_dso_t * nonnull dso, const lstr_t name,
             sb_t * nonnull err)
{
    const iop_struct_t *st;

    st = iop_dso_find_type(dso, name);
    if (!st) {
        sb_setf(err, "unknown IOP type `%pL`", &name);
        return NULL;
    }

    return st;
}


static int
t_parse_yaml(yaml_parse_t *env, const iop_dso_t * nullable dso,
             const iop_struct_t * nullable st, yaml_data_t * nonnull data,
             sb_t * nonnull err)
{
    RETHROW(t_yaml_parse(env, data, err));

    if (dso && !st) {
        if (!data->tag.s) {
            sb_setf(err, "document should start with a tag equals to the "
                    "fullname of the IOP type serialized");
            return -1;
        }
        st = RETHROW_PN(get_iop_type(dso, data->tag, err));
    }

    if (st) {
        void *out = NULL;

        RETHROW(t_iop_yunpack_ptr_yaml_data(data, st, &out, 0, err));
    }

    return 0;
}

static int
repack_yaml(const char * nullable filename, const iop_dso_t * nullable dso,
            const iop_struct_t * nullable st, sb_t * nonnull err)
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

    if (t_parse_yaml(env, dso, st, &data, err) < 0) {
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

static int
repack_json(const char * nullable filename, const iop_struct_t * nonnull st,
            sb_t * nonnull err)
{
    t_scope;
    lstr_t file = LSTR_NULL_V;
    int res = 0;
    void *value = NULL;
    SB_1k(buf);

    if (filename) {
        RETHROW(t_iop_junpack_ptr_file(filename, st, &value, 0, NULL, err));
    } else {
        pstream_t ps;

        if (lstr_init_from_fd(&file, 0, PROT_READ, MAP_SHARED) < 0) {
            sb_setf(err, "cannot read from stdin: %m");
            res = -1;
            goto end;
        }
        ps = ps_initlstr(&file);
        if (t_iop_junpack_ptr_ps(&ps, st, &value, 0, err) < 0) {
            res = -1;
            goto end;
        }
    }

    t_iop_sb_ypack(&buf, st, value, NULL);
    printf("%s\n", buf.data);

  end:
    lstr_wipe(&file);
    return res;
}

static int
parse_and_repack(const char * nullable filename,
                 const iop_dso_t * nullable dso, sb_t * nonnull err)
{
    const iop_struct_t *st = NULL;

    if (dso && opts_g.type_name) {
        st = RETHROW_PN(get_iop_type(dso, LSTR(opts_g.type_name), err));
    }

    if (opts_g.json_input) {
        return repack_json(filename, st, err);
    } else {
        return repack_yaml(filename, dso, st, err);
    }
}

static const char *description[] = {
    "Validate & reformat a YAML document.",
    "",
    "If a file is not provided, the input is read from stdin.",
    "",
    "If an IOP dso is provided, the input will be validated as a serialized ",
    "IOP struct. The IOP type can be provided with the `-t` option. If not ",
    "provided, and the input is in YAML, the document must start with the ",
    "name of the IOP type as a tag.",
    "",
    "The input can be provided in JSON, using the `-j` flag. Both a DSO ",
    "path and an IOP type name are required in that case.",
    "",
    "Here are a few examples:",
    "",
    "# reformat the input",
    "$ yamlfmt <input.yml ",
    "",
    "# validate an IOP-YAML input with the type provided in the file",
    "$ yamlfmt -d iop.so input.yml",
    "",
    "# validate an IOP-YAML input with an explicit type",
    "$ yamlfmt -d iop.so -t pkg.MyStruct input.yml",
    "",
    "# Convert an IOP-JSON input into a YAML document",
    "$ yamlfmt -d iop.so -t pkg.MyStruct -j input.json",
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
        OPT_FLAG('j', "json", &opts_g.json_input, "Unpack the input as JSON"),
        OPT_STR('t', "type", &opts_g.type_name, "Name of the IOP type"),
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

    if (opts_g.json_input && (!opts_g.dso_path || !opts_g.type_name)) {
        fprintf(stderr, "both `-d` and `-t`` are required with JSON input");
        return EXIT_FAILURE;
    }

    if (opts_g.dso_path) {
        dso = iop_dso_open(opts_g.dso_path, LM_ID_BASE, &err);
        if (!dso) {
            fprintf(stderr, "cannot open dso `%s`: %pL", opts_g.dso_path,
                    &err);
            return EXIT_FAILURE;
        }
    }

    if (parse_and_repack(filename, dso, &err) < 0) {
        fprintf(stderr, "%.*s\n", err.len, err.data);
        ret = EXIT_FAILURE;
    }

    iop_dso_close(&dso);
    return ret;
}
