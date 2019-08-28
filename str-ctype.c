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

/* ctype description for tokens "abcdefghijklmnopqrstuvwxyz" */
ctype_desc_t const ctype_islower = {
    {
        0x00000000,
        0x00000000,
        0x00000000,
        0x07fffffe,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};

/* ctype description for tokens "ABCDEFGHIJKLMNOPQRSTUVWXYZ" */
ctype_desc_t const ctype_isupper = {
    {
        0x00000000,
        0x00000000,
        0x07fffffe,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};

/* ctype description for tokens "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" */
ctype_desc_t const ctype_isalnum = {
    {
        0x00000000,
        0x03ff0000,
        0x07fffffe,
        0x07fffffe,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};

/* ctype description for tokens "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" */
ctype_desc_t const ctype_isalpha = {
    {
        0x00000000,
        0x00000000,
        0x07fffffe,
        0x07fffffe,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};

/* ctype description for tokens "0123456789" */
ctype_desc_t const ctype_isdigit = {
    {
        0x00000000,
        0x03ff0000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};

/* ctype description for tokens "0123456789abcdefABCDEF" */
ctype_desc_t const ctype_ishexdigit = {
    {
        0x00000000, 0x03ff0000, 0x0000007e, 0x0000007e,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    }
};

/* XXX Valid C variable name should match this span *and* should not begin
 *     with a numeric character
 */
/* ctype description for tokens "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_" */
ctype_desc_t const ctype_iscvar = {
    {
        0x00000000,
        0x03ff0000,
        0x87fffffe,
        0x07fffffe,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
    }
};


/* ctype description for tokens "01" */
ctype_desc_t const ctype_isbindigit = {
    {
        0x00000000, 0x00030000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    }
};

/* ctype description for s:" \f\n\r\t\v" */
ctype_desc_t const ctype_isspace = { {
    0x00003e00, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
} };


/* ctype describing characters of a word */
ctype_desc_t const ctype_iswordpart = {
    {
        0x00000000,
        0x03ff0010,
        0x07ffffff,
        0x07fffffe,
        0xffffffff,
        0xffffffff,
        0xffffffff,
        0xffffffff,
    }
};
