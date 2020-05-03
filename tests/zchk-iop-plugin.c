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

#include <lib-common/iop.h>
#include <lib-common/iop/ic.iop.h>
#include "zchk-iop-ressources.h"

IOP_EXPORT_PACKAGES_COMMON;

IOP_EXPORT_PACKAGES(&ic__pkg);

IOP_DSO_EXPORT_RESSOURCES(str, &z_ressource_str_a, &z_ressource_str_b);
IOP_DSO_EXPORT_RESSOURCES(int, &z_ressources_int_1, &z_ressources_int_2);
