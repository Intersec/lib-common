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

#ifndef IS_LIB_COMMON_IOP_JSON_H
#define IS_LIB_COMMON_IOP_JSON_H

#include <lib-common/file.h>
#include <lib-common/iop.h>
#include "core/core.iop.h"

/* {{{ Private API and definitions */

typedef enum iop_json_error {
    IOP_JERR_EOF                             =   0,
    IOP_JERR_UNKNOWN                         =  -1,

    /* unterminated things */
    IOP_JERR_UNCLOSED_COMMENT                =  -2,
    IOP_JERR_UNCLOSED_STRING                 =  -3,

    /* numerical conversion */
    IOP_JERR_TOO_BIG_INT                     =  -4,
    IOP_JERR_OUT_OF_RANGE                    =  -5,
    IOP_JERR_PARSE_NUM                       =  -6,
    IOP_JERR_BAD_INT_EXT                     =  -7,

    /* (un)expected things */
    IOP_JERR_EXP_SMTH                        =  -8,
    IOP_JERR_EXP_VAL                         =  -9,
    IOP_JERR_BAD_TOKEN                       = -10,
    IOP_JERR_INVALID_FILE                    = -11,

    /* unreadable values */
    IOP_JERR_BAD_IDENT                       = -12,
    IOP_JERR_BAD_VALUE                       = -13,
    IOP_JERR_ENUM_VALUE                      = -14,

    /* structure checking */
    IOP_JERR_DUPLICATED_MEMBER               = -15,
    IOP_JERR_MISSING_MEMBER                  = -16,
    IOP_JERR_UNION_ARR                       = -17,
    IOP_JERR_UNION_RESERVED                  = -18,
    IOP_JERR_NOTHING_TO_READ                 = -19,

    IOP_JERR_CONSTRAINT                      = -20,

    /* Various errors (information is in err_str) */
    IOP_JERR_VARIOUS                         = -21,
} iop_json_error;

typedef struct iop_json_lex_ctx_t {
    int line;
    int col;

    sb_t b;

    union {
        int64_t i;
        double  d;
    } u;
    bool is_signed;
} iop_json_lex_ctx_t;

typedef struct iop_json_lex_t {
    int peek;

    /* bitfield of iop_unpack_flags */
    int flags;

    /* Context storage */
    int             s_line;
    int             s_col;
    pstream_t       s_ps;
    iop_cfolder_t  * nonnull cfolder;

    iop_json_error err;
    char *         nullable err_str;

    mem_pool_t * nonnull mp;
    pstream_t  * nullable ps;

    iop_json_lex_ctx_t  cur_ctx;
    iop_json_lex_ctx_t  peeked_ctx;
    iop_json_lex_ctx_t * nullable ctx;
} iop_json_lex_t;

qvector_t(iop_json_subfile, iop_json_subfile__t);

/* }}} */
/* {{{ Parsing JSon */

/** Initialize a JSon parser.
 *
 * \param[in] mp  Memory pool to use for memory allocations.
 * \param[in] ll  JSon parser to initialize.
 */
iop_json_lex_t * nonnull iop_jlex_init(mem_pool_t * nonnull mp,
                                       iop_json_lex_t * nonnull ll);

/** New JSon parser.
 *
 * \param[in] mp  Memory pool to use for memory allocations (including the
 *                JSon parser).
 */
static inline iop_json_lex_t * nonnull iop_jlex_new(mem_pool_t * nonnull mp)
{
    return iop_jlex_init(mp, mp_new(mp, iop_json_lex_t, 1));
}

/** Wipe a JSon parser */
void iop_jlex_wipe(iop_json_lex_t * nonnull ll);

/** Delete a JSon parser */
static inline void iop_jlex_delete(iop_json_lex_t * nullable * nonnull ll)
{
    if (*ll) {
        mem_pool_t *mp = (*ll)->mp;
        iop_jlex_wipe(*ll);
        mp_delete(mp, ll);
    }
}

/** Attach the JSon parser on a pstream_t.
 *
 * This function setups the JSon parser on the given pstream_t. You must use
 * this function before to use iop_junpack() & co.
 *
 * \param[in] ll  JSon parser.
 * \param[in] ps  pstream_t containing the JSon to parse.
 */
void iop_jlex_attach(iop_json_lex_t * nonnull ll, pstream_t * nonnull ps);

/** Detach the JSon parser.
 *
 * When calling this function the JSon parser forgets its current data stream.
 * This function is useless in most usages.
 */
static inline void iop_jlex_detach(iop_json_lex_t * nonnull ll)
{
    ll->ps = NULL;
}

/** Change the unpacker flags.
 *
 * The JSon unpacker supports the following flags: IOP_UNPACK_IGNORE_UNKNOWN.
 *
 * \param[in] ll     The JSon parser.
 * \param[in] flags  Bitfield of flags to use (see iop_unpack_flags in iop.h)
 */
static inline void iop_jlex_set_flags(iop_json_lex_t * nonnull ll, int flags)
{
    ll->flags = flags;
}

/** Convert IOP-JSon to an IOP C structure.
 *
 * This function unpacks an IOP structure encoded in JSon format. You have to
 * initialize an iop_json_lex_t structure and iop_jlex_attach() it on the data
 * you want to unpack before calling this function.
 *
 * If you want to set some special flags, you have to set it using
 * iop_jlex_set_flags() before calling this this function.
 *
 * This function cannot be used to unpack a class; use `iop_junpack_ptr`
 * instead.
 *
 * The provided memory pool *MUST* be a frame-based pool. The unpacker does
 * not provide any guarantee about the individual allocations made.
 * Use iop_dup() if you want to transfer the unpacked object to a pool with a
 * single allocation.
 *
 * \param[in]  ll            The JSon parser.
 * \param[in]  st            The IOP structure description.
 * \param[out] out           Pointer on the IOP structure to write.
 * \param[in]  single_value  Whether there is one or more structures to unpack
 *                           in the attached pstream_t.
 *  \return
 *    If `single_value` is true, the function returns 0 upon success and
 *    something < 0 in case of errors (see iop_json_error). Note that it
 *    returns IOP_JERR_NOTHING_TO_READ if EOF is reached without finding
 *    a structure to unpack.
 *
 *    If `single_value` is false, the function returns the number of bytes
 *    read successfully, or 0 if it reaches EOF. An empty buffer will not
 *    raise an error.
 */
__must_check__
int iop_junpack(iop_json_lex_t * nonnull ll, const iop_struct_t * nonnull st,
                void * nonnull out, bool single_value);

/** Convert IOP-JSon to an IOP C structure.
 *
 * This function acts exactly as `iop_junpack` but allocates (or reallocates)
 * the destination structure.
 *
 * This function MUST be used to unpack a class instead of `iop_junpack`,
 * because the size of a class is not known before unpacking it (this could be
 * a child).
 *
 * The provided memory pool *MUST* be a frame-based pool. The unpacker does
 * not provide any guarantee about the individual allocations made.
 * Use iop_dup() if you want to transfer the unpacked object to a pool with a
 * single allocation.
 */
__must_check__
int iop_junpack_ptr(iop_json_lex_t * nonnull ll,
                    const iop_struct_t * nonnull st,
                    void * nullable * nonnull out,
                    bool single_value);

/** Convert IOP-JSon to an IOP C structure using the t_pool().
 *
 * This function allow to unpack an IOP structure encoded in JSon format in
 * one call. This is equivalent to:
 *
 * <code>
 * iop_jlex_init(t_pool(), &jll);
 * iop_jlex_attach(&jll, ps);
 * jll.flags = <...>;
 * iop_junpack(&jll, ..., true);
 * </code>
 *
 * Note that the parameter `single_value` is set to true.
 *
 * This function cannot be used to unpack a class; use `t_iop_junpack_ptr_ps`
 * instead.
 *
 * Only the t_pool() version is provided since the provided memory pool of
 * iop_junpack() must be a frame-based pool.
 *
 * \param[in]  ps    The pstream_t to parse.
 * \param[in]  st    The IOP structure description.
 * \param[out] out   Pointer on the IOP structure to write.
 * \param[in]  flags Unpacker flags to use (see iop_jlex_set_flags).
 * \param[out] errb  NULL or the buffer to use to write textual error.
 *
 * \return
 *   The iop_junpack() result.
 */
__must_check__
int t_iop_junpack_ps(pstream_t * nonnull ps, const iop_struct_t * nonnull st,
                     void * nonnull out, int flags, sb_t * nullable errb);

/** Convert IOP-JSon to an IOP C structure using the t_pool().
 *
 * This function acts exactly as `t_iop_junpack_ps` but allocates
 * (or reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of `t_iop_junpack_ps`,
 * because the size of a class is not known before unpacking it (this could be
 * a child).
 *
 * Only the t_pool() version is provided since the provided memory pool of
 * iop_junpack_ptr() must be a frame-based pool.
 */
__must_check__
int t_iop_junpack_ptr_ps(pstream_t * nonnull ps,
                         const iop_struct_t * nonnull st,
                         void * nullable * nonnull out,
                         int flags, sb_t * nullable errb);

/** Convert an IOP-JSon structure contained in a file to an IOP C structure.
 *
 * This function read a file containing an IOP-JSon structure and set the
 * fields in an IOP C structure.
 *
 * This function cannot be used to unpack a class; use `t_iop_junpack_ptr_file`
 * instead.
 *
 * Only the t_pool() version is provided since the provided memory pool of
 * iop_junpack() must be a frame-based pool.
 *
 * \param[in]  filename The file name to read and parse.
 * \param[in]  st       The IOP structure description.
 * \param[out] out      Pointer on the IOP structure to write.
 * \param[in]  flags    Unpacker flags to use (see iop_jlex_set_flags).
 * \param[out] subfiles List of unpacked subfiles.
 * \param[out] errb     NULL or the buffer to use to write textual error.
 *
 * \return
 *   The t_iop_junpack_ps() result,
 *   or IOP_JERR_INVALID_FILE in case of invalid file.
 */
__must_check__
int t_iop_junpack_file(const char * nonnull filename,
                       const iop_struct_t * nonnull st,
                       void * nonnull out, int flags,
                       qv_t(iop_json_subfile) * nullable subfiles,
                       sb_t * nullable errb);

/** Convert an IOP-JSon structure contained in a file to an IOP C structure.
 *
 * This function acts exactly as `t_iop_junpack_file` but allocates
 * (or reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of
 * `t_iop_junpack_file`, because the size of a class is not known before
 * unpacking it (this could be a child).
 *
 * Only the t_pool() version is provided since the provided memory pool of
 * iop_junpack_ptr() must be a frame-based pool.
 */
__must_check__
int t_iop_junpack_ptr_file(const char * nonnull filename,
                           const iop_struct_t * nonnull st,
                           void * nullable * nonnull out, int flags,
                           qv_t(iop_json_subfile) * nullable subfiles,
                           sb_t * nullable errb);

/** Print a textual error after iop_junpack() failure.
 *
 * When iop_junpack() fails, you can print the error textual description in
 * a sb_t with this function.
 */
void iop_jlex_write_error(iop_json_lex_t * nonnull ll, sb_t * nonnull sb);

/** Print a textual error after iop_junpack() failure.
 *
 * When iop_junpack() fails, you can print the error textual description in
 * a buffer with this function.
 */
int  iop_jlex_write_error_buf(iop_json_lex_t * nonnull ll,
                              char * nonnull buf, int len);


/* }}} */
/* {{{ Generating JSon */

/** JSon packer custom flags */
enum iop_jpack_flags {
    /** Obsolete, kept for backward compatibility.
     */
    IOP_JPACK_STRICT = (1U << 0),

    /** Generate compact JSON.
     *
     * Omit cosmetic whitespaces such as indentations and spaces after colons.
     */
    IOP_JPACK_NO_WHITESPACES = (1U << 1),

    /** Do not append '\n' when done.
     */
    IOP_JPACK_NO_TRAILING_EOL = (1U << 2),

    /** Don't pack private fields.
     *
     * Use this flag when packing objects on a public interface. This will
     * omit private fields from the generated JSON.
     */
    IOP_JPACK_SKIP_PRIVATE = (1U << 3),

    /** Write big integers as integers.
     *
     * By default, the packer writes integers that cannot be safely unpacked
     * in Javascript as strings instead of integers. With this flag set, the
     * packer will always pack integers as integers.
     */
    IOP_JPACK_UNSAFE_INTEGERS = (1U << 4),

    /** Skip fields having their default value.
     *
     * This is good to make the JSon more compact, but is dangerous if a
     * default value changes.
     */
    IOP_JPACK_SKIP_DEFAULT = (1U << 5),

    /** Skip empty repeated fields. */
    IOP_JPACK_SKIP_EMPTY_ARRAYS = (1U << 6),

    /** Skip empty sub-structures. */
    IOP_JPACK_SKIP_EMPTY_STRUCTS = (1U << 7),

    /** Shorten long data strings when not writing a file (lossy).
     *
     * Data longer than 25 characters will be replaced by
     * "XXXXXXXXXXX …(skip x bytes)… YYYYYYYYYYY" where only the first and
     * last 11 characters are kept.
     */
    IOP_JPACK_SHORTEN_DATA = (1U << 8),

    /** Skip class names (lossy). */
    IOP_JPACK_SKIP_CLASS_NAMES = (1U << 9),

    /** Skip class names when not needed.
     *
     * If set, the class names won't be written if they are equal to the
     * actual type of the field (missing class names are supported by the
     * unpacker in that case).
     */
    IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES = (1U << 10),

    /** Produce the smallest possible json. */
    IOP_JPACK_MINIMAL = IOP_JPACK_NO_WHITESPACES
                      | IOP_JPACK_NO_TRAILING_EOL
                      | IOP_JPACK_SKIP_DEFAULT
                      | IOP_JPACK_SKIP_EMPTY_ARRAYS
                      | IOP_JPACK_SKIP_EMPTY_STRUCTS
                      | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES,
};

typedef int (iop_jpack_writecb_f)(void * nonnull priv,
                                  const void * nonnull buf, int len);

/** Convert an IOP C structure to IOP-JSon.
 *
 * This function packs an IOP structure into (strict) JSon format.
 *
 * \param[in] st       IOP structure description.
 * \param[in] value    Pointer on the IOP structure to pack.
 * \param[in] writecb  Callback to call when writing (like iop_sb_write).
 * \param[in] priv     Private data to give to the callback.
 * \param[in] flags    Packer flags bitfield (see iop_jpack_flags).
 */
int iop_jpack(const iop_struct_t * nonnull st, const void * nonnull value,
              iop_jpack_writecb_f * nonnull writecb,
              void * nonnull priv, unsigned flags);

/** Serialize an IOP C structure in an IOP-JSon file.
 *
 * This function packs an IOP structure into (strict) JSon format and writes
 * it in a file.
 *
 * Some IOP sub-objects can be written in separate files using the include
 * feature. Only one level of inclusion is supported.
 *
 * \param[in]  filename   The file in which the value is packed.
 * \param[in]  file_flags The flags to use when opening the file
 *                        (\ref enum file_flags).
 * \param[in]  file_mode  The mode to use when opening the file.
 * \param[in]  st         IOP structure description.
 * \param[in]  value      Pointer on the IOP structure to pack.
 * \param[in]  flags      Packer flags bitfield (see iop_jpack_flags).
 * \param[in]  subfiles   If set, this is the list of IOP objects that must be
 *                        written in separate files using @include.
 * \param[out] err        Buffer filled in case of error.
 * \param[out] err        NULL or the buffer to use to write textual error.
 */
int __iop_jpack_file(const char * nonnull filename, unsigned file_flags,
                     mode_t file_mode, const iop_struct_t * nonnull st,
                     const void * nonnull value, unsigned flags,
                     const qv_t(iop_json_subfile) * nullable subfiles,
                     sb_t * nullable err);

static inline int
iop_jpack_file(const char * nonnull filename, const iop_struct_t * nonnull st,
               const void * nonnull value, unsigned flags, sb_t * nullable err)
{
    return __iop_jpack_file(filename, FILE_WRONLY | FILE_CREATE | FILE_TRUNC,
                            0644, st, value, flags, NULL, err);
}

/** Callback to use for writing JSon into a sb_t. */
static inline int iop_sb_write(void * nonnull _b, const void * nonnull buf,
                               int len)
{
    sb_add((sb_t *)_b, buf, len);
    return len;
}

/** Pack an IOP C structure to IOP-JSon in a sb_t.
 *
 * See iop_jpack().
 */
static inline int
iop_sb_jpack(sb_t * nonnull sb, const iop_struct_t * nonnull st,
             const void * nonnull value, unsigned flags)
{
    return iop_jpack(st, value, &iop_sb_write, sb, flags);
}

/** Dump IOP structures in JSon format using e_trace */
#ifndef NDEBUG
void iop_jtrace_(int lvl, const char * nonnull fname, int lno,
                 const char * nonnull func, const char * nullable name,
                 const iop_struct_t * nonnull , const void * nonnull);
#define iop_jtrace(lvl, st, v) \
    do {                                                     \
        if (e_is_traced(lvl)) {                              \
            const st##__t *__v = (v);                        \
            iop_jtrace_(lvl, __FILE__, __LINE__, __func__,   \
                        NULL, &st##__s, __v);                \
        }                                                    \
    } while (0)
#define iop_named_jtrace(lvl, name, st, v)                   \
    do {                                                     \
        if (e_name_is_traced(lvl, name)) {                   \
            const st##__t *__v = (v);                        \
            iop_jtrace_(lvl, __FILE__, __LINE__, __func__,   \
                        name, &st##__s, __v);                \
        }                                                    \
    } while (0)
#else
#define iop_jtrace_(lvl, ...)               e_trace_ignore(lvl, ##__VA_ARGS__)
#define iop_jtrace(lvl, st, v)              e_trace_ignore(lvl, v)
#define iop_named_jtrace(lvl, name, st, v)  e_trace_ignore(lvl, name, v)
#endif

/* }}} */
/* {{{ Struct printf formatter %*pS */

/** Private intermediary structure for IOP struct/union formatting. */
struct iop_struct_value {
    /* Struct/union description, can be null only when the element is an
     * object.
     */
    const iop_struct_t * nullable st;

    const void * nonnull val;
};

/** Provide the appropriate arguments to the %*pS modifier.
 *
 * '%*pS' can be used in format string in order to print the JSON-encoded
 * content of an IOP stuct or union. You can provide the flags to be used to
 * the JSON packer in the format arguments (\ref iop_jpack_flags).
 *
 * \param[in]  pfx    IOP struct descriptor prefix.
 * \param[in]  _val   The IOP struct or union to print.
 * \param[in]  _flags The JSON packing flags
 *
 * See \ref IOP_ST_FMT_ARG() for a convenience helper to print compact JSON.
 */
#define IOP_ST_FMT_ARG_FLAGS(pfx, _val, _flags)                              \
    (_flags), &(struct iop_struct_value){                                    \
        .st = &pfx##__s,                                                     \
        .val = ({ const pfx##__t *__val = (_val); __val; }) }

/** Provide the appropriate argument to print compact JSON with the %*pS
 * format.
 *
 * This macro is a convenience helper for \ref IOP_ST_FMT_ARG_FLAGS to
 * cover the usual use case where compact JSON is needed.
 *
 * \param[in]  pfx  IOP struct descriptor prefix.
 * \param[in]  _val The IOP struct or union to print.
 *
 * \note If the struct is actually a class, use \ref IOP_OBJ_FMT_ARG
 *       formatting helper instead to get the code smaller.
 */
#define IOP_ST_FMT_ARG(pfx, _val)                                            \
    IOP_ST_FMT_ARG_FLAGS(pfx, _val,                                          \
                         IOP_JPACK_NO_WHITESPACES | IOP_JPACK_NO_TRAILING_EOL)

/** Same as \ref IOP_ST_FMT_ARG_FLAGS but with explicit description pointer.
 */
#define IOP_ST_DESC_FMT_ARG_FLAGS(desc, _val, _flags)                        \
    (_flags), &(struct iop_struct_value){                                    \
        .st = desc,                                                          \
        .val = (_val) }

/** Provide the appropriate arguments to print an IOP class instance in JSON
 *  with the %*pS modifier.
 *
 * '%*pS' can be used in format string in order to print the JSON-encoded
 * content of an IOP object (instance of an IOP class). You can provide the
 * flags to be used to the JSON packer in the format arguments
 * (\ref iop_jpack_flags).
 *
 * \param[in]  _obj   The object to print.
 * \param[in]  _flags The JSON packing flags
 *
 * See \ref IOP_OBJ_FMT_ARG() for a convenience helper to print compact JSON.
 */
#define IOP_OBJ_FMT_ARG_FLAGS(_obj, _flags)                                  \
    (_flags),                                                                \
    &(struct iop_struct_value){                                              \
        .st = NULL,                                                          \
        .val = ({                                                            \
            typeof(*_obj) *_fmt_obj = (_obj);                                \
            __unused__ const iop_struct_t *__ofa_st = _fmt_obj->__vptr;      \
            _fmt_obj;                                                        \
        })                                                                   \
    }

/** Provide the appropriate argument to print an IOP class instance in compact
 *  JSON with the %*pS modifier.
 *
 * This macro is a convenience helper for \ref IOP_OBJ_FMT_ARG_FLAGS to
 * cover the usual use case where compact JSON is needed.
 *
 * \param[in]  _obj  The object to print.
 */
#define IOP_OBJ_FMT_ARG(_obj)                                                \
    IOP_OBJ_FMT_ARG_FLAGS((_obj),                                            \
                          IOP_JPACK_NO_WHITESPACES | IOP_JPACK_NO_TRAILING_EOL)

/* }}} */
/* {{{ Packing helpers */

/* Get a pointer to the index'th value for the field.
 * To be used in conjunction with iop_json_get_n_and_ptr.
 */
const void * nonnull
iop_json_get_struct_field_value(const iop_field_t * nonnull fdesc,
                                const void * nonnull ptr, int index);

/* Get details about the packed value.
 *
 * If the field is not set, is_skipped is set to true.
 * If the field is an array, n is set to the number of fields, and value
 * is set to the first element.
 * Otherwise, n is set to 1 and value to the element.
 */
const void * nullable
iop_json_get_n_and_ptr(const iop_struct_t * nonnull desc, unsigned flags,
                       const iop_field_t * nonnull fdesc,
                       const void * nonnull value, int * nonnull n,
                       bool * nonnull is_skipped);

/* }}} */

#endif
