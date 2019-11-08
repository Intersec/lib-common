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
 * \param[in]  description  The description of the application. Optional.
 * \return An IOP OpenAPI application.
 */
iop_openapi_t * nonnull
t_new_iop_openapi(const lstr_t title, const lstr_t version,
                  const lstr_t description);

/** Add an IOP struct in the OpenAPI application.
 *
 * The IOP struct will be added in the components schemas of the application,
 * as well as the schemas of all its dependencies.
 */
void t_iop_openapi_add_struct(iop_openapi_t *openapi, const iop_struct_t *st);

void t_iop_openapi_to_yaml(const iop_openapi_t * nonnull openapi,
                           yaml_data_t * nonnull data);

MODULE_DECLARE(iop_openapi);

#endif /* IS_LIB_COMMON_IOP_OPENAPI_H */
