/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

/*
 *
 * \file       crc_macros.h
 * \brief      CRC32 calculation
 *
 * Calculate the CRC32 using the slice-by-eight algorithm.
 * It is explained in this document:
 * http://www.intel.com/technology/comms/perfnet/download/CRC_generators.pdf
 * The code in this file is not the same as in Intel's paper, but
 * the basic principle is identical.
 *
 *  Author:     Lasse Collin
 *
 *  This file has been put into the public domain.
 *  You can do whatever you want with this file.
 *
 */

#if __BYTE_ORDER == __BIG_ENDIAN
#   define A(x)   ((x) >> 24)
#   define A1(x)  ((x) >> 56)
#   define B(x)   (((x) >> 16) & 0xFF)
#   define C(x)   (((x) >> 8) & 0xFF)
#   define D(x)   ((x) & 0xFF)

#   define S8(x)  ((x) << 8)
#   define S32(x) ((x) << 32)
#else
#   define A(x)   ((x) & 0xFF)
#   define A1(x)  A(x)
#   define B(x)   (((x) >> 8) & 0xFF)
#   define C(x)   (((x) >> 16) & 0xFF)
#   define D(x)   ((x) >> 24)

#   define S8(x)  ((x) >> 8)
#   define S32(x) ((x) >> 32)
#endif
