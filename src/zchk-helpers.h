/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#ifndef PLATFORM_LIB_COMMON_ZCHK_HELPERS_H
#define PLATFORM_LIB_COMMON_ZCHK_HELPERS_H

#include <lib-common/core.h>

/** Load .pem test files.
 *
 * \param[in] libcommon_path path to lib-common, the .pem files are expected
 *                           to be in lib-common/test-data/keys directory.
 * \param[out] priv the buffer in which the private key will be loaded.
 * \param[out] priv_encrypted the buffer in which the encrypted private key
 *                            will be loaded.
 * \param[out] pub the buffer in which the public key will be loaded.
 */
void z_load_keys(lstr_t libcommon_path, sb_t *priv, sb_t *priv_encrypted,
                 sb_t *pub);

/** Z helper to run a command, check its status and capture its output.
 *
 *
 * \param[in]  file    \ref el_spawn_child_capture
 * \param[in]  argv    \ref el_spawn_child_capture
 * \param[in]  envp    \ref el_spawn_child_capture
 * \param[in]  timeout \ref el_spawn_child_capture
 * \param[in]  expected_status  expected return code of the command
 * \param[out] output  output of the command (stdout + stderr)
 */
int z_run_command(const char * nonnull file,
                  const char * nullable argv[],
                  const char * nullable envp[],
                  int timeout, int expected_status,
                  sb_t *out);

#endif /* PLATFORM_LIB_COMMON_ZCHK_HELPERS_H */
