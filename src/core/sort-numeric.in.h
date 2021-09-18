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

#ifndef IS_LIB_COMMON_SORT_H
# error "you must include sort.h instead"
#endif

static inline size_t (bisect)(type_t what, const type_t data[], size_t len,
                              bool *found)
{
    size_t l = 0, r = len;

    while (l < r) {
        size_t i = (l + r) / 2;

        if (what == data[i]) {
            if (found) {
                *found = true;
            }
            return i;
        }
        if (what < data[i]) {
            r = i;
        } else {
            l = i + 1;
        }
    }
    if (found) {
        *found = false;
    }
    return r;
}

static inline bool (contains)(type_t what, const type_t data[], size_t len)
{
    size_t l = 0, r = len;

    while (l < r) {
        size_t i = (l + r) / 2;

        if (what == data[i])
            return true;
        if (what < data[i]) {
            r = i;
        } else {
            l = i + 1;
        }
    }
    return false;
}

#undef type_t
#undef bisect
#undef contains
