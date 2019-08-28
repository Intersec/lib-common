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

#include "property.h"

const char *
property_findval(const qv_t(props) *arr, const char *k, const char *def)
{
    int i;

    for (i = 0; i < arr->len; i++) {
        property_t *prop = arr->tab[i];
        if (!strcasecmp(k, prop->name)) {
            return prop->value ? prop->value : def;
        }
    }

    return def;
}

/* OG: should take buf+len with len<0 for strlen */
int props_from_fmtv1_cstr(const char *buf, qv_t(props) *props)
{
    int pos = 0;
    int len = strlen(buf);

    while (pos < len) {
        const char *k, *v, *end;
        int klen, vlen;
        property_t *prop;

        k    = skipblanks(buf + pos);
        klen = strcspn(k, " \t:");

        v    = skipblanks(k + klen);
        if (*v != ':')
            return -1;
        v    = skipblanks(v + 1);
        end  = strchr(v, '\n');
        if (!end)
            return -1;
        vlen = end - v;
        while (vlen > 0 && isspace((unsigned char)v[vlen - 1]))
            vlen--;

        prop = property_new();
        prop->name  = p_dupz(k, klen);
        prop->value = p_dupz(v, vlen);
#if 0   // XXX: NULL triggers Segfaults in user code :(
        prop->value = vlen ? p_dupz(v, vlen) : NULL;
#endif
        qv_append(props, prop);

        pos = end + 1 - buf;
    }

    return 0;
}
