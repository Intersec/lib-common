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

#if !defined(IS_LIB_COMMON_IOP_H) || defined(IS_LIB_COMMON_IOP_XML_H)
#  error "you must include <lib-common/iop.h> instead"
#else
#define IS_LIB_COMMON_IOP_XML_H

/* {{{ Parsing XML */

/** Convert IOP-XML to an IOP C structure.
 *
 * This function unpacks an IOP structure encoded in XML format. You have to
 * provide yourself a xml_reader_t setup on the XML data to unpack.
 *
 * iop_xunpack() assumes that the provided xml_reader_t is given with the
 * following flags:
 *      XML_PARSE_NOENT | XML_PARSE_NOBLANKS | XML_PARSE_NONET
 *      | XML_PARSE_NOCDATA
 *
 * Concerning the unpacking flags, the XML unpacker supports the following
 * ones: IOP_UNPACK_IGNORE_UNKNOWN.
 *
 * This function cannot be used to unpack a class; use `iop_xunpack_ptr_flags`
 * instead.
 *
 * \param[in] xp     The xml_reader_t setup on the XML data (see xmlr.h).
 * \param[in] mp     Memory pool to use for memory allocations.
 * \param[in] st     The IOP structure description.
 * \param[in] out    Pointer on the IOP structure to write.
 * \param[in] flags  Bitfield of flags to use (see iop_unpack_flags in iop.h).
 */
__must_check__
int iop_xunpack_flags(void * nonnull xp, mem_pool_t * nonnull mp,
                      const iop_struct_t * nonnull st, void * nonnull out,
                      int flags);

/** Convert IOP-XML to an IOP C structure.
 *
 * This function acts exactly as `iop_xunpack_flags` but allocates (or
 * reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of
 * `iop_xunpack_flags`, because the size of a class is not known before
 * unpacking it (this could be a child).
 *
 */
__must_check__
int iop_xunpack_ptr_flags(void * nonnull xp, mem_pool_t * nonnull mp,
                          const iop_struct_t * nonnull st,
                          void * nullable * nonnull out, int flags);

/** Convert IOP-XML to an IOP C structure.
 *
 * This function just call iop_xunpack_flags() with flags set to 0.
 */
__must_check__ static inline int
iop_xunpack(void * nonnull xp, mem_pool_t * nonnull mp,
            const iop_struct_t * nonnull st, void * nonnull out)
{
    return iop_xunpack_flags(xp, mp, st, out, 0);
}

/** Convert IOP-XML to an IOP C structure.
 *
 * This function just call iop_xunpack_ptr_flags() with flags set to 0.
 */
__must_check__ static inline int
iop_xunpack_ptr(void * nonnull xp, mem_pool_t * nonnull mp,
                const iop_struct_t * nonnull st,
                void * nullable * nonnull out)
{
    return iop_xunpack_ptr_flags(xp, mp, st, out, 0);
}

/* qm of Content-ID -> decoded message parts */
qm_kptr_t(part, lstr_t, lstr_t, qhash_lstr_hash, qhash_lstr_equal);

/** Convert IOP-XML to an IOP C structure with XML parts support.
 *
 * This function just works as iop_xunpack_flags() but supports XML parts
 * additionally. Parts must be given in a hashtable indexed by their
 * Content-ID.
 *
 * This function cannot be used to unpack a class; use `iop_xunpack_ptr_parts`
 * instead.
 *
 * \param[in] xp     The xml_reader_t setup on the XML data (see xmlr.h).
 * \param[in] mp     Memory pool to use for memory allocations.
 * \param[in] st     The IOP structure description.
 * \param[in] out    Pointer on the IOP structure to write.
 * \param[in] flags  Bitfield of flags to use (see iop_unpack_flags in iop.h).
 * \param[in] parts  Hashtable to retrieve XML parts.
 */
__must_check__
int iop_xunpack_parts(void * nonnull xp, mem_pool_t * nonnull mp,
                      const iop_struct_t * nonnull st,
                      void * nonnull out, int flags,
                      qm_t(part) * nonnull parts);

/** Convert IOP-XML to an IOP C structure with XML parts support.
 *
 * This function acts exactly as `iop_xunpack_parts` but allocates (or
 * reallocates) the destination structure.
 *
 * This function MUST be used to unpack a class instead of
 * `iop_xunpack_parts`, because the size of a class is not known before
 * unpacking it (this could be a child).
 */
__must_check__
int iop_xunpack_ptr_parts(void * nonnull xp, mem_pool_t * nonnull mp,
                          const iop_struct_t * nonnull st,
                          void * nullable * nonnull out, int flags,
                          qm_t(part) * nonnull parts);


/** iop_xunpack_flags() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack_flags(void * nonnull xp, const iop_struct_t * nonnull st,
                    void * nonnull out, int flags)
{
    return iop_xunpack_flags(xp, t_pool(), st, out, flags);
}

/** iop_xunpack() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack(void * nonnull xp, const iop_struct_t * nonnull st,
              void * nonnull out)
{
    return iop_xunpack(xp, t_pool(), st, out);
}

/** iop_xunpack_parts() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack_parts(void * nonnull xp, const iop_struct_t * nonnull st,
                    void * nonnull out, int flags, qm_t(part) * nonnull parts)
{
    return iop_xunpack_parts(xp, t_pool(), st, out, flags, parts);
}

/** iop_xunpack_ptr_flags() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack_ptr_flags(void * nonnull xp, const iop_struct_t * nonnull st,
                        void * nullable * nonnull out, int flags)
{
    return iop_xunpack_ptr_flags(xp, t_pool(), st, out, flags);
}

/** iop_xunpack_ptr() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack_ptr(void * nonnull xp, const iop_struct_t * nonnull st,
                  void * nullable * nonnull out)
{
    return iop_xunpack_ptr(xp, t_pool(), st, out);
}

/** iop_xunpack_ptr_parts() using the t_pool() */
__must_check__ static inline int
t_iop_xunpack_ptr_parts(void * nonnull xp, const iop_struct_t * nonnull st,
                        void * nullable * nonnull out, int flags,
                        qm_t(part) * nonnull parts)
{
    return iop_xunpack_ptr_parts(xp, t_pool(), st, out, flags, parts);
}

/* }}} */
/* {{{ Generating XML */

enum iop_xpack_flags {
    /* Generate verbose XML (with XSI types & co) */
    IOP_XPACK_VERBOSE           = (1U << 0),
    /* Use enums literal values when possible */
    IOP_XPACK_LITERAL_ENUMS     = (1U << 1),
    /** skip PRIVATE fields */
    IOP_XPACK_SKIP_PRIVATE      = (1U << 2),
};


/** Convert an IOP C structure to IOP-XML.
 *
 * This function packs an IOP structure into XML format. It assumes that you
 * have already written the root node with xsi pointing to
 * http://www.w3.org/2001/XMLSchema-instance and xsd to
 * http://www.w3.org/2001/XMLSchema.
 *
 * \param[out] sb          Buffer used to write the generated XML.
 * \param[in]  st          IOP structure definition.
 * \param[in]  v           Pointer on the IOP structure to pack.
 * \param[in]  flags       Bitfield of iop_pack_flags.
 *
 */
void iop_xpack_flags(sb_t * nonnull sb, const iop_struct_t * nonnull st,
                     const void * nonnull v, unsigned flags);

/** Convert an IOP C structure to IOP-XML.
 *
 * simpler interface for iop_xpack_flags
 */
void iop_xpack(sb_t * nonnull sb, const iop_struct_t * nonnull st,
               const void * nonnull v, bool verbose, bool with_enums);


/** RPC set for WSDL generation */
qh_k32_t(xwsdl_impl);

/** Generate the WSDL corresponding to an IOP module.
 *
 * \param[out] sb          Output buffer.
 * \param[in]  mod         IOP module description.
 * \param[in]  impl        Optional RPC set if you do not want to export the
 *                         whole module.
 * \param[in]  ns          WSDL namespace.
 * \param[in]  addr        WSDL address location.
 * \param[in]  with_auth   Add SOAP authentication headers.
 * \param[in]  with_enums  Dump enums literal representations in WSDL.
 */
void iop_xwsdl(sb_t * nonnull sb, const iop_mod_t * nonnull mod,
               qh_t(xwsdl_impl) * nullable impl, const char * nonnull ns,
               const char * nullable addr, bool with_auth, bool with_enums);

/** Register a module RPC in a RPC set.
 *
 * \param[in] h     RPC set.
 * \param[in] _mod  Module name.
 * \param[in] _if   Interface name.
 * \param[in] _rpc  RPC name.
 */
#define xwsdl_register(h, _mod, _if, _rpc) \
    qh_add(xwsdl_impl, h, IOP_RPC_CMD(_mod, _if, _rpc))

/* }}} */

#endif
