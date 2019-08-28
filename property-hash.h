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

#ifndef IS_LIB_COMMON_PROPERTY_HASH_H
#define IS_LIB_COMMON_PROPERTY_HASH_H

#include "container-qhash.h"
#include "xmlpp.h"

qm_k64_t(proph, char *);

typedef struct props_hash_t {
    char *name;
    qm_t(proph) h;
    qh_t(str)  *names;
} props_hash_t;

/****************************************************************************/
/* Create hashtables, update records                                        */
/****************************************************************************/

static inline props_hash_t *props_hash_init(props_hash_t *ph, qh_t(str) *names)
{
    p_clear(ph, 1);
    qm_init(proph, &ph->h);
    ph->names = names;
    return ph;
}
static inline props_hash_t *props_hash_new(qh_t(str) *names)
{
    return props_hash_init(p_new_raw(props_hash_t, 1), names);
}
props_hash_t *props_hash_dup(const props_hash_t *);
void props_hash_wipe(props_hash_t *ph);
GENERIC_DELETE(props_hash_t, props_hash);

void props_hash_update(props_hash_t *ph, const char *name, const char *value);
void props_hash_remove(props_hash_t *ph, const char *name);
void props_hash_merge(props_hash_t *, const props_hash_t *);

/****************************************************************************/
/* Search in props_hashes                                                   */
/****************************************************************************/

const char *props_hash_findval(const props_hash_t *ph, const char *name, const char *def);
static inline const char *props_hash_find(const props_hash_t *ph, const char *name)
{
    return props_hash_findval(ph, name, NULL);
}

int props_hash_findval_int(const props_hash_t *ph, const char *name, int defval);
bool props_hash_findval_bool(const props_hash_t *ph, const char *name, bool defval);

/****************************************************************************/
/* Serialize props_hashes                                                   */
/****************************************************************************/

void props_hash_to_conf(sb_t *out, const props_hash_t *ph);
void props_hash_to_xml(xmlpp_t *pp, const props_hash_t *ph);

/****************************************************************************/
/* Unserialize props_hashes                                                 */
/****************************************************************************/

int props_hash_from_fmtv1_data_start(props_hash_t *ph, const void *data,
                                     int len, int start);
int props_hash_from_fmtv1_data(props_hash_t *ph, const void *data, int len);
int props_hash_from_fmtv1(props_hash_t *ph, const sb_t *payload);
int props_hash_from_fmtv1_len(props_hash_t *ph, const sb_t *payload,
                              int begin, int end);

#endif /* IS_LIB_COMMON_PROPERTY_H */
