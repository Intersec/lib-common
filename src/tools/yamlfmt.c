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

#include <lib-common/yaml.h>
#include <lib-common/parseopt.h>

static struct {
    bool help;
} opts_g;

static int yaml_pack_write_stdout(void * nullable priv,
                                  const void * nonnull buf, int len,
                                  sb_t *err)
{
    return printf("%.*s", len, (const char *)buf);
}

static int
yaml_repack(lstr_t filename, sb_t * nonnull err)
{
    t_scope;
    const yaml_presentation_t *pres = NULL;
    yaml_parse_t *env;
    yaml_data_t data;
    lstr_t file = LSTR_NULL_V;
    int res = 0;

    env = t_yaml_parse_new();
    if (filename.s) {
        if (t_yaml_parse_attach_file(env, filename, LSTR_NULL_V, err) < 0) {
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

    if (t_yaml_parse(env, &data, &pres, err) >= 0) {
        res = yaml_pack(&data, pres, yaml_pack_write_stdout, NULL, err);
        printf("\n");
    }

  end:
    lstr_wipe(&file);
    yaml_parse_delete(&env);
    return res;
}

int main(int argc, char **argv)
{
    lstr_t filename = LSTR_NULL_V;
    const char *arg0;
    popt_t options[] = {
        OPT_FLAG('h', "help", &opts_g.help, "show help"),
        OPT_END(),
    };
    SB_1k(err);

    arg0 = NEXTARG(argc, argv);
    argc = parseopt(argc, argv, options, 0);
    if (opts_g.help) {
        makeusage(!opts_g.help, arg0, "[<file>]", NULL, options);
    }

    if (argc >= 1) {
        filename = LSTR(NEXTARG(argc, argv));
    }
    if (yaml_repack(filename, &err) < 0) {
        fprintf(stderr, "%.*s\n", err.len, err.data);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
