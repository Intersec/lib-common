/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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
    const char *output_path;
    bool json_input;
    bool raw_mode;
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
pack_yaml(yaml_data_t * nonnull data,
          const yaml__document_presentation__t * nullable pres,
          sb_t * nonnull err)
{
    t_scope;
    yaml_pack_env_t *pack_env;
    int res = 0;
    unsigned flags = YAML_PACK_ALLOW_UNBOUND_VARIABLES;

    pack_env = t_yaml_pack_env_new();
    if (opts_g.raw_mode) {
        /* use an empty document presentation to prevent any presentation
         * data stored in the AST from being used. */
        yaml__document_presentation__t empty_pres;

        iop_init(yaml__document_presentation, &empty_pres);
        t_yaml_pack_env_set_presentation(pack_env, &empty_pres);
    } else
    if (pres) {
        t_yaml_pack_env_set_presentation(pack_env, pres);
    }

    if (opts_g.output_path) {
        yaml_pack_env_set_flags(pack_env, flags);
        res = t_yaml_pack_file(pack_env, opts_g.output_path, data, err);
    } else {
        flags |= YAML_PACK_NO_SUBFILES;
        yaml_pack_env_set_flags(pack_env, flags);
        res = t_yaml_pack(pack_env, data, yaml_pack_write_stdout, NULL, err);
        printf("\n");
    }

    return res;
}

static int
repack_yaml(const char * nullable filename, const iop_dso_t * nullable dso,
            const iop_struct_t * nullable st, sb_t * nonnull err)
{
    t_scope;
    yaml_parse_t *env;
    yaml_data_t data;
    lstr_t file = LSTR_NULL_V;
    int res = 0;
    unsigned flags = YAML_PARSE_GEN_PRES_DATA;

    if (!dso && !st) {
        /* no IOP validation, so variables can be unbound */
        flags |= YAML_PARSE_ALLOW_UNBOUND_VARIABLES;
    }

    env = t_yaml_parse_new(flags);
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

    res = pack_yaml(&data, NULL, err);

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
    void *value = NULL;
    yaml__document_presentation__t *pres;
    qv_t(iop_json_subfile) subfiles;
    iop_json_subfile__array_t subfiles_array;
    yaml_data_t data;
    int res = 0;

    t_qv_init(&subfiles, 0);

    if (filename) {
        RETHROW(t_iop_junpack_ptr_file(filename, st, &value, 0, &subfiles,
                                       err));
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

    subfiles_array = IOP_TYPED_ARRAY_TAB(iop_json_subfile, &subfiles);
    pres = t_build_yaml_pres_from_json_subfiles(&subfiles_array, st, value);

    t_iop_to_yaml_data(st, value, &data);
    res = pack_yaml(&data, pres, err);

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
    "When no output is specified, the input stream is reformated and ",
    "written on stdout. In that case, included subfiles are not recreated.",
    "If an output file is specified (`-o`), the whole document will be ",
    "written, including subfiles. It is a good idea to thus always output ",
    "in a subdirectory, to avoid writing subfiles everywhere.",
    "",
    "The whole document can also be written without any presentation ",
    "details. This will write the whole YAML AST without includes, ",
    "comments, etc."
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
    "# Convert an IOP-JSON input into a YAML document, and output it and ",
    "# all the included subfiles in a new directory",
    "$ yamlfmt -d iop.so -t pkg.MyStruct -j input.json -o out/doc.yml",
    "",
    "# Output the raw AST of a YAML document",
    "$ yamlfmt --raw doc.yml",
    "",
    NULL
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
        OPT_STR('o', "output", &opts_g.output_path,
                "Path to the output file"),
        OPT_FLAG('r', "raw", &opts_g.raw_mode,
                 "Format without any presentation details."),
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
