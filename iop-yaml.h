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

#include "file.h"
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

/** Convert an IOP-YAML structure contained in a file to an IOP C structure.
 *
 * This function read a file containing an IOP-YAML structure and set the
 * fields in an IOP C structure.
 *
 * This function cannot be used to unpack a class;
 * use `t_iop_yunpack_ptr_file` instead.
 *
 * \param[in]  filename The file name to read and parse.
 * \param[in]  st       The IOP structure description.
 * \param[out] out      Pointer on the IOP structure to write.
 * \param[out] subfiles List of unpacked subfiles.
 * \param[out] err   If the unpacking fails, this pointer is set to a
 *                   description of the error, allocated on the t_scope.
 */
/* FIXME: rename iop_json_subfile */
__must_check__
int t_iop_yunpack_file(const char * nonnull filename,
                       const iop_struct_t * nonnull st,
                       void * nonnull out,
                       qv_t(iop_json_subfile) * nullable subfiles,
                       sb_t * nonnull err);

/** Convert an IOP-YAML structure contained in a file to an IOP C structure.
 *
 * This function acts exactly as `t_iop_yunpack_file` but allocates
 * (or reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of
 * `t_iop_yunpack_file`, because the size of a class is not known before
 * unpacking it (this could be a child).
 */
__must_check__
int t_iop_yunpack_ptr_file(const char * nonnull filename,
                           const iop_struct_t * nonnull st,
                           void * nullable * nonnull out,
                           qv_t(iop_json_subfile) * nullable subfiles,
                           sb_t * nullable errb);

/* }}} */
/* {{{ Generating YAML */

enum iop_ypack_flags {
    IOP_YPACK_DEFAULT = 0,
};

typedef int (iop_ypack_writecb_f)(void * nonnull priv,
                                  const void * nonnull buf, int len);

/** Convert an IOP C structure to IOP-YAML.
 *
 * This function packs an IOP structure into YAML format.
 *
 * \param[in] st       IOP structure description.
 * \param[in] value    Pointer on the IOP structure to pack.
 * \param[in] writecb  Callback to call when writing (like iop_sb_write).
 * \param[in] priv     Private data to give to the callback.
 * \param[in] flags    Packer flags bitfield (see iop_ypack_flags).
 */
int iop_ypack(const iop_struct_t * nonnull st, const void * nonnull value,
              iop_ypack_writecb_f * nonnull writecb,
              void * nonnull priv, unsigned flags);

/** Serialize an IOP C structure in an IOP-YAML file.
 *
 * This function packs an IOP structure into YAML format and writes it in a
 * file.
 *
 * Some IOP sub-objects can be written in separate files using the include
 * feature. Only one level of inclusion is supported.
 *
 * \param[in]  filename   The file in which the value is packed.
 * \param[in]  file_flags The flags to use when opening the file
 *                        (\ref enum file_flags).
 * \param[in]  file_mode  The mode to use when opening the file.
 * \param[in]  st         IOP structure description.
 * \param[in]  value      Pointer on the IOP structure to pack.
 * \param[in]  subfiles   If set, this is the list of IOP objects that must be
 *                        written in separate files using @include.
 * \param[out] err        NULL or the buffer to use to write textual error.
 */
int _iop_ypack_file(const char * nonnull filename, unsigned file_flags,
                    mode_t file_mode, const iop_struct_t * nonnull st,
                    const void * nonnull value, unsigned flags,
                    const qv_t(iop_json_subfile) * nullable subfiles,
                    sb_t * nullable err);

/** Pack an IOP C structure to IOP-YAML in a sb_t.
 *
 * See iop_ypack().
 */
static inline int
iop_sb_ypack(sb_t * nonnull sb, const iop_struct_t * nonnull st,
             const void * nonnull value, unsigned flags)
{
    return iop_ypack(st, value, &iop_sb_write, sb, flags);
}

/* }}} */

MODULE_DECLARE(iop_yaml);

#endif
