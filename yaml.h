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

#ifndef IS_LIB_COMMON_YAML_H
#define IS_LIB_COMMON_YAML_H

#include "container-qvector.h"
#include "container-qhash.h"

/* {{{ AST types definitions */

typedef struct yaml_data_t yaml_data_t;
typedef struct yaml_scalar_t yaml_scalar_t;
typedef struct yaml_obj_t yaml_obj_t;
typedef struct yaml_seq_t yaml_seq_t;

/* All possible types for YAML scalar values */
typedef enum yaml_scalar_type_t {
    /* raw string, or delimited with "" */
    YAML_SCALAR_STRING,
    /* floating-point number */
    YAML_SCALAR_DOUBLE,
    /* integer >= 0 */
    YAML_SCALAR_UINT,
    /* integer < 0 */
    YAML_SCALAR_INT,
    /* true or false */
    YAML_SCALAR_BOOL,
    /* ~ or null */
    YAML_SCALAR_NULL
} yaml_scalar_type_t;

/* Position in the parsed string */
typedef struct yaml_pos_t {
    uint32_t line_nb;
    uint32_t col_nb;
    /* Pointer to this position in the string. Very useful for informative
     * logs and errors */
    const char *s;
} yaml_pos_t;

#define YAML_POS_FMT  "%u:%u"
#define YAML_POS_ARG(p)  p.line_nb, p.col_nb

struct yaml_scalar_t {
    union {
        lstr_t s;
        double d;
        uint64_t u;
        int64_t i;
        bool b;
    };
    yaml_scalar_type_t type;
};

typedef enum yaml_data_type_t {
    /* Scalar value */
    YAML_DATA_SCALAR,
    /* Sequence, ie array of data */
    YAML_DATA_SEQ,
    /* Object, ie list of (string, data) pairs */
    YAML_DATA_OBJ
} yaml_data_type_t;

struct yaml_data_t {
    /* Position in the parsed string where the data started */
    yaml_pos_t pos_start;
    /* Position in the parsed string where the data ended */
    yaml_pos_t pos_end;
    union {
        yaml_scalar_t scalar;
        yaml_seq_t *seq;
        yaml_obj_t *obj;
    };
    yaml_data_type_t type;

    /* LSTR_NULL_V if untyped */
    lstr_t tag;
};
qvector_t(yaml_data, yaml_data_t);
qm_kvec_t(yaml_data, lstr_t, yaml_data_t, qhash_lstr_hash, qhash_lstr_equal);

typedef struct yaml_key_data_t {
    lstr_t key;
    yaml_data_t data;
} yaml_key_data_t;
qvector_t(yaml_key_data, yaml_key_data_t);

struct yaml_obj_t {
    qv_t(yaml_key_data) fields;
};

struct yaml_seq_t {
    qv_t(yaml_data) datas;
};

/* }}} */
/* {{{ Parsing */

/** Return a description of the data's type.
 *
 * The description will be formatted in this style:
 *  "a scalar"
 *  "a tagged object"
 *  ...
 */
const char * nonnull yaml_data_get_type(const yaml_data_t * nonnull data);

/** Parse a YAML stream into a yaml data object.
 *
 * \param[in]   ps   The pstream to parse.
 * \param[out]  out  The YAML data parsed.
 * \param[out]  err  Error buffer filled in case of error.
 * \return -1 on error, 0 otherwise.
 */
int t_yaml_parse(pstream_t ps, yaml_data_t * nonnull out, sb_t * nonnull err);

/* }}} */
/* {{{ Packing */

/** Pack a YAML data into a YAML string.
 */
int yaml_pack_sb(const yaml_data_t * nonnull data, sb_t * nonnull sb);

/** Pack a YAML data into a YAML file.
 *
 * \param[in]  filename   The file in which the value is packed.
 * \param[in]  file_flags The flags to use when opening the file
 *                        (\ref enum file_flags).
 * \param[in]  file_mode  The mode to use when opening the file.
 * \param[in]  data       The YAML data to pack.
 * \param[out] err        Buffer filled in case of error.
 */
int yaml_pack_file(const char * nonnull filename, unsigned file_flags,
                   mode_t file_mode, const yaml_data_t * nonnull data,
                   sb_t * nonnull err);

/* }}} */
/* {{{ Packing helpers */

void yaml_data_set_string(yaml_data_t * nonnull data, lstr_t str);
void yaml_data_set_double(yaml_data_t * nonnull data, double d);
void yaml_data_set_uint(yaml_data_t * nonnull data, uint64_t u);
void yaml_data_set_int(yaml_data_t * nonnull data, int64_t i);
void yaml_data_set_bool(yaml_data_t * nonnull data, bool b);
void yaml_data_set_null(yaml_data_t * nonnull data);

void t_yaml_data_new_seq(yaml_data_t * nonnull data, int capacity);
void yaml_seq_add_data(yaml_data_t * nonnull data, yaml_data_t val);

void t_yaml_data_new_obj(yaml_data_t * nonnull data, int nb_fields_capacity);
void yaml_obj_add_field(yaml_data_t * nonnull data, lstr_t key,
                        yaml_data_t val);

/* }}} */

MODULE_DECLARE(yaml);

#endif /* IS_LIB_COMMON_YAML_H */
