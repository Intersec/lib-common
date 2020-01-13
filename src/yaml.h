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

#include <lib-common/container-qvector.h>
#include <lib-common/container-qhash.h>

/* {{{ AST types definitions */

typedef struct yaml_data_t yaml_data_t;
typedef struct yaml_scalar_t yaml_scalar_t;
typedef struct yaml_obj_t yaml_obj_t;
typedef struct yaml_seq_t yaml_seq_t;
typedef struct yaml_parse_t yaml_parse_t;
typedef struct yaml_presentation_node_t yaml_presentation_node_t;
qvector_t(yaml_pres_node, yaml_presentation_node_t * nullable);

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

/* Substring delimited by two positions in the parsed string. */
typedef struct yaml_span_t {
    /* Position in the parsed string where the data started */
    yaml_pos_t start;
    /* Position in the parsed string where the data ended */
    yaml_pos_t end;

    /* Related parsing context */
    const yaml_parse_t * nonnull env;
} yaml_span_t;

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
    yaml_span_t span;
    union {
        yaml_scalar_t scalar;
        yaml_seq_t *seq;
        yaml_obj_t *obj;
    };
    yaml_data_type_t type;

    /* LSTR_NULL_V if untyped */
    lstr_t tag;
    yaml_span_t * nullable tag_span;

    yaml_presentation_node_t * nullable presentation;
};
qvector_t(yaml_data, yaml_data_t);
qm_kvec_t(yaml_data, lstr_t, yaml_data_t, qhash_lstr_hash, qhash_lstr_equal);

typedef struct yaml_key_data_t {
    lstr_t key;
    yaml_data_t data;

    yaml_span_t key_span;
    yaml_presentation_node_t * nullable key_presentation;
} yaml_key_data_t;
qvector_t(yaml_key_data, yaml_key_data_t);

struct yaml_obj_t {
    qv_t(yaml_key_data) fields;
};

struct yaml_seq_t {
    qv_t(yaml_data) datas;

    qv_t(yaml_pres_node) pres_nodes;
};

typedef struct yaml_presentation_t yaml_presentation_t;

/** Return a description of the data's type.
 *
 * The description will be formatted in this style:
 *  "a scalar"
 *  "a tagged object"
 *  ...
 */
const char * nonnull yaml_data_get_type(const yaml_data_t * nonnull data,
                                        bool ignore_tag);

/* }}} */
/* {{{ Parsing */

typedef enum yaml_parse_flags_t {
    /** Save presentation data when parsing.
     *
     * The presentation data can then be retrieved using
     * t_yaml_pack_env_get_presentation.
     */
    YAML_PARSE_GEN_PRES_DATA = 1 << 0,
} yaml_parse_flags_t;

/** Create a new YAML parsing object.
 *
 * \param[in]  flags  bitfield of yaml_pack_flags_t elements.
 */
yaml_parse_t * nonnull t_yaml_parse_new(int flags);

/** Delete a YAML parsing object.
 *
 * This function *must* be called once parsing is done, as even though the
 * yaml_parse_t object is t_pool allocated, it might have assets that needs
 * proper deallocations (for example, mmap'ed included files).
 */
void yaml_parse_delete(yaml_parse_t * nullable * nonnull self);

/** Attach a pstream_t to the yaml_parse_t object.
 *
 * The stream will be used when t_yaml_parse is called. In error messages,
 * it will be referred as "<string>".
 */
void yaml_parse_attach_ps(yaml_parse_t * nonnull self, pstream_t ps);

/** Attach a file to the yaml_parse_t object.
 *
 * The file will be mmap'ed and used when t_yaml_parse is called.
 * `yaml_parse_delete` *must* be called to free the mmap.
 *
 * dirpath can be used to resolve relative imports. If set, \p filepath
 * will be resolved relative to \p dirpath. If LSTR_NULL_V, \p filepath
 * is resolved as is (so relative to the current working directory).
 */
int t_yaml_parse_attach_file(yaml_parse_t * nonnull self, lstr_t filepath,
                             lstr_t dirpath, sb_t * nonnull err);

/** Parse a YAML stream into a yaml data object.
 *
 * `yaml_parse_attach_ps` or `yaml_parse_attach_file` must have been called
 * first.
 *
 * Although the resulting YAML data is t_pool allocated, it will depend
 * on data stored in the yaml_parse_t object. Therefore, the filetime of
 * the YAML data *must* be contained within the lifetime of the YAML parse
 * object.
 *
 * \param[in]   self          A YAML parsing object.
 * \param[out]  out           The YAML data parsed.
 * \param[out]  err        Error buffer filled in case of error.
 * \return -1 on error, 0 otherwise.
 */
int
t_yaml_parse(yaml_parse_t * nonnull self, yaml_data_t * nonnull out,
             sb_t * nonnull err);

/** Pretty print an error message related to a parsed span.
 *
 * The generated error message will look like this:
 *
 * if the span is on a single line:
 *  <filename>:<line>:<col>: <error_msg>
 *  <line of span in parsed stream>
 *      ^^^^^^^
 *
 * if the span is on multiple lines:
 *  <filename>:<line>:<col>: <error_msg>
 *  <first line of span in parsed stream>
 *      ^ starting here
 */
void yaml_parse_pretty_print_err(const yaml_span_t * nonnull span,
                                 lstr_t error_msg, sb_t * nonnull out);

/** Copy the presentation data associated with a parsed YAML data.
 *
 * This presentation data can then be used when repacking, to reformat the
 * YAML document properly. It includes comments, includes, newlines, etc.
 *
 * \warning the flag YAML_PARSE_GEN_PRES_DATA must have been used when
 * parsing the object.
 */
const yaml_presentation_t * nonnull
t_yaml_data_get_presentation(const yaml_data_t * nonnull data);

/* }}} */
/* {{{ Packing */

typedef struct yaml_pack_env_t yaml_pack_env_t;
typedef int (yaml_pack_writecb_f)(void * nullable priv,
                                  const void * nonnull buf, int len,
                                  sb_t * nonnull err);

/** Create a new YAML packing context. */
yaml_pack_env_t * nonnull t_yaml_pack_env_new(void);

/** Set the output directory.
 *
 * Files, including subfiles, will be created inside this directory.
 * This function must be called in order for included subfiles to be recreated
 * properly.
 *
 * \param[in]   env         The packing environment.
 * \param[in]   dirpath     The path to the directory where files will be
 *                          written. If non-existent, it will be created with
 *                          permission 0755.
 * \param[out]  err         Error buffer, filled iff -1 is returned.
 */
int t_yaml_pack_env_set_outdir(yaml_pack_env_t * nonnull env,
                               const char * nonnull dirpath,
                               sb_t * nonnull err);

/** Set the mode to use when creating files.
 *
 * Files, including subfiles, will be created with this mode.
 * By default, 0644 is used.
 *
 * \param[in]   env         The packing environment.
 * \param[in]   file_mode   Mode to use when creating files.
 */
void yaml_pack_env_set_file_mode(yaml_pack_env_t * nonnull env, mode_t mode);

/** Pack a YAML data.
 *
 * The callback \p writecb will be called for every buffer than must be
 * written.
 *
 * Unless you need to write a custom writer, you probably want to use
 * yaml_pack_sb or yaml_pack_file instead.
 *
 * \param[in]  env           Packing environment.
 * \param[in]  data          The YAML data to pack.
 * \param[in]  presentation  Presentation data to reformat the document
 *                           properly. If NULL, the presentation data that may
 *                           be present in \p data is used.
 * \param[in]  writecb       Callback called on every buffer that must be
 *                           written.
 * \param[in]  data          Private data passed to \p writecb.
 * \param[out] err           Buffer filled in case of error.
 */
int yaml_pack(yaml_pack_env_t * nonnull env, const yaml_data_t * nonnull data,
              const yaml_presentation_t * nullable presentation,
              yaml_pack_writecb_f * nonnull writecb, void * nullable priv,
              sb_t * nullable err);

/** Pack a YAML data into a YAML string.
 */
void yaml_pack_sb(yaml_pack_env_t * nonnull env,
                  const yaml_data_t * nonnull data,
                  const yaml_presentation_t * nullable presentation,
                  sb_t * nonnull sb);

/** Pack a YAML data into a YAML file.
 *
 * \warning t_yaml_pack_env_set_outdir *must* have been called first.
 *
 * \param[in]  filename   The file in which the value is packed.
 * \param[in]  file_flags The flags to use when opening the file
 *                        (\ref enum file_flags).
 * \param[in]  file_mode  The mode to use when opening the file.
 */
int yaml_pack_file(yaml_pack_env_t * nonnull env,
                   const char * nonnull filename,
                   const yaml_data_t * nonnull data,
                   const yaml_presentation_t * nullable presentation,
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
