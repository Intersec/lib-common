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

#ifndef IS_LIB_COMMON_PROPERTY_H
#define IS_LIB_COMMON_PROPERTY_H

#include "container-qvector.h"

typedef struct property_t {
    char *name;
    char *value;
} property_t;

static inline void property_wipe(property_t *property) {
    p_delete(&property->name);
    p_delete(&property->value);
}
GENERIC_NEW_INIT(property_t, property);
GENERIC_DELETE(property_t, property);
qvector_t(props, property_t *);

const char *
property_findval(const qv_t(props) *arr, const char *k, const char *def);

int props_from_fmtv1_cstr(const char *buf, qv_t(props) *props);

#endif /* IS_LIB_COMMON_PROPERTY_H */
