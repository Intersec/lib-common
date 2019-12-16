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
                                  const void * nonnull buf, int len)
{
    return printf("%.*s", len, (const char *)buf);
}

static int
yaml_repack(const char * nullable filename, sb_t * nonnull err)
{
    t_scope;
    const yaml_presentation_t *pres = NULL;
    yaml_parse_t *env;
    yaml_data_t data;
    lstr_t file;
    pstream_t ps;

    if (filename) {
        if (lstr_init_from_file(&file, filename, PROT_READ, MAP_SHARED) < 0) {
            sb_setf(err, "cannot read file %s: %m", filename);
            return -1;
        }
    } else {
        if (lstr_init_from_fd(&file, 0, PROT_READ, MAP_SHARED) < 0) {
            sb_setf(err, "cannot read from stdin: %m");
            return -1;
        }
    }

    env = t_yaml_parse_new();
    ps = ps_initlstr(&file);
    if (t_yaml_parse_ps(env, ps, &data, &pres, err) < 0) {
        lstr_wipe(&file);
        yaml_parse_delete(&env);
        return -1;
    }

    yaml_pack(&data, pres, yaml_pack_write_stdout, NULL);
    printf("\n");

    lstr_wipe(&file);
    yaml_parse_delete(&env);
    return 0;
}

int main(int argc, char **argv)
{
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

    if (yaml_repack(argc >= 1 ? NEXTARG(argc, argv) : NULL, &err) < 0) {
        fprintf(stderr, "%.*s\n", err.len, err.data);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
