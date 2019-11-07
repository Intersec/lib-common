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

#ifndef IS_LIB_COMMON_IOP_YAML_H
#define IS_LIB_COMMON_IOP_YAML_H

#include "iop-json.h"
#include "core.iop.h"

/* {{{ Parsing */

/** Convert IOP-YAML to an IOP C structure using the t_pool().
 *
 * This function allow to unpack an IOP structure encoded in YAML format in
 * one call. This is equivalent to:
 *
 * This function cannot be used to unpack a class; use `t_iop_yunpack_ptr_ps`
 * instead.
 *
 * \param[in]  ps    The pstream_t to parse.
 * \param[in]  st    The IOP structure description.
 * \param[out] out   Pointer on the IOP structure to write.
 * \param[out] err   If the unpacking fails, this pointer is set to a
 *                   description of the error, allocated on the t_scope.
 */
__must_check__
int t_iop_yunpack_ps(pstream_t * nonnull ps, const iop_struct_t * nonnull st,
                     void * nonnull out, sb_t * nonnull out_err);

/** Convert IOP-YAML to an IOP C structure using the t_pool().
 *
 * This function acts exactly as `t_iop_yunpack_ps` but allocates
 * (or reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of `t_iop_yunpack_ps`,
 * because the size of a class is not known before unpacking it (this could be
 * a child).
 */
__must_check__
int t_iop_yunpack_ptr_ps(pstream_t * nonnull ps,
                         const iop_struct_t * nonnull st,
                         void * nullable * nonnull out,
                         sb_t * nonnull out_err);

/** Convert a YAML file into an IOP C structure using the t_pool().
 *
 * See t_iop_junpack_ps.
 */
__must_check__
int t_iop_yunpack_file(const char * nonnull filename,
                       const iop_struct_t * nonnull st,
                       void * nullable * nonnull out,
                       sb_t * nonnull out_err);

/** Convert a YAML file into an IOP C structure using the t_pool().
 *
 * See t_iop_junpack_ptr_ps.
 */
__must_check__
int t_iop_yunpack_ptr_file(const char * nonnull filename,
                           const iop_struct_t * nonnull st,
                           void * nullable * nonnull out,
                           sb_t * nonnull out_err);

/* }}} */
/* {{{ Generating YAML */

/** Convert an IOP C structure to IOP-YAML.
 *
 * See iop_ypack. This function can be used to provide specific packing flags.
 * *DO NOT USE THIS*. Use iop_ypack instead.
 */
int iop_sb_ypack_with_flags(sb_t * nonnull sb,
                            const iop_struct_t * nonnull st,
                            const void * nonnull value, unsigned flags);

/** Pack an IOP C structure to IOP-YAML in a sb_t.
 *
 * See iop_ypack().
 */
int iop_sb_ypack(sb_t * nonnull sb, const iop_struct_t * nonnull st,
                 const void * nonnull value);

/** Pack an IOP C structure in an IOP-YAML file.
 *
 * \param[in]  filename   The file in which the value is packed.
 * \param[in]  file_flags The flags to use when opening the file
 *                        (\ref enum file_flags).
 * \param[in]  file_mode  The mode to use when opening the file.
 * \param[in]  st         IOP structure description.
 * \param[in]  value      Pointer on the IOP structure to pack.
 * \param[out] err        Buffer filled in case of error.
 */
int iop_ypack_file(const char * nonnull filename, unsigned file_flags,
                   mode_t file_mode, const iop_struct_t * nonnull st,
                   const void * nonnull value, sb_t * nonnull err);

#define iop_ypack_file(filename, st, value, err)                             \
    (iop_ypack_file)((filename), FILE_WRONLY | FILE_CREATE | FILE_TRUNC,     \
                     0644, (st), (value), (err))
/* }}} */

MODULE_DECLARE(iop_yaml);

#endif
