/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

/* This is the default implementation of lstr_obfuscate. It is a naïve one,
 * that could be overridden using 'ctx.lstr_obfuscate_src' at waf configure.
 */

#include <lib-common/core.h>

void lstr_obfuscate(lstr_t in, uint64_t key, lstr_t out)
{
    t_scope;
    lstr_t key_str = t_lstr_fmt("%ju", key);

    lstr_xor(in, key_str, out);
}
