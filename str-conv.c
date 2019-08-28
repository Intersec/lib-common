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

#include "core.h"
#include "arith.h"

uint8_t const __utf8_mark[7] = { 0x00, 0x00, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc };

uint8_t const __utf8_clz_to_charlen[32] = {
#define X  0
    1, 1, 1, 1, 1, 1, 1, /* <=  7 bits */
    2, 2, 2, 2,          /* <= 11 bits */
    3, 3, 3, 3, 3,       /* <= 16 bits */
    4, 4, 4, 4, 4,       /* <= 21 bits */
    X, X, X, X, X,       /* <= 26 bits */
    X, X, X, X, X,       /* <= 31 bits */
    X,                   /* 0x80000000 and beyond */
#undef X
};

uint8_t const __utf8_char_len[32] = {
#define X  0
    1, 1, 1, 1, 1, 1, 1, 1,  /* 00... */
    1, 1, 1, 1, 1, 1, 1, 1,  /* 01... */
    X, X, X, X, X, X, X, X,  /* 100.. */
    2, 2, 2, 2,              /* 1100. */
    3, 3,                    /* 1110. */
    4,                       /* 11110 */
    X,                       /* 11111 */
#undef X
};

uint32_t const __utf8_offs[6] = {
    0x00000000UL, 0x00003080UL, 0x000e2080UL,
    0x03c82080UL, 0xfa082080UL, 0x82082080UL
};

uint8_t const __str_digit_value[128 + 256] = {
#define REPEAT16(x)  x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
    REPEAT16(255), REPEAT16(255), REPEAT16(255),
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255, 255, 255, 255, 255, 255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 255, 255, 255, 255, 255,
    255, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 255, 255, 255, 255, 255,
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
};

char const __str_digits_upper[36] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char const __str_digits_lower[36] = "0123456789abcdefghijklmnopqrstuvwxyz";

/* Unicode case mapping for most languages (except turkish 69 -> 130) */
uint16_t const __str_unicode_upper[512] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007, // 0000
    0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F, // 0008
    0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017, // 0010
    0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F, // 0018
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027, // 0020
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F, // 0028
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037, // 0030
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F, // 0038
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047, // 0040
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F, // 0048
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057, // 0050
    0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F, // 0058
    0x0060,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047, // 0060
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F, // 0068
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057, // 0070
    0x0058,0x0059,0x005A,0x007B,0x007C,0x007D,0x007E,0x007F, // 0078

    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087, // 0080
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F, // 0088
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097, // 0090
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F, // 0098
    0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7, // 00A0
    0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF, // 00A8
    // Unicode says 00B5 -> 039C (greek letter mu)
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7, // 00B0
    0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF, // 00B8
    0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7, // 00C0
    0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF, // 00C8
    0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D7, // 00D0
    0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x00DF, // 00D8
    0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7, // 00E0
    0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF, // 00E8
    0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00F7, // 00F0
    0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x0178, // 00F8

    0x0100,0x0100,0x0102,0x0102,0x0104,0x0104,0x0106,0x0106, // 0100
    0x0108,0x0108,0x010A,0x010A,0x010C,0x010C,0x010E,0x010E, // 0108
    0x0110,0x0110,0x0112,0x0112,0x0114,0x0114,0x0116,0x0116, // 0110
    0x0118,0x0118,0x011A,0x011A,0x011C,0x011C,0x011E,0x011E, // 0118
    0x0120,0x0120,0x0122,0x0122,0x0124,0x0124,0x0126,0x0126, // 0120
    0x0128,0x0128,0x012A,0x012A,0x012C,0x012C,0x012E,0x012E, // 0128
    0x0130,0x0049,0x0132,0x0132,0x0134,0x0134,0x0136,0x0136, // 0130
    0x0138,0x0139,0x0139,0x013B,0x013B,0x013D,0x013D,0x013F, // 0138
    0x013F,0x0141,0x0141,0x0143,0x0143,0x0145,0x0145,0x0147, // 0140
    0x0147,0x0149,0x014A,0x014A,0x014C,0x014C,0x014E,0x014E, // 0148
    0x0150,0x0150,0x0152,0x0152,0x0154,0x0154,0x0156,0x0156, // 0150
    0x0158,0x0158,0x015A,0x015A,0x015C,0x015C,0x015E,0x015E, // 0158
    0x0160,0x0160,0x0162,0x0162,0x0164,0x0164,0x0166,0x0166, // 0160
    0x0168,0x0168,0x016A,0x016A,0x016C,0x016C,0x016E,0x016E, // 0168
    0x0170,0x0170,0x0172,0x0172,0x0174,0x0174,0x0176,0x0176, // 0170
    0x0178,0x0179,0x0179,0x017B,0x017B,0x017D,0x017D,0x0053, // 0178
    0x0243,0x0181,0x0182,0x0182,0x0184,0x0184,0x0186,0x0187, // 0180
    0x0187,0x0189,0x018A,0x018B,0x018B,0x018D,0x018E,0x018F, // 0188
    0x0190,0x0191,0x0191,0x0193,0x0194,0x01F6,0x0196,0x0197, // 0190
    0x0198,0x0198,0x023D,0x019B,0x019C,0x019D,0x0220,0x019F, // 0198
    0x01A0,0x01A0,0x01A2,0x01A2,0x01A4,0x01A4,0x01A6,0x01A7, // 01A0
    0x01A7,0x01A9,0x01AA,0x01AB,0x01AC,0x01AC,0x01AE,0x01AF, // 01A8
    0x01AF,0x01B1,0x01B2,0x01B3,0x01B3,0x01B5,0x01B5,0x01B7, // 01B0
    0x01B8,0x01B8,0x01BA,0x01BB,0x01BC,0x01BC,0x01BE,0x01F7, // 01B8
    0x01C0,0x01C1,0x01C2,0x01C3,0x01C4,0x01C4,0x01C4,0x01C7, // 01C0
    0x01C7,0x01C7,0x01CA,0x01CA,0x01CA,0x01CD,0x01CD,0x01CF, // 01C8
    0x01CF,0x01D1,0x01D1,0x01D3,0x01D3,0x01D5,0x01D5,0x01D7, // 01D0
    0x01D7,0x01D9,0x01D9,0x01DB,0x01DB,0x018E,0x01DE,0x01DE, // 01D8
    0x01E0,0x01E0,0x01E2,0x01E2,0x01E4,0x01E4,0x01E6,0x01E6, // 01E0
    0x01E8,0x01E8,0x01EA,0x01EA,0x01EC,0x01EC,0x01EE,0x01EE, // 01E8
    0x01F0,0x01F1,0x01F1,0x01F1,0x01F4,0x01F4,0x01F6,0x01F7, // 01F0
    0x01F8,0x01F8,0x01FA,0x01FA,0x01FC,0x01FC,0x01FE,0x01FE, // 01F8
};

/* Unicode case mapping for most languages (except turkish 49 -> 131) */
uint16_t const __str_unicode_lower[512] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007, // 0000
    0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F, // 0008
    0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017, // 0010
    0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F, // 0018
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027, // 0020
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F, // 0028
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037, // 0030
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F, // 0038
    0x0040,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067, // 0040
    0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F, // 0048
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077, // 0050
    0x0078,0x0079,0x007A,0x005B,0x005C,0x005D,0x005E,0x005F, // 0058
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067, // 0060
    0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F, // 0068
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077, // 0070
    0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x007F, // 0078

    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087, // 0080
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F, // 0088
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097, // 0090
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F, // 0098
    0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7, // 00A0
    0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF, // 00A8
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7, // 00B0
    0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF, // 00B8
    0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7, // 00C0
    0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF, // 00C8
    0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00D7, // 00D0
    0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00DF, // 00D8
    0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7, // 00E0
    0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF, // 00E8
    0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F7, // 00F0
    0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF, // 00F8

    0x0101,0x0101,0x0103,0x0103,0x0105,0x0105,0x0107,0x0107, // 0100
    0x0109,0x0109,0x010B,0x010B,0x010D,0x010D,0x010F,0x010F, // 0108
    0x0111,0x0111,0x0113,0x0113,0x0115,0x0115,0x0117,0x0117, // 0110
    0x0119,0x0119,0x011B,0x011B,0x011D,0x011D,0x011F,0x011F, // 0118
    0x0121,0x0121,0x0123,0x0123,0x0125,0x0125,0x0127,0x0127, // 0120
    0x0129,0x0129,0x012B,0x012B,0x012D,0x012D,0x012F,0x012F, // 0128
    0x0069,0x0131,0x0133,0x0133,0x0135,0x0135,0x0137,0x0137, // 0130
    0x0138,0x013A,0x013A,0x013C,0x013C,0x013E,0x013E,0x0140, // 0138
    0x0140,0x0142,0x0142,0x0144,0x0144,0x0146,0x0146,0x0148, // 0140
    0x0148,0x0149,0x014B,0x014B,0x014D,0x014D,0x014F,0x014F, // 0148
    0x0151,0x0151,0x0153,0x0153,0x0155,0x0155,0x0157,0x0157, // 0150
    0x0159,0x0159,0x015B,0x015B,0x015D,0x015D,0x015F,0x015F, // 0158
    0x0161,0x0161,0x0163,0x0163,0x0165,0x0165,0x0167,0x0167, // 0160
    0x0169,0x0169,0x016B,0x016B,0x016D,0x016D,0x016F,0x016F, // 0168
    0x0171,0x0171,0x0173,0x0173,0x0175,0x0175,0x0177,0x0177, // 0170
    0x00FF,0x017A,0x017A,0x017C,0x017C,0x017E,0x017E,0x017F, // 0178
    0x0180,0x0253,0x0183,0x0183,0x0185,0x0185,0x0254,0x0188, // 0180
    0x0188,0x0256,0x0257,0x018C,0x018C,0x018D,0x01DD,0x0259, // 0188
    0x025B,0x0192,0x0192,0x0260,0x0263,0x0195,0x0269,0x0268, // 0190
    0x0199,0x0199,0x019A,0x019B,0x026F,0x0272,0x019E,0x0275, // 0198
    0x01A1,0x01A1,0x01A3,0x01A3,0x01A5,0x01A5,0x0280,0x01A8, // 01A0
    0x01A8,0x0283,0x01AA,0x01AB,0x01AD,0x01AD,0x0288,0x01B0, // 01A8
    0x01B0,0x028A,0x028B,0x01B4,0x01B4,0x01B6,0x01B6,0x0292, // 01B0
    0x01B9,0x01B9,0x01BA,0x01BB,0x01BD,0x01BD,0x01BE,0x01BF, // 01B8
    0x01C0,0x01C1,0x01C2,0x01C3,0x01C6,0x01C6,0x01C6,0x01C9, // 01C0
    0x01C9,0x01C9,0x01CC,0x01CC,0x01CC,0x01CE,0x01CE,0x01D0, // 01C8
    0x01D0,0x01D2,0x01D2,0x01D4,0x01D4,0x01D6,0x01D6,0x01D8, // 01D0
    0x01D8,0x01DA,0x01DA,0x01DC,0x01DC,0x01DD,0x01DF,0x01DF, // 01D8
    0x01E1,0x01E1,0x01E3,0x01E3,0x01E5,0x01E5,0x01E7,0x01E7, // 01E0
    0x01E9,0x01E9,0x01EB,0x01EB,0x01ED,0x01ED,0x01EF,0x01EF, // 01E8
    0x01F0,0x01F3,0x01F3,0x01F3,0x01F5,0x01F5,0x0195,0x01BF, // 01F0
    0x01F9,0x01F9,0x01FB,0x01FB,0x01FD,0x01FD,0x01FF,0x01FF, // 01F8
};

#define A  'A'
#define D  'D'
#define E  'E'
#define I  'I'
#define J  'J'
#define L  'L'
#define N  'N'
#define O  'O'
#define S  'S'
#define Z  'Z'
#define P(a,b)  ((a)+((b)<<16))
uint32_t const __str_unicode_general_ci[512] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007, // 0000
    0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F, // 0008
    0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017, // 0010
    0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F, // 0018
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027, // 0020
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F, // 0028
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037, // 0030
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F, // 0038
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047, // 0040
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F, // 0048
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057, // 0050
    0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F, // 0058
    0x0060,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047, // 0060
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F, // 0068
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057, // 0070
    0x0058,0x0059,0x005A,0x007B,0x007C,0x007D,0x007E,0x007F, // 0078

    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087, // 0080
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F, // 0088
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097, // 0090
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F, // 0098
    0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7, // 00A0
    0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF, // 00A8
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7, // 00B0
    0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF, // 00B8

       'A',   'A',   'A',   'A',   'A',   'A',P(A,E),   'C', // 00C0
       'E',   'E',   'E',   'E',   'I',   'I',   'I',   'I', // 00C8
    0x00D0,   'N',   'O',   'O',   'O',   'O',   'O',0x00D7, // 00D0
       'O',   'U',   'U',   'U',   'U',   'Y',0x00DE,P(S,S), // 00D8
       'A',   'A',   'A',   'A',   'A',   'A',P(A,E),   'C', // 00E0
       'E',   'E',   'E',   'E',   'I',   'I',   'I',   'I', // 00E8
    0x00D0,   'N',   'O',   'O',   'O',   'O',   'O',0x00F7, // 00F0
       'O',   'U',   'U',   'U',   'U',   'Y',0x00DE,   'Y', // 00F8

       'A',   'A',   'A',   'A',   'A',   'A',   'C',   'C',
       'C',   'C',   'C',   'C',   'C',   'C',   'D',   'D',
       'D',   'D',   'E',   'E',   'E',   'E',   'E',   'E',
       'E',   'E',   'E',   'E',   'G',   'G',   'G',   'G',
       'G',   'G',   'G',   'G',   'H',   'H',   'H',   'H',
       'I',   'I',   'I',   'I',   'I',   'I',   'I',   'I',
       'I',   'I',P(I,J),P(I,J),   'J',   'J',   'K',   'K',
    0x0138,   'L',   'L',   'L',   'L',   'L',   'L',   'L',
       'L',   'L',   'L',   'N',   'N',   'N',   'N',   'N',
       'N',   'N',   'N',   'N',   'O',   'O',   'O',   'O',
       'O',   'O',P(O,E),P(O,E),   'R',   'R',   'R',   'R',
       'R',   'R',   'S',   'S',   'S',   'S',   'S',   'S',
       'S',   'S',   'T',   'T',   'T',   'T',   'T',   'T',
       'U',   'U',   'U',   'U',   'U',   'U',   'U',   'U',
       'U',   'U',   'U',   'U',   'W',   'W',   'Y',   'Y',
       'Y',   'Z',   'Z',   'Z',   'Z',   'Z',   'Z',   'S',

       'B',   'B',0x0182,0x0182,0x0184,0x0184,0x0186,   'C', // 0180
       'C',   'D',   'D',0x018B,0x018B,0x018D,0x018E,0x018F, // 0188
    0x0190,0x0191,0x0191,   'G',0x0194,0x01F6,0x0196,0x0197, // 0190
    0x0198,0x0198,0x023D,0x019B,0x019C,0x019D,0x0220,0x019F, // 0198
       'O',   'O',0x01A2,0x01A2,0x01A4,0x01A4,0x01A6,0x01A7, // 01A0
    0x01A7,0x01A9,0x01AA,0x01AB,0x01AC,0x01AC,0x01AE,0x01AF, // 01A8
    0x01AF,0x01B1,0x01B2,0x01B3,0x01B3,   'Z',   'Z',0x01B7, // 01B0
    0x01B8,0x01B8,0x01BA,0x01BB,0x01BC,0x01BC,0x01BE,0x01F7, // 01B8
    0x01C0,0x01C1,0x01C2,0x01C3,P(D,Z),P(D,Z),P(D,Z),P(L,J), // 01C0
    P(L,J),P(L,J),P(N,J),P(N,J),P(N,J),   'A',   'A',   'I', // 01C8
       'I',   'O',   'O',   'U',   'U',   'U',   'U',   'U', // 01D0
       'U',   'U',   'U',   'U',   'U',0x018E,   'A',   'A', // 01D8
       'A',   'A',P(A,E),P(A,E),   'G',   'G',   'G',   'G', // 01E0
       'K',   'K',   'O',   'O',   'O',   'O',0x01EE,0x01EE, // 01E8
       'J',P(D,Z),P(D,Z),P(D,Z),   'G',   'G',0x01F6,0x01F7, // 01F0
       'N',   'N',   'A',   'A',P(A,E),P(A,E),   'O',   'O', // 01F8
};
#undef P
#undef A
#undef D
#undef E
#undef I
#undef J
#undef L
#undef N
#undef O
#undef S
#undef Z


#define A  'A'
#define D  'D'
#define E  'E'
#define I  'I'
#define J  'J'
#define L  'L'
#define N  'N'
#define O  'O'
#define S  'S'
#define Z  'Z'
#define a  'a'
#define d  'd'
#define e  'e'
#define i  'i'
#define j  'j'
#define l  'l'
#define n  'n'
#define o  'o'
#define s  's'
#define z  'z'
#define P(a,b)  ((a)+((b)<<16))
uint32_t const __str_unicode_general_cs[512] = {
    0x0000,0x0001,0x0002,0x0003,0x0004,0x0005,0x0006,0x0007, // 0000
    0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F, // 0008
    0x0010,0x0011,0x0012,0x0013,0x0014,0x0015,0x0016,0x0017, // 0010
    0x0018,0x0019,0x001A,0x001B,0x001C,0x001D,0x001E,0x001F, // 0018
    0x0020,0x0021,0x0022,0x0023,0x0024,0x0025,0x0026,0x0027, // 0020
    0x0028,0x0029,0x002A,0x002B,0x002C,0x002D,0x002E,0x002F, // 0028
    0x0030,0x0031,0x0032,0x0033,0x0034,0x0035,0x0036,0x0037, // 0030
    0x0038,0x0039,0x003A,0x003B,0x003C,0x003D,0x003E,0x003F, // 0038
    0x0040,0x0041,0x0042,0x0043,0x0044,0x0045,0x0046,0x0047, // 0040
    0x0048,0x0049,0x004A,0x004B,0x004C,0x004D,0x004E,0x004F, // 0048
    0x0050,0x0051,0x0052,0x0053,0x0054,0x0055,0x0056,0x0057, // 0050
    0x0058,0x0059,0x005A,0x005B,0x005C,0x005D,0x005E,0x005F, // 0058
    0x0060,0x0061,0x0062,0x0063,0x0064,0x0065,0x0066,0x0067, // 0060
    0x0068,0x0069,0x006A,0x006B,0x006C,0x006D,0x006E,0x006F, // 0068
    0x0070,0x0071,0x0072,0x0073,0x0074,0x0075,0x0076,0x0077, // 0070
    0x0078,0x0079,0x007A,0x007B,0x007C,0x007D,0x007E,0x007F, // 0078

    0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087, // 0080
    0x0088,0x0089,0x008A,0x008B,0x008C,0x008D,0x008E,0x008F, // 0088
    0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097, // 0090
    0x0098,0x0099,0x009A,0x009B,0x009C,0x009D,0x009E,0x009F, // 0098
    0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7, // 00A0
    0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF, // 00A8
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7, // 00B0
    0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF, // 00B8

       'A',   'A',   'A',   'A',   'A',   'A',P(A,E),   'C', // 00C0
       'E',   'E',   'E',   'E',   'I',   'I',   'I',   'I', // 00C8
    0x00D0,   'N',   'O',   'O',   'O',   'O',   'O',0x00D7, // 00D0
       'O',   'U',   'U',   'U',   'U',   'Y',0x00DE,P(s,s), // 00D8
       'a',   'a',   'a',   'a',   'a',   'a',P(a,e),   'c', // 00E0
       'e',   'e',   'e',   'e',   'i',   'i',   'i',   'i', // 00E8
    0x00F0,   'n',   'o',   'o',   'o',   'o',   'o',0x00F7, // 00F0
       'o',   'u',   'u',   'u',   'u',   'y',0x00FE,   'y', // 00F8

       'A',   'a',   'A',   'a',   'A',   'a',   'C',   'c',
       'C',   'c',   'C',   'c',   'C',   'c',   'D',   'd',
       'D',   'd',   'E',   'e',   'E',   'e',   'E',   'e',
       'E',   'e',   'E',   'e',   'G',   'g',   'G',   'g',
       'G',   'g',   'G',   'g',   'H',   'h',   'H',   'h',
       'I',   'i',   'I',   'i',   'I',   'i',   'I',   'i',
       'I',   'i',P(I,J),P(i,j),   'J',   'j',   'K',   'k',
    0x0138,   'L',   'l',   'L',   'l',   'L',   'l',   'L',
       'l',   'L',   'l',   'N',   'n',   'N',   'n',   'N',
       'n',   'n',   'N',   'n',   'O',   'o',   'O',   'o',
       'O',   'o',P(O,E),P(o,e),   'R',   'r',   'R',   'r',
       'R',   'r',   'S',   's',   'S',   's',   'S',   's',
       'S',   's',   'T',   't',   'T',   't',   'T',   't',
       'U',   'u',   'U',   'u',   'U',   'u',   'U',   'u',
       'U',   'u',   'U',   'u',   'W',   'w',   'Y',   'y',
       'Y',   'Z',   'z',   'Z',   'z',   'Z',   'z',   's',

       'b',   'B',0x0182,0x0183,0x0184,0x0185,0x0186,   'C', // 0180
       'c',   'D',   'D',0x018B,0x018C,0x018D,0x018E,0x018F, // 0188
    0x0190,0x0191,0x0192,   'G',0x0194,0x0195,0x0196,0x0197, // 0190
    0x0198,0x0199,0x019A,0x019B,0x019C,0x019D,0x019E,0x019F, // 0198
       'O',   'o',0x01A2,0x01A3,0x01A4,0x01A5,0x01A6,0x01A7, // 01A0
    0x01A8,0x01A9,0x01AA,0x01AB,0x01AC,0x01AD,0x01AE,0x01AF, // 01A8
    0x01B0,0x01B1,0x01B2,0x01B3,0x01B4,   'Z',   'z',0x01B7, // 01B0
    0x01B8,0x01B9,0x01BA,0x01BB,0x01BC,0x01BD,0x01BE,0x01BF, // 01B8
    0x01C0,0x01C1,0x01C2,0x01C3,P(D,Z),P(D,z),P(d,z),P(L,J), // 01C0
    P(L,j),P(l,j),P(N,J),P(N,j),P(n,j),   'A',   'a',   'I', // 01C8
       'i',   'O',   'o',   'U',   'u',   'U',   'u',   'U', // 01D0
       'u',   'U',   'u',   'U',   'u',0x018E,   'A',   'a', // 01D8
       'A',   'a',P(A,E),P(a,e),   'G',   'g',   'G',   'g', // 01E0
       'K',   'k',   'O',   'o',   'O',   'o',0x01EE,0x01EF, // 01E8
       'j',P(D,Z),P(D,z),P(d,z),   'G',   'g',0x01F6,0x01F7, // 01F0
       'N',   'n',   'A',   'a',P(A,E),P(A,e),   'O',   'o', // 01F8
};
#undef p
#undef a
#undef d
#undef e
#undef i
#undef j
#undef l
#undef n
#undef o
#undef s
#undef z
#undef P
#undef A
#undef D
#undef E
#undef I
#undef J
#undef L
#undef N
#undef O
#undef S
#undef Z

/* XXX: strconv_hexdecode does not check source correcness beyond
 * dest size, not a big issue for current uses.
 */
int strconv_hexdecode(void *dest, int size, const char *src, int len)
{
    const char *end;
    byte *w = dest;

    if (len < 0)
        len = strlen(src);
    end = src + MIN(len, 2 * size);

    if (len & 1)
        return -1;

    for (; src < end; src += 2) {
        *w++ = RETHROW(hexdecode(src));
    }

    return len / 2;
}

int strconv_hexencode(char *dest, int size, const void *src, int len)
{
    const byte *s = src, *end = s + MIN(len, (size - 1) / 2);

    if (size <= 0)
        return 2 * len;

    while (s < end) {
        *dest++ = __str_digits_lower[(*s >> 4) & 0xf];
        *dest++ = __str_digits_lower[(*s++)    & 0xf];
    }

    *dest = '\0';
    return len * 2;
}

static int utf8_strcmp_(const char *str1, int len1, const char *str2, int len2,
                        bool strip, bool starts_with,
                        uint32_t const str_conv[], int str_conv_len)
{
    int c1, c2, cc1, cc2;
    int off1 = 0, off2 = 0;

    /* GET_CHAR decodes invalid byte sequences as latin1
     * characters.
     */
#define GET_CHAR(i)  ({                                                      \
        int __c = utf8_ngetc_at(str##i, len##i, &off##i);                    \
                                                                             \
        if (__c < 0 && unlikely(off##i < len##i)) {                          \
            __c = str##i[off##i++];                                          \
        }                                                                    \
        __c;                                                                 \
    })

    for (;;) {
        c1 = GET_CHAR(1);
        c2 = GET_CHAR(2);
        if (c1 < 0) {
            goto eos1;
        }
        if (c2 < 0) {
            goto eos2;
        }
        if (c1 == c2) {
            continue;
        }

        if ((c1 | c2) >= str_conv_len) {
            /* large characters require exact match */
            break;
        }
        cc1 = str_conv[c1];
        cc2 = str_conv[c2];

      again:
        if (cc1 == cc2) {
            continue;
        }
        c1 = cc1 & STR_COLLATE_MASK;
        c2 = cc2 & STR_COLLATE_MASK;
        if (c1 != c2) {
            break;
        }
        /* first collate characters are identical but cc1 != cc2,
         * thus we know at least one of cc1 or cc2 has a second collate
         * character.
         */
        cc1 = STR_COLLATE_SHIFT(cc1);
        cc2 = STR_COLLATE_SHIFT(cc2);
        if (cc1 == 0) {
            c1 = GET_CHAR(1);
            if (c1 < 0)
                c1 = 0;
            if (c1 >= str_conv_len)
                break;
            cc1 = str_conv[c1];
        } else
        if (cc2 == 0) {
            c2 = GET_CHAR(2);
            if (c2 < 0)
                c2 = 0;
            if (c2 >= str_conv_len)
                break;
            cc2 = str_conv[c2];
        }
        goto again;
    }
    return CMP(c1, c2);

  eos1:
    if (strip) {
        /* Ignore trailing white space */
        while (c2 == ' ') {
            c2 = GET_CHAR(2);
        }
    }

    return c2 < 0 ? 0 : -1;

  eos2:
    if (starts_with) {
        return 0;
    }
    if (strip) {
        /* Ignore trailing white space */
        while (c1 == ' ') {
            c1 = GET_CHAR(1);
        }
    }

    return c1 < 0 ? 0 : 1;

#undef GET_CHAR
}

int utf8_stricmp(const char *str1, int len1,
                 const char *str2, int len2, bool strip)
{
    return utf8_strcmp_(str1, len1, str2, len2, strip, false,
                        __str_unicode_general_ci,
                        countof(__str_unicode_general_ci));
}

int utf8_strcmp(const char *str1, int len1,
                const char *str2, int len2, bool strip)
{
    return utf8_strcmp_(str1, len1, str2, len2, strip, false,
                        __str_unicode_general_cs,
                        countof(__str_unicode_general_cs));
}

int utf8_str_istartswith(const char *str1, int len1,
                       const char *str2, int len2)
{
    return !utf8_strcmp_(str1, len1, str2, len2, false, true,
                         __str_unicode_general_ci,
                         countof(__str_unicode_general_ci));
}

int utf8_str_startswith(const char *str1, int len1,
                       const char *str2, int len2)
{
    return !utf8_strcmp_(str1, len1, str2, len2, false, true,
                         __str_unicode_general_cs,
                         countof(__str_unicode_general_cs));
}

/****************************************************************************/
/* Charset conversions                                                      */
/****************************************************************************/

static uint16_t const __latinX_to_utf8[0x40] = {
    /* cp1252 to utf8 */
    /*  "€"            "‚"     "ƒ"     "„"     "…"      "†"    "‡"    */
    0x20ac,   0x81, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    /* "ˆ"     "‰"     "Š"     "‹"     "Œ"             "Ž"            */
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152,   0x8d, 0x017d,   0x8f,
    /*         "‘"     "’"     "“"     "”"     "•"     "–"     "—"    */
      0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    /* "˜"     "™"     "š"     "›"     "œ"             "ž"     "Ÿ"    */
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153,   0x9d, 0x017e, 0x0178,

    /* latin9 to utf8 */
    /*                                 "€"             "Š"            */
      0xa0,   0xa1,   0xa2,   0xa3, 0x20ac,   0xa5, 0x0160,   0xa7,
    /* "š"                                                            */
    0x0161,   0xa9,   0xaa,   0xab,   0xac,   0xad,   0xae,   0xaf,
    /*                                 "Ž"                            */
      0xb0,   0xb1,   0xb2,   0xb3, 0x017d,   0xb5,   0xb6,   0xb7,
    /* "ž"                             "Œ"     "œ"     "Ÿ"            */
    0x017e,   0xb9,   0xba,   0xbb, 0x0152, 0x0153, 0x0178,   0xbf,
};

static void __from_latinX_aux(sb_t *sb, const void *data, int len, int limit)
{
    const char *s = data, *end = s + len;

    while (s < end) {
        const char *p = s;

        s = utf8_skip_valid(s, end);
        sb_add(sb, p, s - p);

        if (s < end) {
            int c = (unsigned char)*s++;
            if (c < limit)
                c = __latinX_to_utf8[c & 0x7f];
            sb_adduc(sb, c);
        }
    }
}

void sb_conv_from_latin1(sb_t *sb, const void *s, int len)
{
    __from_latinX_aux(sb, s, len, 0xa0);
}

void sb_conv_from_latin9(sb_t *sb, const void *s, int len)
{
    __from_latinX_aux(sb, s, len, 0xc0);
}

int sb_conv_to_latin1(sb_t *sb, const void *data, int len, int rep)
{
    sb_t orig = *sb;
    const char *s = data, *end = s + len;

    while (s < end) {
        const char *p = s;

        while (s < end && !(*s & 0x80))
            s++;
        sb_add(sb, p, s - p);

        while (s < end && (*s & 0x80)) {
            int c = utf8_ngetc(s, end - s, &s);

            if (c < 0)
                return __sb_rewind_adds(sb, &orig);

            if (c >= 256) {
                if (rep < 0)
                    return __sb_rewind_adds(sb, &orig);
                if (!rep)
                    continue;
                c = rep;
            }
            sb_addc(sb, c);
        }
    }
    return 0;
}

int sb_conv_to_ucs2le(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    const char *s = data, *end = s + len;

    sb_grow(sb, 2 * len);

    while (s < end) {
        const char *p = s;
        char *buf;

        while (s < end && !(*s & 0x80))
            s++;
        buf = sb_growlen(sb, (s - p) * 2);
        for (; p < s; p++) {
            buf[0] = *p;
            buf[1] = '\0';
            buf += 2;
        }

        while (s < end && (*s & 0x80)) {
            int c = utf8_ngetc(s, end - s, &s);

            if (c < 0)
                c = (unsigned char)*s++;
            if (c > 0xffff)
                return __sb_rewind_adds(sb, &orig);
            buf = sb_growlen(sb, 2);
            buf[0] = c;
            buf[1] = c >> 8;
        }
    }
    return 0;
}

int sb_conv_to_ucs2be(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    const char *s = data, *end = s + len;

    sb_grow(sb, 2 * len);

    while (s < end) {
        const char *p = s;
        char *buf;

        while (s < end && !(*s & 0x80))
            s++;
        buf = sb_growlen(sb, (s - p) * 2);
        for (; p < s; p++) {
            buf[0] = '\0';
            buf[1] = *p;
            buf += 2;
        }

        while (s < end && (*s & 0x80)) {
            int c = utf8_ngetc(s, end - s, &s);

            if (c < 0)
                c = (unsigned char)*s++;
            if (c > 0xffff)
                return __sb_rewind_adds(sb, &orig);
            buf = sb_growlen(sb, 2);
            buf[0] = c >> 8;
            buf[1] = c;
        }
    }
    return 0;
}

int sb_conv_to_ucs2be_hex(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    const char *s = data, *end = s + len;

    sb_grow(sb, 4 * len);

    while (s < end) {
        const char *p = s;
        char *buf;

        while (s < end && !(*s & 0x80))
            s++;
        buf = sb_growlen(sb, (s - p) * 4);
        for (; p < s; p++) {
            buf[0] = '0';
            buf[1] = '0';
            buf[2] = __str_digits_upper[(*p >> 4) & 0xf];
            buf[3] = __str_digits_upper[(*p >> 0) & 0xf];
            buf += 4;
        }

        while (s < end && (*s & 0x80)) {
            int c = utf8_ngetc(s, end - s, &s);

            if (c < 0)
                c = (unsigned char)*s++;
            if (c > 0xffff)
                return __sb_rewind_adds(sb, &orig);
            buf = sb_growlen(sb, 4);
            buf[0] = __str_digits_upper[(c >> 12) & 0xf];
            buf[1] = __str_digits_upper[(c >>  8) & 0xf];
            buf[2] = __str_digits_upper[(c >>  4) & 0xf];
            buf[3] = __str_digits_upper[(c >>  0) & 0xf];
        }
    }
    return 0;
}

static int sb_conv_from_ucs2_hex(sb_t *sb, const void *s, int slen, bool is_be)
{
    const char *p = s, *end = p + slen;
    char *w, *wend;
    sb_t orig = *sb;

    slen /= 2;
    if (slen & 1)
        return -1;

    w    = sb_grow(sb, slen / 2);
    wend = w + sb_avail(sb);

    while (p < end) {
        int ch, cl, c;

        ch = hexdecode(p);
        cl = hexdecode(p + 2);
        p += 4;
        if (is_be) {
            c = (ch << 8) | cl;
        } else {
            c = (cl << 8) | ch;
        }

        if (unlikely(c < 0))
            return __sb_rewind_adds(sb, &orig);

        if (wend - w < 4) {
            __sb_fixlen(sb, w - sb->data);
            w    = sb_grow(sb, (end - p) / 2 + 4);
            wend = w + sb_avail(sb);
        }
        w += __pstrputuc(w, c);
    }
    __sb_fixlen(sb, w - sb->data);
    return 0;
}
int sb_conv_from_ucs2be_hex(sb_t *sb, const void *s, int slen)
{
    return sb_conv_from_ucs2_hex(sb, s, slen, true);
}
int sb_conv_from_ucs2le_hex(sb_t *sb, const void *s, int slen)
{
    return sb_conv_from_ucs2_hex(sb, s, slen, false);
}

/****************************************************************************/
/* Unicode normalization                                                    */
/****************************************************************************/

static int sb_normalize_utf8_(sb_t *sb, const char *s, int len,
                              uint32_t const str_conv[], int str_conv_len)
{
    sb_t orig = *sb;
    int off = 0;
    char *pos;
    char *end;

    pos = sb_grow(sb, len);
    end = pos + sb_avail(sb);

    for (;;) {
        int c = utf8_ngetc_at(s, len, &off);
        uint32_t conv;
        uint32_t hconv;
        int bytes;

        if (c < 0) {
            if (likely(off >= len)) {
                break;
            }
            c = s[off++];
        }
        if (c > 0xffff) {
            return __sb_rewind_adds(sb, &orig);
        }

        if (c < str_conv_len) {
            conv = str_conv[c];
        } else {
            conv = c;
        }
        hconv = conv >> 16;
        conv &= 0xffff;

        bytes = __utf8_clz_to_charlen[bsr16(conv | 1)];
        if (hconv) {
            bytes += __utf8_clz_to_charlen[bsr16(hconv)];
        }

        if (end - pos < bytes) {
            __sb_fixlen(sb, pos - sb->data);
            pos = sb_grow(sb, len - off + bytes);
            end = pos + sb_avail(sb);
        }
        pos += __pstrputuc(pos, conv);
        if (hconv) {
            pos += __pstrputuc(pos, hconv);
        }
    }
    __sb_fixlen(sb, pos - sb->data);
    return 0;
}

static int sb_utf8_transform(sb_t *sb, const char *s, int len,
                             uint16_t const str_conv[], int str_conv_len)
{
    sb_t orig = *sb;
    int off = 0;
    char *pos;
    char *end;

    pos = sb_grow(sb, len);
    end = pos + sb_avail(sb);

    for (;;) {
        int c = utf8_ngetc_at(s, len, &off);
        int bytes;

        if (c < 0) {
            if (likely(off >= len)) {
                break;
            }
            c = s[off++];
        }
        if (c > 0xffff) {
            return __sb_rewind_adds(sb, &orig);
        }

        if (c < str_conv_len) {
            c = str_conv[c];
        } else {
            c &= 0xffff;
        }

        bytes = __utf8_clz_to_charlen[bsr16(c | 1)];

        if (end - pos < bytes) {
            __sb_fixlen(sb, pos - sb->data);
            pos = sb_grow(sb, len - off + bytes);
            end = pos + sb_avail(sb);
        }
        pos += __pstrputuc(pos, c);
    }
    __sb_fixlen(sb, pos - sb->data);
    return 0;
}

int sb_normalize_utf8(sb_t *sb, const char *s, int len, bool ci)
{
    if (ci) {
        return sb_normalize_utf8_(sb, s, len, __str_unicode_general_ci,
                                  countof(__str_unicode_general_ci));
    } else {
        return sb_normalize_utf8_(sb, s, len, __str_unicode_general_cs,
                                  countof(__str_unicode_general_cs));
    }
}

int sb_add_utf8_tolower(sb_t *sb, const char *s, int len)
{
    return sb_utf8_transform(sb, s, len, __str_unicode_lower,
                             countof(__str_unicode_lower));
}

int sb_add_utf8_toupper(sb_t *sb, const char *s, int len)
{
    return sb_utf8_transform(sb, s, len, __str_unicode_upper,
                             countof(__str_unicode_upper));
}
