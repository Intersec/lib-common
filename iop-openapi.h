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

#ifndef IS_LIB_COMMON_IOP_OPENAPI_H
#define IS_LIB_COMMON_IOP_OPENAPI_H

#include "yaml.h"
#include "iop.h"

typedef struct iop_openapi_t iop_openapi_t;

/** Create a new IOP OpenAPI application.
 *
 * This object can be used to add types, RPCs, interfaces to the
 * application. Then, a YAML OpenAPI description of the application
 * can be generated.
 *
 * \param[in]  title  The title of the application
 * \param[in]  version The version the application, must be a semver string.
 * \param[in]  module  The IOP module used for the OpenAPI application.
 * \param[in]  route  The route name. Every RPCs will be exposed in the route
 *                    /<route>/<iface_alias>/<rpc_name>.
 * \return An IOP OpenAPI application.
 */
iop_openapi_t * nonnull
t_new_iop_openapi(const lstr_t title, const lstr_t version,
                  const iop_mod_t * nullable mod, const lstr_t route);

void
t_iop_openapi_set_description(iop_openapi_t * nonnull oa,
                              const lstr_t description);

/** Whitelist an RPC in the IOP OpenaAPI application.
 *
 * Only RPCs that have been whitelisted will be exposed in the OpenAPI
 * description.
 * The RPC must be written in the format "<iface_fullname>.<rpc_name>".
 *
 * \warning If this function is never called (so the whitelist is empty), all
 * RPCs will be exposed.
 */
void t_iop_openapi_whitelist_rpc(iop_openapi_t * nonnull openapi,
                                 const lstr_t fullname);

/** Add an IOP struct in the OpenAPI application.
 *
 * Its schema will be described in the app, as well as the schema of all
 * related IOP objects.
 */
void t_iop_openapi_add_struct(iop_openapi_t * nonnull openapi,
                              const iop_struct_t * nonnull st);

/** Generate a YAML AST for the OpenAPI application. */
int t_iop_openapi_to_yaml(iop_openapi_t * nonnull openapi,
                          yaml_data_t * nonnull data, sb_t * nonnull err);

MODULE_DECLARE(iop_openapi);

#endif /* IS_LIB_COMMON_IOP_OPENAPI_H */
