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

#ifndef IS_IOP_IOPC_IOPC_H
#define IS_IOP_IOPC_IOPC_H

#include <lib-common/container.h>

qm_kptr_ckey_t(iopc_env, char, char *, qhash_str_hash, qhash_str_equal);

/** Check that the name is valid to use as an IOP type
 */
int iopc_check_type_name(lstr_t name, sb_t * nullable err);

/** Check that the name is valid to use as an IOP field name
 */
int iopc_check_field_name(lstr_t name, sb_t * nullable err);

/** Specify the class id range used when building IOP DSO with iopc_dso_load.
 */
void iopc_dso_set_class_id_range(uint16_t class_id_min,
                                 uint16_t class_id_max);

/** Build an IOP DSO.
 *
 * \param[in] pfxdir       prefix directory of the IOP file to compile
 * \param[in] display_pfx  set to false if only the relative part of the files
 *                         should be printed in case of error
 * \param[in] iopfile      the IOP file to compile; this path must be relative
 *                         to \p pfxdir
 * \param[in] env          a map of buffered IOP files (dependencies); the paths
 *                         of these dependencies must be relative to \p pfxdir
 * \param[in] outdir       the absolute path of the directory to store the IOP DSO
 *                         file (outdir/pkgname.so)
 * \param[out] err         buffer filled in case of error
 *
 * \return             0 if ok, -1 if the build failed
 */
int iopc_dso_build(const char *pfxdir, bool display_pfx,
                   const char *iopfile, const qm_t(iopc_env) *env,
                   const char *outdir, sb_t *err);

MODULE_DECLARE(iopc_dso);
MODULE_DECLARE(iopc_lang_c);
MODULE_DECLARE(iopc);

#endif
