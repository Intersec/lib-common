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

#include <lib-common/iop.h>
#include <lib-common/iop-rpc.h>
#include <lib-common/core/core.iop.h>

#include "test.iop.h"

#include "test_1_2.fc.c"
#include "test_3.fc.c"

IOP_EXPORT_PACKAGES_COMMON;

IOP_EXPORT_PACKAGES(&test__pkg, &ic__pkg, &core__pkg);

IOP_DSO_EXPORT_RESSOURCES(iopy_on_register, test_1_2_scripts, test_3_script);
