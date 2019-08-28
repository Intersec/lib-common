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

#include "zchk-helpers.h"


void z_load_keys(lstr_t libcommon_path, sb_t *priv, sb_t *priv_encrypted,
                 sb_t *pub)
{
    /* To generate such test data, you can:
     * $ openssl genpkey -algorithm RSA -out priv.pem -pkeyopt
     *   rsa_keygen_bits:2048
     * $ openssl pkey -in priv.pem -pubout -out pub.pem
     * $ echo 'secret pioupiou23' > a
     * $ openssl pkeyutl -encrypt -pubin -inkey pub.pem -in a -hexdump
     */
    t_scope;

    if (sb_read_file(priv, t_fmt("%*pM/test-data/keys/priv.pem",
                                  LSTR_FMT_ARG(libcommon_path))) < 0)
    {
        e_fatal("cannot read private key");
    }
    if (sb_read_file(priv_encrypted,
                     t_fmt("%*pM/test-data/keys/priv.encrypted.pem",
                           LSTR_FMT_ARG(libcommon_path))) < 0)
    {
        e_fatal("cannot read encrypted private key");
    }
    if (sb_read_file(pub, t_fmt("%*pM/test-data/keys/pub.pem",
                                 LSTR_FMT_ARG(libcommon_path))) < 0)
    {
        e_fatal("cannot read public key");
    }
}
