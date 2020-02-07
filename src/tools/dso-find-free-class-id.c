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
#include <lib-common/parseopt.h>
#include <sysexits.h>

static struct find_class_id_g {
    int help;
} find_class_id_g = {
#define _G find_class_id_g
    .help = 0,
};

static const char *usage_g[] = {
    "Loads a DSO, and finds the first available (non-used) class-id in the",
    "family of a given class.",
    "",
    "<dso_path>:       path to the DSO to open",
    "<class_id_range>: authorized class id range, in format <min>-<max>",
    "<class_name>:     IOP fullname of any class in the wanted hierarchy;",
    "                  It can be the parent of the class to add, the root",
    "                  class, or any other class of the same family",
    NULL
};

static struct popt_t popt_g[] = {
    OPT_GROUP("Options:"),
    OPT_FLAG('h', "help", &_G.help, "show this help"),
    OPT_END(),
};

static iop_dso_t *open_dso(const char *dso_path)
{
    SB_1k(err);
    iop_dso_t *dso;

    if (!(dso = iop_dso_open(dso_path, LM_ID_BASE, &err))) {
        printf("cannot to load `%s`: %*pM\n", dso_path, SB_FMT_ARG(&err));
    }

    return dso;
}

static int
parse_class_id_range(const char *class_id_range, int *min, int *max)
{
    pstream_t ps = ps_initstr(class_id_range);

    if (!ps_len(&ps)) {
        return -1;
    }

    errno = 0;
    *min = ps_geti(&ps);
    THROW_ERR_IF(errno != 0 || *min < 0);

    RETHROW(ps_skipc(&ps, '-'));

    *max = ps_geti(&ps);
    THROW_ERR_IF(errno != 0 || *max < *min || *max > UINT16_MAX);

    return 0;
}

int main(int argc, char *argv[])
{
    iop_dso_t *dso;
    const char *arg0 = NEXTARG(argc, argv);
    int class_id_min;
    int class_id_max;
    const char *fullname;
    const iop_obj_t *obj;
    int ret = EX_DATAERR;

    argc = parseopt(argc, argv, popt_g, 0);
    if (argc != 3 || _G.help) {
        makeusage(_G.help ? EX_OK : EX_USAGE, arg0,
                  "<dso_path> <class_id_range> <class_name>",
                  usage_g, popt_g);
    }

    dso = open_dso(NEXTARG(argc, argv));
    if (!dso) {
        goto end;
    }

    if (parse_class_id_range(NEXTARG(argc, argv),
                             &class_id_min, &class_id_max) < 0)
    {
        printf("invalid class id range\n");
        goto end;
    }

    fullname = NEXTARG(argc, argv);
    if (!(obj = iop_get_obj(LSTR(fullname)))) {
        printf("cannot find IOP object `%s`\n", fullname);
        goto end;
    }
    if (obj->type != IOP_OBJ_TYPE_ST || !iop_struct_is_class(obj->desc.st)) {
        printf("IOP object `%s` is not a class\n", fullname);
        goto end;
    }

    for (int i = class_id_min; i <= class_id_max; i++) {
        if (!iop_get_class_by_id(obj->desc.st, i)) {
            printf("first available class id in the family of `%s` is %d\n",
                   fullname, i);
            ret = 0;
            goto end;
        }
    }

    printf("no available class id found in the family of `%s`\n", fullname);

  end:
    iop_dso_close(&dso);
    return ret;
}
