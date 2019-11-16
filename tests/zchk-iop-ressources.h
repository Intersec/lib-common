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

#ifndef IS_ZCHK_IOP_RESSOURCES_H
#define IS_ZCHK_IOP_RESSOURCES_H

#include "iop.h"

IOP_DSO_DECLARE_RESSOURCE_CATEGORY(str, const char *);
IOP_DSO_DECLARE_RESSOURCE_CATEGORY(int, int);

extern const char *z_ressource_str_a;
extern const char *z_ressource_str_b;

extern const int z_ressources_int_1;
extern const int z_ressources_int_2;

#endif
