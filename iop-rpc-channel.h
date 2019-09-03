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

/* IOP Channels.
 *
 * An IOP Channel is used to execute some RPCs (Remote Procedure Call). Each
 * IOP module can implement multiple IOP interfaces which have IOP RPCs. To
 * allow an IChannel to accept RPCs, each RPC must be registered with its
 * interface in the IChannel. Note that the registration of RPCs does not
 * depends on its module. Thus, you MUST use at least one IChannel per module
 * (otherwise, you may call the wrong RPC).
 *
 * IOP Channels can be used either over network or Unix Domain sockets. See
 * Section 2 for more information.
 *
 * 1  IChannel packet format
 * =========================
 *
 * An IChannel packet is composed of a header followed by some payload.
 *
 * 1.1  Warning about endianness
 * -----------------------------
 *
 * Note that Intersec only supports little endian architectures. For this
 * reason, IChannels data are not encoded in network byte order but directly
 * in little endian.
 *
 * For example, in section 1.2, Flags is the most significant byte of a 32
 * bits little endian encoded field (32LE) and it's the last byte of the
 * network stream.
 *
 * 1.2  IC Header format: general case
 * -----------------------------------
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Flags     |                   Reserved                    | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Command                            | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0|                         Data length                         | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Payload...
 * +-+-+-+-+-+-+-
 *
 * The header format is at least composed of 12 bytes encoded as three words
 * of four bytes in little endian.
 *
 *     Flags   8 bits reserved for Flags. Defined flags      +-+-+-+-+-+-+-+-+
 *             are:                                          |  0  | D |C|B|A|
 *               - A (IC_MSG_HAS_FD): the IC embed a         +-+-+-+-+-+-+-+-+
 *                 file descriptor (Unix sockets only),
 *               - B (IC_MSG_HAS_HDR): the payload starts with an IC header,
 *               - C (IC_MSG_IS_TRACED): the IC is traced,
 *               - D (IC_MSG_PRIORITY): the IC priority; messages with high
 *                 priority (in the sense of EV_PRIORITY) are sent first; this
 *                 field propagate the priority such that high priority
 *                 responses are also sent first (but not parsed first).
 *
 *     Reserved  Depends on the Command.
 *
 *     Command  The type of the message.
 *                - If Command > 0, then the message is a query,
 *                - If Command == 0x80000000, then the message is a stream
 *                  control message,
 *                - Otherwise, the message is a reply.
 *
 *     Data length  The length of the payload. For compatibility purposes with
 *                  version 0, the sign bit MUST be 0.
 *
 *     Payload  Depends on the Command. It may be part of the header or being
 *              binary-packed IOPs (i.e. TLVs).
 *
 * Note: in fact, Command defines both the type of the message and, if it is a
 * query, the RPC called and its interface (see 1.3).
 *
 * 1.3  Query message (Command > 0)
 * --------------------------------
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Flags     |                     Slot                      | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0|         Interface           |0|           RPC               | = Command
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   ↳ 32LE
 * |0|                         Data length                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Payload...
 * +-+-+-+-+-+-+-
 *
 *     Flags    See 1.2.
 *
 *     Slot     The IC slot of the query or response (IC_MSG_SLOT_MASK).
 *
 *     Interface  The RPC interface on 15 bits.
 *
 *     RPC      The RPC tag on 15 bits.
 *
 *     Data length  The length of the Payload.
 *
 *     Payload  Contains the RPC query. If B (IC_MSG_HAS_HDR) is set, the
 *              payload starts with an IC-internal IOP header (see ic.iop).
 *
 * 1.4  Reply message
 * ------------------
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Flags     |                     Slot                      | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Status                             | = Command
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+   ↳ 32LE
 * |0|                         Data length                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Payload...
 * +-+-+-+-+-+-+-
 *
 *     Flags    See 1.2.
 *
 *     Slot     The IC slot of the query or response (IC_MSG_SLOT_MASK).
 *
 *     Status   The status of the response, defined by the ic_status_t enum.
 *
 *     Data length  The length of the Payload.
 *
 *     Payload  Contains the RPC response (out). If B (IC_MSG_HAS_HDR) is set,
 *              the payload starts with an IC-internal IOP header (see
 *              ic.iop).
 *
 * 1.5  Stream control message
 * ---------------------------
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Flags     |                     Type                      | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           0x80000000                          | = Command
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0|                         Data length                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Stream control messages are used for internal IC purposes. It is defined as
 * an IC message having IC_MSG_STREAM_CONTROL (0x80000000) as Command. The
 * message type is given by the ic_msg_sc_slots enum.
 *
 * 1.5.1  Bye message
 *
 * The Bye message indicates that the remote peer will shutdown in a very few
 * time and will not send any further data. It has no payload.
 *
 * 1.5.2  Nop message
 *
 * The nop message MUST be silently ignored. It was sent by version 0 at
 * connection establishment to force a message exchange.
 *
 * 1.5.3  Version message
 *
 * This message is introduced in version 1.
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Flags     |             Type = IC_SC_VERSION              | } 32LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           0x80000000                          | = Command
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Data length = 2                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |          Version = 1          |T|          Reserved           | } 2x16LE
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * This MUST be the very first message sent by both the server and the
 * client. If not, then the remote version is 0 and each flag is considered
 * unset.
 *
 *     Type    Set to IC_SC_VERSION.
 *
 *     Reserved  See 1.2.
 *
 *     Version  The version of the IC channel (IC_VERSION).
 *
 *     T       Indicate that TLS is required on this connection. If one peer
 *             require TLS, then both peers proceed to the TLS handshake (or
 *             close the connection).
 *
 *     Reserved  MUST be set to 0, reserved for future use.
 *
 * 2  IChannel connection establishment
 * ====================================
 *
 * Connection establishment is different according to whether we use Network
 * or Unix Domain sockets. There is no version exchange on Unix Domain
 * sockets.
 *
 * 2.1 Network sockets
 * -------------------
 *
 * Once the TCP connection is established, the server and the client
 * immediately send a version message (1.5.3). If the first message received
 * from the remote peer is not a version message, then the remote peer has
 * version 0.
 *
 * The client and the server MUST NOT send any other data before the complete
 * parsing of the version message sent by their peer. Note they may received
 * more data from their peer than just the version message.
 *
 * 2.2 Unix Domain sockets
 * -----------------------
 *
 * We assume that processes of the same host have the same IC version. Thus,
 * we skip version handshake in the case of Unix sockets.
 *
 * Also, if you have two connected Unix Domain sockets (e.g. obtained with
 * socketpairx), you can call ic_spawn to have two connected IChannels. Note
 * that you may use either SOCK_STREAM or SOCK_SEQPACKETS (the latter may be
 * used to send file descriptors).
 *
 * 3  Extensibility
 * ================
 *
 * There is several ways to extend the IC format message: increasing the
 * version number, using trailing space and using reserved fields.
 *
 * The version number is strictly increasing and it is believed that any
 * version knows about its forefathers. The initial version exchange (see
 * 1.5.3) allows the most recent peer to know which message it can use and
 * which it cannot. Especially, a newer version MUST NOT send messages ignored
 * by the older version, but it MAY reject older messages.
 *
 * Messages which does not yet have payload MAY be extended: the trailing data
 * are ignored.
 *
 * It is possible to use Reserved fields to extend messages: reserved space is
 * ignored.
 *
 * 4  Versions, bugs and tricks
 * ============================
 *
 * 4.1 Version 0
 * -------------
 *
 * Version 0 does not have Version messages and thus concerns all IOP Channels
 * versions before version 1. We know that a peer has version 0 because it's
 * first message is not a Version message (see 1.5.3).
 *
 * Version 0 uses Data length as a signed integer and does not check it is
 * positive. The most significant bit of Data length MUST be 0.
 *
 * Version 0 closes the connection if new Flags are defined.
 *
 * Version 0 closes the connection if new IC_MSG are defined (except if your
 * message is huge (>= 10 << 20)).
 *
 * 4.2 Version 1
 * -------------
 *
 * Version 1 introduces Version messages and TLS cryptography (see 1.5.3).
 *
 * This is the last version: all this documentation applies to Version 1.
 */

#if !defined(IS_LIB_COMMON_IOP_RPC_H) || defined(IS_LIB_COMMON_IOP_RPC_CHANNEL_H)
#  error "you must include <lib-common/iop-rpc.h> instead"
#else
#define IS_LIB_COMMON_IOP_RPC_CHANNEL_H

#include <lib-common/container-htlist.h>
#include <openssl/ssl.h>

#if 0
#define IC_DEBUG_REPLIES
#endif
#ifdef IC_DEBUG_REPLIES
qh_k64_t(ic_replies);
#endif

typedef struct ichannel_t    ichannel_t;
typedef struct ic_msg_t      ic_msg_t;
typedef struct ic_hook_ctx_t ic_hook_ctx_t;

typedef enum ic_event_t {
    IC_EVT_CONNECTED,
    IC_EVT_DISCONNECTED,
    /* used to notify of first activity when using soft wa */
    IC_EVT_ACT,
    /* used to notify no activity when using soft wa       */
    IC_EVT_NOACT,
} ic_event_t;

#define IC_VERSION                  1

#define IC_MSG_HDR_LEN             12
#define IC_MSG_CMD_OFFSET           4
#define IC_MSG_DLEN_OFFSET          8
#define IC_PKT_MAX              65536

#define IC_ID_MAX               BITMASK_LE(uint32_t, 30)
#define IC_MSG_SLOT_MASK        (0xffffffU)
#define IC_MSG_HAS_FD           (1U << 24)
#define IC_MSG_HAS_HDR          (1U << 25)
#define IC_MSG_IS_TRACED        (1U << 26)
#define IC_MSG_PRIORITY_SHIFT   27
#define IC_MSG_PRIORITY_MASK    (BITMASK_LT(uint32_t,                        \
                                            2) << IC_MSG_PRIORITY_SHIFT)

#define IC_SC_VERSION_TLS  (1U << 15)

#define IC_PROXY_MAGIC_CB       ((ic_msg_cb_f *)-1)

typedef struct ic_creds_t {
    uid_t uid;
    gid_t gid;
    int   pid;
} ic_creds_t;

typedef void (ic_hook_f)(ichannel_t * nonnull, ic_event_t evt);
typedef void (ic_pre_hook_f)(ichannel_t * nullable, uint64_t,
                             ic__hdr__t * nullable, data_t);
typedef void (ic_post_hook_f)(ichannel_t * nullable, ic_status_t,
                              ic_hook_ctx_t * nonnull, data_t,
                              const iop_struct_t * nullable,
                              const void * nullable);
typedef int (ic_creds_f)(ichannel_t * nonnull,
                         const ic_creds_t * nonnull creds);
typedef void (ic_msg_cb_f)(ichannel_t * nonnull, ic_msg_t * nonnull,
                           ic_status_t, void * nullable, void * nullable);
#ifdef __has_blocks
typedef void (BLOCK_CARET ic_msg_cb_b)(ichannel_t * nonnull, ic_status_t,
                                       void * nullable, void * nullable);
#endif

struct ic_msg_t {
    htnode_t      msg_link;        /**< private field used by ichannel_t */
    int           fd         : 24; /**< the fd to send */
    bool          async      :  1; /**< whether the RPC is async */
    bool          raw        :  1; /**< whether the answer should be decoded
                                        or not. */
    bool          force_pack :  1; /**< if set then msg is packed even if it
                                        is used with a local ic */
    bool          force_dup  :  1; /**< if set when ic is local and force_pack
                                        is false then hdr and arg are
                                        duplicated before being used in rpc
                                        implementation */
    bool          trace      :  1; /**< Activate tracing for this message. */
    bool          canceled   :  1; /**< Is the query canceled ? */
    ev_priority_t priority   :  2; /**< Priority of the message. */
    int32_t  cmd;                  /**< automatically filled by ic_query/reply
                                        */
    uint32_t slot;                 /**< automatically filled by ic_query/reply
                                        */
    uint32_t timeout;              /**< max lifetime of the query */
    ichannel_t * nullable ic;      /**< the ichannel_t used for the query */
    el_t nullable timeout_timer;
    unsigned dlen;
    void    * nullable data;
    pstream_t raw_res;

    /* user provided fields */
    const iop_rpc_t  * nullable rpc;
    const ic__hdr__t * nullable hdr;
    ic_msg_cb_f      * nullable cb;
    byte              priv[];
};
ic_msg_t * nonnull ic_msg_new(int len);
#define ic_msg_p(_t, _v)                                                     \
    ({                                                                       \
        ic_msg_t *_msg = ic_msg_new(sizeof(_t));                             \
        *acast(_t, _msg->priv) = *(_v);                                      \
        _msg;                                                                \
    })
#define ic_msg(_t, ...)  ic_msg_p(_t, (&(_t){ __VA_ARGS__ }))

#ifdef __has_blocks
ic_msg_t * nonnull ic_msg_new_blk(ic_msg_cb_b nonnull blk);
#endif
ic_msg_t * nonnull ic_msg_new_fd(int fd, int len);
ic_msg_t * nonnull ic_msg_proxy_new(int fd, uint64_t slot,
                                    const ic__hdr__t * nullable hdr);
void ic_msg_delete(ic_msg_t * nullable * nonnull);

/** Cancel an ic message.
 *
 * Flag an ic_msg_t as "canceled". The message will not be sent or, if it is
 * already sent, the potential answer will be dropped.
 *
 * \param[in]  msg the message to cancel.
 */
void ic_msg_cancel(ic_msg_t * nonnull msg);

/** Set a timeout for ic_msg_t.
 *
 *  When sending a query with such a ic_msg_t, the query will fail with the
 *  IC_MSG_TIMEDOUT error if it is not entirely processed after 'timeout'
 *  milliseconds.
 *
 * \param[in]  msg      the ic_msg_t on which the timeout should be set.
 * \param[in]  timeout  the maximum lifetime, in milliseconds, of the query in
 *                      the ichannel.
 *                      Upon expiration of this period, the query will fail
 *                      with IC_MSG_TIMEDOUT if it hasn't been processed yet.
 *
 * \return the ic_msg_t with the timeout set.
 */
ic_msg_t * nonnull ic_msg_set_timeout(ic_msg_t * nonnull msg,
                                      uint32_t timeout);

/** Set a priority for ic_msg_t.
 *
 * When adding a message to the queue, messages with a high priority level
 * will be at the head, just after the other messages having the same priority
 * level, while messages with a low priority level will be at the tail. As a
 * consequence, if there is a lot of messages with a high priority level,
 * messages with a low or normal priority will never be pop'ed from the queue.
 *
 * \param[in]  msg       the ic_msg_t on which the priority should be set.
 * \param[in]  priority  the priority to set.
 *
 * \return the ic_msg_t with the priority set.
 */
ic_msg_t * nonnull ic_msg_set_priority(ic_msg_t * nonnull msg,
                                       ev_priority_t priority);

qm_k32_t(ic_msg, ic_msg_t * nonnull);

struct ic_hook_ctx_t {
    uint64_t         slot;
    ic_post_hook_f  * nullable post_hook;
    const iop_rpc_t * nonnull rpc;
    data_t           post_hook_args;
    byte             data[];  /* data to pass through RPC workflow */
};

int ic_hook_ctx_save(ic_hook_ctx_t * nonnull ctx);
ic_hook_ctx_t * nonnull ic_hook_ctx_new(uint64_t slot, ssize_t extra);
ic_hook_ctx_t * nullable ic_hook_ctx_get(uint64_t slot);
void ic_hook_ctx_delete(ic_hook_ctx_t * nullable * nonnull pctx);

typedef enum ic_cb_entry_type_t {
    IC_CB_NORMAL,
    IC_CB_NORMAL_BLK,
    IC_CB_PROXY_P,
    IC_CB_PROXY_PP,
    IC_CB_DYNAMIC_PROXY,
    IC_CB_WS_SHARED,
} ic_cb_entry_type_t;

typedef struct ic_dynproxy_t {
    ichannel_t * nullable ic;
    ic__hdr__t * nullable hdr;
} ic_dynproxy_t;

/** Callback to fetch a dynamic proxy (pair of ichannel + header).
 *
 * This function is allowed to return an header allocated on the t_pool() just
 * like a t_ function.
 */
typedef ic_dynproxy_t (ic_dynproxy_f)(ic__hdr__t * nullable hdr,
                                      void * nullable priv);

#define IC_DYNPROXY_NULL    ((ic_dynproxy_t){ .ic = NULL })
#define IC_DYNPROXY(_ic)    ((ic_dynproxy_t){ .ic = (_ic) })
#define IC_DYNPROXY_HDR(_ic, _hdr) \
    ((ic_dynproxy_t){ .ic = (_ic), .hdr = (_hdr) })

typedef struct ic_cb_entry_t {
    ic_cb_entry_type_t cb_type;
    const iop_rpc_t * nonnull rpc;

    ic_pre_hook_f  * nullable pre_hook;
    ic_post_hook_f * nullable post_hook;
    data_t          pre_hook_args;
    data_t          post_hook_args;
    union {
        struct {
            void (* nonnull cb)(ichannel_t * nonnull, uint64_t,
                                void * nullable, const ic__hdr__t * nullable);
        } cb;

#ifdef __has_blocks
        struct {
            void (BLOCK_CARET nonnull __unsafe_unretained cb)
                (ichannel_t * nonnull, uint64_t, void * nullable,
                 const ic__hdr__t * nullable);
        } blk;
#endif

        struct {
            ichannel_t * nonnull ic_p;
            ic__hdr__t * nullable hdr_p;
        } proxy_p;

        struct {
            ichannel_t * nullable * nonnull ic_pp;
            ic__hdr__t * nullable * nullable hdr_pp;
        } proxy_pp;

        struct {
            ic_dynproxy_f * nonnull get_ic;
            void * nullable priv;
        } dynproxy;

        struct {
            void (* nonnull cb)(void * nullable, uint64_t, void * nullable,
                                const ic__hdr__t * nullable);
        } iws_cb;

#ifdef __has_blocks
        struct {
            void (BLOCK_CARET __unsafe_unretained nonnull cb)
                (ichannel_t * nullable, uint64_t, void * nullable,
                 const ic__hdr__t * nullable);
        } iws_blk;
#endif
    } u;
} ic_cb_entry_t;
qm_k32_t(ic_cbs, ic_cb_entry_t);
extern qm_t(ic_cbs) const ic_no_impl;

struct ichannel_t {
    uint32_t id;

    bool is_closing   :  1;
    bool is_spawned   :  1;   /**< auto delete if true; contrarily to what is
        displayed with ic_get_state(), it does not always indicate that the
        ichannel is actually server-side but really that it should be
        autodeleted (if no_autodel is false) */
    bool no_autodel   :  1;   /**< disable autodelete feature             */
    bool is_seqpacket :  1;   /**< true if socket is SOCK_SEQPACKET       */
    bool is_unix      :  1;   /**< true if socket is a Unix socket        */
    bool auto_reconn  :  1;
    bool do_el_unref  :  1;
    bool is_wiped     :  1;
    bool cancel_guard :  1;
    bool queuable     :  1;   /**< indicate that the IC is ready to send
                                   messages, but some process does enqueue
                                   messages before the IC being queuable */
    bool is_local     :  1;
    bool is_trusted   :  1;   /**< set to true for internal ichannels     */
    bool is_public    :  1;   /**< setting this flag to true causses private
                                   fields to be omitted on outgoing messages
                                   and forbidden on incoming messages */
    bool fd_overflow  :  1;
    bool hdr_checked  :  1;   /**< read checks are successful */
    bool tls_required :  1;   /**< ignored on non TCP sockets */
    bool is_connected :  1;   /**< true if handshakes are completed */

    unsigned nextslot;          /**< next slot id to try                    */

    el_t              nullable elh;
    el_t              nullable timer;
    ichannel_t      * nullable * nullable owner;
                                /**< content set to NULL on deletion        */
    void             * nullable priv;
                                /**< user private data                      */
    void             * nullable peer;
                                /**< user field to identify the peer        */
    const iop_rpc_t  * nullable desc;
                                /**< desc of the current unpacked RPC       */
    int               cmd;      /**< cmd of the current unpacked structure  */
    ev_priority_t     priority; /**< priority of the channel                */

    el_t nullable wa_soft_timer;
    int  wa_soft;             /**< to be notified when no activity          */
    int  wa_hard;             /**< to close the connection when no activity */

    uint16_t     peer_version; /**< version of the remote peer */
    int          protocol;     /**< transport layer protocol (0 = default) */
    int          retry_delay;  /**< delay before a reconnection attempt (ms) */
    sockunion_t  su;
    const qm_t(ic_cbs) * nullable impl;
    ic_hook_f   * nonnull on_event;
    ic_creds_f  * nullable on_creds;
    void        (* nullable on_wipe)(ichannel_t * nonnull ic);

    /* private */
    qm_t(ic_msg) queries;      /**< hash of queries waiting for an answer  */
    htlist_t     iov_list;     /**< list of messages to send, in iov       */
    htlist_t     msg_list;     /**< list of messages to send               */
    htnode_t    * nullable last_normal_prio_msg;
                               /**< last message of msg_list having
                                             the priority NORMAL */
    int current_fd; /**< used to store the current fd                       */
    int pending;    /**< number of pending queries (for peak warning)       */
    int queue_len;  /**< length of the query queue, without canceled        */
    SSL * nullable ssl; /**< TLS context, if any. */

    /* Buffers */
    qv_t(i32)    fds;
    qv_t(iovec)  iov;
    int          iov_total_len;
    sb_t         rbuf;

    lstr_t       peer_address;
#ifdef IC_DEBUG_REPLIES
    qh_t(ic_replies) dbg_replies;
#endif
#ifndef NDEBUG
    int pending_max;
#endif
};

void ic_drop_ans_cb(ichannel_t * nonnull, ic_msg_t * nonnull,
                    ic_status_t, void * nullable, void * nullable);

MODULE_DECLARE(ic);

/*----- ichannel handling -----*/

static inline bool ic_is_local(const ichannel_t * nonnull ic) {
    return ic->is_local;
}

static inline void ic_set_local(ichannel_t * nonnull ic) {
    ic->is_local = true;
    ic->peer_address = LSTR("127.0.0.1");
}

static inline int ic_get_fd(ichannel_t * nonnull ic) {
    int res = ic->current_fd;
    ic->current_fd = -1;
    return res;
}
static inline int ic_queue_len(ichannel_t * nonnull ic) {
    return ic->queue_len;
}
static inline bool ic_is_empty(ichannel_t * nonnull ic) {
    return htlist_is_empty(&ic->msg_list)
        && htlist_is_empty(&ic->iov_list)
        && ic_queue_len(ic) == 0
        && !ic->pending;
}

/* XXX be carefull, this function do not mean that the ichannel is actually
 * connected, just that you are allowed to queue some queries.
 *
 * To check if the IC is actually connected (TLS handshakes finished), use the
 * `ic->is_connected` flag.
 */
static inline bool ic_is_ready(const ichannel_t * nonnull ic) {
    return (ic_is_local(ic) && ic->impl)
        || (ic->elh && ic->queuable && !ic->is_closing);
}

static inline bool ic_slot_is_async(uint64_t slot) {
    return !(slot & IC_MSG_SLOT_MASK);
}

/** \brief watch the incoming activity of an ichannel.
 *
 * If a positive soft timeout is given, the on_event callback will be called
 * with IC_EVT_NOACT when an inactivity period of timeout_soft milliseconds
 * is detected, and with IC_EVT_ACT when an activity occurs after a period
 * of inactivity.
 *
 * If a positive hard timeout is given, the ichannel connection will be
 * automatically closed if an inactivity period of timeout_hard milliseconds
 * is detected.
 *
 * If one of the two given timeouts is positive, some outgoing traffic will
 * be generated each timeout / 3 milliseconds.
 *
 * In general, this function should be called with the same arguments on both
 * client and server side.
 */
void ic_watch_activity(ichannel_t * nonnull ic, int timeout_soft,
                       int timeout_hard);

ev_priority_t ic_set_priority(ichannel_t * nonnull ic, ev_priority_t prio);
ichannel_t * nullable ic_get_by_id(uint32_t id);
ichannel_t * nonnull ic_init(ichannel_t * nonnull);

/* Disconnect the IC (close the socket) and wipe it. */
void ic_wipe(ichannel_t * nonnull);
GENERIC_NEW(ichannel_t, ic);

__attr_nonnull__((1))
static inline void ic_delete(ichannel_t * nullable * nonnull icp)
{
    ichannel_t *ic = *icp;

    if (ic) {
        ic_wipe(ic);
        /* XXX never delete icp which may be set to NULL by ic_wipe() through
         * ic->owner. */
        p_delete(&ic);
        *icp = NULL;
    }
}

/** Connect an IC to an IC server.
 *
 * If the IChannel uses Unix Domain socket (is_unix == true), the connect(2)
 * system call is blocking and no version handshake is performed: the IC is
 * immediately connected.
 *
 * \param[in]  ic  The (initialized) ichannel structure.
 */
int  ic_connect(ichannel_t * nonnull ic);

/** Connect an IC to an IC server in a blocking manner.
 *
 * \param[in]  ic  The (initialized) ichannel structure.
 * \param[in]  timeout  The time, in seconds, after which the connection
 *                      should timeout; 0 means default (i.e. 60s).
 **/
int  ic_connect_blocking(ichannel_t * nonnull ic, int timeout);

void ic_disconnect(ichannel_t * nonnull ic);

/** Setup an IC for a connected socket.
 *
 * It should be used after an accept() (server side) but can also be used on
 * two connected Unix Domain sockets.
 *
 * \param[inout]  ic  The ichannel structure to bind to the socket.
 * \param[in]  fd  A connected socket (server-side).
 * \param[in]  creds_fn  the on_creds callback.
 */
void ic_spawn(ichannel_t * nonnull ic, int fd, ic_creds_f * nullable fn);
void ic_bye(ichannel_t * nonnull);
void ic_nop(ichannel_t * nonnull);

el_t nullable
ic_listento(const sockunion_t * nonnull su, int type, int proto,
            int (*nonnull on_accept)(el_t nonnull ev, int fd));

/** Synchronously write everything in queue.
 *
 * The socket MUST be connected, i.e. ic->is_connected must be true: you may
 * use a connect blocking or wait for the IC_EVT_CONNECTED event. On
 * termination, you may simply skip flushing data if the IC is not yet
 * connected.
 *
 * \param[in]  ic  The IC to flush.
 */
void ic_flush(ichannel_t * nonnull ic);
lstr_t ic_get_client_addr(ichannel_t * nonnull ic);

/** Mark an ichannel_t as disconnected.
 *
 * This function ensures that the ichannel_t is properly disconnected and
 * restart the connection procedure if needed. This is useful when having a
 * connected but not properly started ichannel (handshake failure for
 * example).
 *
 * \param[in]  ic The ichannel_t to mark as disconnected.
 */
void ic_mark_disconnected(ichannel_t * nonnull ic);

/*----- rpc handling / registering -----*/

/** \brief builds the typed argument list of the implementation of an rpc.
 *
 * This macro builds the arguments of an rpc implementing the server-side of
 * an IC RPC.
 *
 * For example, for this iop:
 * <code>
 * package pkg;
 *
 * interface If {
 *     funCall in ... out ... throw ...;
 * }
 *
 * module mod {
 *     If  myIface;
 * }
 * </code>
 *
 * Then one can define the following suitable implementation callback for
 * pkg.mod.myIface.funCall:
 *
 * <code>
 * void rpc_impl(IOP_RPC_IMPL_ARGS(pkg__mod, my_iface, fun_call))
 * {
 *     ic_reply_err(ic, slot, IC_MSG_UNIMPLEMENTED);
 * }
 * </code>
 *
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \return
 *   typed arguments of the implementation callback: (ic, slot, arg, hdr).
 *   - \v ic is the name of the calling iop-channel (to be used for
 *     synchronous replies).
 *   - \v slot is the slot of the query, to be ised in the ic_reply call.
 *   - \v arg the properly typed unfolded argument of the query. Note that the
 *     argument is allocated on the stack, hence should be duplicated in the
 *     implementation callback if needed.
 *   - \v hdr the IC query header (if any).
 */
#define IOP_RPC_IMPL_ARGS(_mod, _i, _r)                                      \
    ichannel_t * nonnull ic, uint64_t slot,                                  \
    IOP_RPC_T(_mod, _i, _r, args) * nullable arg,                            \
    const ic__hdr__t * nullable hdr

/** \brief builds the typed argument list of the reply callback of an rpc.
 *
 * This macro builds the arguments of a reply callback (client side) of an IC
 * RPC.
 *
 * For example, for this iop:
 * <code>
 * package pkg;
 *
 * interface If {
 *     funCall in ... out ... throw ...;
 * }
 *
 * module mod {
 *     If  myIface;
 * }
 * </code>
 *
 * Then one can define the following suitable reply callback for
 * pkg.mod.myIface.funCall:
 *
 * <code>
 * void rpc_answer_cb(IOP_RPC_CB_ARGS(pkg__mod, my_iface, fun_call))
 * {
 *     if (status == IC_MSG_OK) {
 *         // one can use res here;
 *     } else
 *     if (status == IC_MSG_EXN) {
 *         // one can use exn here;
 *     } else {
 *         // deal with transport error
 *     }
 * }
 * </code>
 *
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \return
 *   typed arguments of the reply callback: (ic, msg, status, res, exn).
 *   - \v ic is the called iop-channel (shouldn't actually be used!).
 *   - \v msg is the original message sent with ic_query. It doesn't need to
 *     be freed, though if data is put in the "priv" pointer it must be
 *     reclaimed in the reply callback.
 *   - \v status is the status of the reply.
 *   - \v res is the value of the callback result when \v status is
 *     #IC_MSG_OK, and should not be accessed otherwise.
 *   - \v exn is the value of the callback result when \v status is
 *     #IC_MSG_EXN, and should not be accessed otherwise.
 */
#define IOP_RPC_CB_ARGS(_mod, _i, _r)                                        \
    ichannel_t * nonnull ic, ic_msg_t * nonnull msg, ic_status_t status,     \
    IOP_RPC_T(_mod, _i, _r, res) * nullable res,                             \
    IOP_RPC_T(_mod, _i, _r, exn) * nullable exn

/* some useful macros to define IOP rpcs and callbacks */

/** \brief builds an RPC name.
 * \param[in]  _m     name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  sfx    a unique suffix to distinguish usages (cb, impl, ...)
 */
#define IOP_RPC_NAME(_m, _i, _r, sfx)  _m##__##_i##__##_r##__##sfx

/** \brief builds an RPC Implementation prototype.
 * \param[in]  _m     name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 */
#define IOP_RPC_IMPL(_m, _i, _r) \
    IOP_RPC_NAME(_m, _i, _r, impl)(IOP_RPC_IMPL_ARGS(_m, _i, _r))

/** \brief builds an RPC Callback prototype.
 * \param[in]  _m     name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 */
#define IOP_RPC_CB(_m, _i, _r) \
    IOP_RPC_NAME(_m, _i, _r, cb)(IOP_RPC_CB_ARGS(_m, _i, _r))

/*
 * XXX this is an ugly piece of preprocessing because we lack templates and
 *     because __builtin_choose_expr generates syntaxic errors *sigh*.
 *
 * First IOP_RPC_CB_REF calls IOP_RPC_CB_REF_ with the async-ness of the rpc
 * as a macro.
 *
 * We recurse into IOP_RPC_CB_REF__ so that 'async' evaluates to 0 or 1.
 *
 * We catenate it to IOP_RPC_CB_REF__ which means it will either:
 * - evaluate to IOP_RPC_CB_REF__0(...) -> build cb name
 * - evaluate to IOP_RPC_CB_REF__1(...) -> NULL
 */
#define IOP_RPC_CB_REF__0(_m, _i, _r)        IOP_RPC_NAME(_m, _i, _r, cb)
#define IOP_RPC_CB_REF__1(_m, _i, _r)        NULL
#define IOP_RPC_CB_REF__(async, _m, _i, _r)  IOP_RPC_CB_REF__##async(_m, _i, _r)
#define IOP_RPC_CB_REF_(async, _m, _i, _r)   IOP_RPC_CB_REF__(async, _m, _i, _r)

/** \brief builds an RPC callback reference (NULL if the RPC is async).
 * \param[in]  _m     name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 */
#define IOP_RPC_CB_REF(_m, _i, _r) \
    IOP_RPC_CB_REF_(_m##__##_i(_r##__rpc__async), _m, _i, _r)

/** \brief register local callback and pre/post hooks for an rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  _cb
 *    the implementation callback. Its type should be:
 *    <tt>void (*)(IOP_RPC_IMPL_ARGS(_mod, _if, _rpc))</tt>
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *,
 *                 const iop_struct_t * nullable, const void * nullable)</tt>
 *    see #ic_query_do_post_hook about how to use the last two arguments.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ic_register_pre_post_hook_(h, _mod, _if, _rpc, _cb,                  \
                                   _pre_cb, _post_cb, _pre_arg, _post_arg)   \
    do {                                                                     \
        void (*__cb)(IOP_RPC_IMPL_ARGS(_mod, _if, _rpc)) = _cb;              \
        uint32_t cmd    = IOP_RPC_CMD(_mod, _if, _rpc);                      \
        ic_cb_entry_t e = {                                                  \
            .cb_type = IC_CB_NORMAL,                                         \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .cb = {                                                   \
                .cb  = (void *)__cb,                                         \
            } },                                                             \
        };                                                                   \
        e_assert_n(panic, qm_add(ic_cbs, h, cmd, e),                         \
                   "collision in RPC registering");                          \
    } while (0)

/** \brief same as #ic_register_pre_post_hook_ but _pre and _post args
 *    will be transform into data_t ptr.
 */
#define ic_register_pre_post_hook_p_(h, _mod, _if, _rpc, _cb,                \
                                     _pre_cb, _post_cb, _pre_arg, _post_arg) \
    ic_register_pre_post_hook_(h, _mod, _if, _rpc, _cb,                      \
                               _pre_cb, _post_cb,                            \
                               { .ptr = _pre_arg }, { .ptr = _post_arg })

/** \brief same as #ic_register_pre_post_hook_ but auto-computes the
 *    rpc name.
 */
#define ic_register_pre_post_hook(h, _m, _i, _r, _pre_cb,                    \
                                  _post_cb, _pre_arg, _post_arg)             \
    ic_register_pre_post_hook_(h, _m, _i, _r,                                \
                               IOP_RPC_NAME(_m, _i, _r, impl),               \
                               _pre_cb, _post_cb, _pre_arg, _post_arg)
/** \brief same as #ic_register_pre_post_hook_p_ but auto-computes the
 *    rpc name.
 */
#define ic_register_pre_post_hook_p(h, _m, _i, _r, _pre_cb,                  \
                                    _post_cb, _pre_arg, _post_arg)           \
    ic_register_pre_post_hook(h, _m, _i, _r, _pre_cb, _post_cb,              \
                              { .ptr = _pre_arg }, { .ptr = _post_arg })

/** \brief same as #ic_register_pre_post_hook_ but doesn't register pre/post
 *    hooks.
 */
#define ic_register_(h, _mod, _if, _rpc, _cb)                                \
    ic_register_pre_post_hook_p_(h, _mod, _if, _rpc, _cb,                    \
                                 NULL, NULL, NULL, NULL)

/** \brief same as #ic_register_ but auto-computes the rpc name. */
#define ic_register(h, _m, _i, _r) \
    ic_register_(h, _m, _i, _r, IOP_RPC_NAME(_m, _i, _r, impl))

/** \brief unregister a local callback for an rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation of which you want to unregister the
 *    rpc implementation.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 */
#define ic_unregister(h, _mod, _if, _rpc) \
    do {                                                                     \
        uint32_t cmd = IOP_RPC_CMD(_mod, _if, _rpc);                         \
        qm_del_key(ic_cbs, h, cmd);                                          \
    } while (0)

/** \brief register a proxy destination for the given rpc with forced header.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *,
 *                 const iop_struct_t * nullable, const void * nullable)</tt>
 *    see #ic_query_do_post_hook about how to use the last two arguments.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ic_register_proxy_hdr_pre_post_hook(h, _mod, _if, _rpc, ic, hdr,     \
                                            _pre_cb, _post_cb,               \
                                            _pre_arg, _post_arg)             \
    do {                                                                     \
        uint32_t cmd    = IOP_RPC_CMD(_mod, _if, _rpc);                      \
        ic_cb_entry_t e = {                                                  \
            .cb_type = IC_CB_PROXY_P,                                        \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .proxy_p = { .ic_p = ic, .hdr_p = hdr } },                \
        };                                                                   \
        qm_add(ic_cbs, h, cmd, e);                                           \
    } while (0)

/** \brief same as #ic_register_proxy_hdr_pre_post_hook but _pre and _post
 *    args will be transform into data_t ptr.
 */
#define ic_register_proxy_hdr_pre_post_hook_p(h, _mod, _if, _rpc, ic, hdr,   \
                                              _pre_cb, _post_cb,             \
                                              _pre_arg, _post_arg)           \
    ic_register_proxy_hdr_pre_post_hook(h, _mod, _if, _rpc, ic, hdr,         \
                                        _pre_cb, _post_cb,                   \
                                        { .ptr = _pre_arg },                 \
                                        { .ptr = _post_arg })

/** \brief same as #ic_register_proxy_hdr_pre_post_hook but don't set
 * the hdr.
 */
#define ic_register_proxy_pre_post_hook(h, _mod, _if, _rpc, ic, _pre_cb,     \
                                        _post_cb, _pre_arg, _post_arg)       \
    ic_register_proxy_hdr_pre_post_hook(h, _mod, _if, _rpc, ic, NULL,        \
                                        _pre_cb, _post_cb,                   \
                                        _pre_arg, _post_arg)

/** \brief same as #ic_register_proxy_hdr_pre_post_hook but _pre and _post
 *    args will be transform into data_t ptr.
 */
#define ic_register_proxy_pre_post_hook_p(h, _mod, _if, _rpc, ic, _pre_cb,   \
                                          _post_cb, _pre_arg, _post_arg)     \
    ic_register_proxy_hdr_pre_post_hook(h, _mod, _if, _rpc, ic, NULL,        \
                                        _pre_cb, _post_cb,                   \
                                        { .ptr = _pre_arg },                 \
                                        { .ptr = _post_arg })
/** \brief register a proxy destination for the given rpc with forced header.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 */
#define ic_register_proxy_hdr(h, _mod, _if, _rpc, ic, hdr)                   \
    ic_register_proxy_hdr_pre_post_hook_p(h, _mod, _if, _rpc, ic,            \
                                          hdr, NULL, NULL, NULL, NULL)

/** \brief register a proxy destination for the given rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 */
#define ic_register_proxy(h, _mod, _if, _rpc, ic) \
    ic_register_proxy_hdr(h, _mod, _if, _rpc, ic, NULL)

/** \brief register a pointed proxy destination for the given rpc with header.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *,
 *                 const iop_struct_t * nullable, const void * nullable)</tt>
 *    see #ic_query_do_post_hook about how to use the last two arguments.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ic_register_proxy_hdr_p_pre_post_hook(h, _mod, _if, _rpc, ic, hdr,   \
                                              _pre_cb, _post_cb,             \
                                              _pre_arg, _post_arg)           \
    do {                                                                     \
        uint32_t cmd    = IOP_RPC_CMD(_mod, _if, _rpc);                      \
        ic_cb_entry_t e = {                                                  \
            .cb_type = IC_CB_PROXY_PP,                                       \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .proxy_pp = { .ic_pp = ic, .hdr_pp = hdr } },             \
        };                                                                   \
        qm_add(ic_cbs, h, cmd, e);                                           \
    } while (0)

/** \brief same as #ic_register_proxy_hdr_p_pre_post_hook but _pre and _post
 *    args will be transform into data_t ptr.
 */
#define ic_register_proxy_hdr_p_pre_post_hook_p(h, _mod, _if, _rpc, ic, hdr, \
                                                _pre_cb, _post_cb,           \
                                                _pre_arg, _post_arg)         \
    ic_register_proxy_hdr_p_pre_post_hook(h, _mod, _if, _rpc, ic, hdr,       \
                                          _pre_cb, _post_cb,                 \
                                          { .ptr = _pre_arg },               \
                                          { .ptr = _post_arg })

/** \brief register a pointed proxy destination for the given rpc with header.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 */
#define ic_register_proxy_hdr_p(h, _mod, _if, _rpc, ic, hdr)                 \
    ic_register_proxy_hdr_p_pre_post_hook_p(h, _mod, _if, _rpc, ic, hdr,     \
                                            NULL, NULL, NULL, NULL)

/** \brief register a pointed proxy destination for the given rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 */
#define ic_register_proxy_p(h, _mod, _if, _rpc, icp) \
    ic_register_proxy_hdr_p(h, _mod, _if, _rpc, icp, NULL)

/** \brief register a dynamic proxy destination for the given rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  cb
 *   a callback that returns an #ic_dynproxy_t (a pair of an #ichannel_t and
 *   an #ic__hdr__t). When profixying the callback is called. When it returns
 *   a #NULL #ichannel_t the request is rejected with a #IC_MSG_PROXY_ERROR
 *   status, else it's forwarded to this #ichannel_t using the returned
 *   #ic__hdr__t.
 * \param[in]  _priv
 *   an opaque pointer passed to the callback each time it's called.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *,
 *                 const iop_struct_t * nullable, const void * nullable)</tt>
 *    see #ic_query_do_post_hook about how to use the last two arguments.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ic_register_dynproxy_pre_post_hook(h, _mod, _if, _rpc, cb, _priv,    \
                                           _pre_cb, _post_cb,                \
                                           _pre_arg, _post_arg)              \
    do {                                                                     \
        uint32_t cmd    = IOP_RPC_CMD(_mod, _if, _rpc);                      \
        ic_cb_entry_t e = {                                                  \
            .cb_type = IC_CB_DYNAMIC_PROXY,                                  \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .dynproxy = {                                             \
                .get_ic = cb,                                                \
                .priv   = _priv,                                             \
            } },                                                             \
        };                                                                   \
        qm_add(ic_cbs, h, cmd, e);                                           \
    } while (0)

/** \brief same as #ic_register_dynproxy_pre_post_hook but _pre and _post args
 *    will be transform into data_t ptr.
 */
#define ic_register_dynproxy_pre_post_hook_p(h, _mod, _if, _rpc, cb, _priv,  \
                                             _pre_cb, _post_cb,              \
                                             _pre_arg, _post_arg)            \
    ic_register_dynproxy_pre_post_hook(h, _mod, _if, _rpc, cb, _priv,        \
                                       _pre_cb, _post_cb,                    \
                                       { .ptr = _pre_arg },                  \
                                       { .ptr = _post_arg })

/** \brief register a dynamic proxy destination for the given rpc.
 * \param[in]  h
 *    the qm_t(ic_cbs) of implementation to register the rpc
 *    implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  cb
 *   a callback that returns an #ic_dynproxy_t (a pair of an #ichannel_t and
 *   an #ic__hdr__t). When profixying the callback is called. When it returns
 *   a #NULL #ichannel_t the request is rejected with a #IC_MSG_PROXY_ERROR
 *   status, else it's forwarded to this #ichannel_t using the returned
 *   #ic__hdr__t.
 * \param[in]  _priv
 *   an opaque pointer passed to the callback each time it's called.
 */
#define ic_register_dynproxy(h, _mod, _if, _rpc, cb, _priv)                  \
    ic_register_dynproxy_pre_post_hook_p(h, _mod, _if, _rpc, cb, _priv,      \
                                         NULL, NULL, NULL, NULL)

/*----- message handling -----*/

/** \brief internal do not use directly, or know what you're doing. */
void * nonnull __ic_get_buf(ic_msg_t * nonnull msg, int len);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_bpack(ic_msg_t * nonnull, const iop_struct_t * nonnull,
                 const void * nonnull);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_msg_build(ic_msg_t * nonnull, const iop_struct_t * nonnull,
                     const void * nonnull, bool);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_msg_build_from(ic_msg_t * nonnull, const ic_msg_t * nonnull);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_query_flags(ichannel_t * nonnull, ic_msg_t * nonnull, uint32_t flags);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_query(ichannel_t * nonnull, ic_msg_t * nonnull);
/** \brief internal do not use directly, or know what you're doing. */
void  __ic_query_sync(ichannel_t * nonnull, ic_msg_t * nonnull);

/** \brief internal do not use directly, or know what you're doing. */
size_t __ic_reply(ichannel_t * nullable, uint64_t slot, int cmd, int fd,
                  const iop_struct_t * nonnull, const void * nonnull);

/** \brief reply to a given rpc with an error.
 *
 * This function is meant to be used either:
 * - synchronously, in an rpc implementation callback (in which case \v ic and
 *   \v slot should be the \v ic and \v slot passed to the implementation.
 *   callback).
 * - asynchronously, in which case \v ic \em MUST be #NULL, and \v slot should
 *   be the slot originally passed to the implementation callback.
 *
 * \param[in] ic
 *   the #ichannel_t to send the reply to, should be #NULL if the reply is
 *   asynchronous.
 * \param[in] slot  the slot of the query we're answering to.
 * \param[in] err
 *   the error status to use, should NOT be #IC_MSG_OK nor #IC_MSG_EXN.
 */
void ic_reply_err(ichannel_t * nullable ic, uint64_t slot, int err);

/** \brief helper to set ctx and execute the pre hook of the query.
 *
 * \param[in]     ic   the #ichannel_t to send the query to.
 * \param[in]     slot the slot of the received query.
 * \param[in]     e    the #ic_cb_entry_t of the rpc called.
 * \param[in,out] hdr  the #ic__hdr__t of the query.
 *
 * return -1 if the pre_hook has replied to the query, 0 otherwise.
 */
int
ic_query_do_pre_hook(ichannel_t * nullable ic, uint64_t slot,
                     const ic_cb_entry_t * nonnull e,
                     ic__hdr__t * nullable hdr);

/** \brief helper to get and execute the post hook of the query.
 *
 * \param[in]  ic      the #ichannel_t to send the query to.
 * \param[in]  status  the received answer status parameter.
 * \param[in]  slot    the slot of the received query.
 * \param[in]  st      the type of the result value.
 * \param[in]  value   the result value of the query.
 *
 * \p st and \p value are only set if status is IC_MSG_OK or IC_MSG_EXN, the
 * query is not proxified and the query has been replied with #ic_reply or
 * #ic_throw.
 */
void ic_query_do_post_hook(ichannel_t * nullable ic, ic_status_t status,
                           uint64_t slot, const iop_struct_t * nullable st,
                           const void * nullable value);

#ifndef NDEBUG
bool __ic_rpc_is_traced(const iop_iface_t * nonnull iface,
                        const iop_rpc_t * nonnull rpc);

/** Check if the given RPC is traced.
 *
 * Traces are automatically enabled for RPCs listed in IC_TRACE. That
 * environment variable is a space-separate list of interface and RPC names.
 * The names are provided in the camel-case form (same os .iop files),
 * including the package name:
 *
 *  IC_TRACE="core.Log" will trace all logging RPCs
 *  IC_TRACE="core.Log.setRootLevel" will trace the setRootLevel RPCs
 *
 * Traces are emitted using the ic/tracing logger that is a silent logger, and
 * thus you must make sure you activated that logger in order to get the
 * traces. For example, using the IS_DEBUG environment variable, this would be
 *
 *  IS_DEBUG=+ic/tracing:0
 *
 * Packed and unpacked arguments/responses/headers are emitted at trace level
 * 1 only.
 *
 * A more complete example with mixed RPC and interface names:
 *
 *  IC_TRACE="qkv.Base qkv.Repl.push" IS_DEBUG=+ic/tracing:0 ./zchk-cluster
 *
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \return true if tracing is activated for that RPC.
 */
#define ic_rpc_is_traced(_mod, _if, _rpc)  ({                               \
        static int _mod##__##_if##__##_rpc##_traced = -1;                   \
                                                                            \
        if (unlikely(_mod##__##_if##__##_rpc##_traced < 0)) {               \
            _mod##__##_if##__##_rpc##_traced                                \
                = __ic_rpc_is_traced(&_mod##__##_if(if),                    \
                                     IOP_RPC(_mod, _if, _rpc));             \
        }                                                                   \
        _mod##__##_if##__##_rpc##_traced;                                   \
    })
#else
#define ic_rpc_is_traced(_mod, _if, _rpc)  (false)
#endif

/** \brief helper to prepare a typed query message.
 * \param[in]  _msg   the #ic_msg_t to prepare.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 */
#define __ic_prepare_msg(_msg, _cb, _mod, _if, _rpc) \
    ({                                                                      \
        ic_msg_t *__msgp = (_msg);                                          \
        void (*__cb)(IOP_RPC_CB_ARGS(_mod, _if, _rpc)) = _cb;               \
        __msgp->cb = __cb != NULL ? (ic_msg_cb_f *)__cb : &ic_drop_ans_cb;  \
        __msgp->rpc = IOP_RPC(_mod, _if, _rpc);                             \
        __msgp->async = __msgp->rpc->async;                                 \
        __msgp->cmd = IOP_RPC_CMD(_mod, _if, _rpc);                         \
        __msgp->trace = __msgp->trace || ic_rpc_is_traced(_mod, _if, _rpc); \
        __msgp;                                                             \
    })

/** \brief helper to build a typed query message.
 * \param[in]  _ich   the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_build_query_p(_ich, _msg, _cb, _mod, _if, _rpc, v) \
    ({                                                                      \
        const IOP_RPC_T(_mod, _if, _rpc, args) *__v = (v);                  \
        ic_msg_t *__msg = (_msg);                                           \
        const ichannel_t *__ich = (_ich);                                   \
        __ic_prepare_msg(__msg, (_cb), _mod, _if, _rpc);                    \
        __ic_msg_build(__msg, IOP_RPC(_mod, _if, _rpc)->args, __v,          \
                       !ic_is_local(__ich) || __msg->force_pack);           \
        __msg;                                                              \
    })

/** \brief helper to build a typed query message by duplicating another.
 * \param[in]  _msg      the #ic_msg_t to fill.
 * \param[in]  _msg_src  the #ic_msg_t to duplicate.
 */
#define ic_build_query_from(_msg, _msg_src) \
    ({                                                                      \
        ic_msg_t *__msg = (_msg), *__msg_src = (_msg_src);                  \
        __msg->cb         = __msg_src->cb;                                  \
        __msg->rpc        = __msg_src->rpc;                                 \
        __msg->async      = __msg_src->async;                               \
        __msg->cmd        = __msg_src->cmd;                                 \
        __msg->trace      = __msg_src->trace;                               \
        __msg->force_pack = true;                                           \
        __ic_msg_build_from(__msg, __msg_src);                              \
        __msg;                                                              \
    })

/** \brief helper to build a typed query message.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_build_query(_ic, _msg, _cb, _mod, _if, _rpc, ...) \
    ic_build_query_p(_ic, _msg, _cb, _mod, _if, _rpc,                       \
                     (&(IOP_RPC_T(_mod, _if, _rpc, args)){ __VA_ARGS__ }))

/** \brief helper to send a query to a given ic.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_query(_ic, _msg, _cb, _mod, _if, _rpc, ...) \
    ({  ichannel_t *_ich = (_ic);                                         \
        __ic_query(_ich, ic_build_query(_ich, _msg, _cb, _mod, _if, _rpc, \
                                        __VA_ARGS__));                    \
    })

/** \brief helper to send a query to a given ic.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query_p(_ic, _msg, _cb, _mod, _if, _rpc, v) \
    ({  ichannel_t *_ich = (_ic);                                           \
        __ic_query(_ich, ic_build_query_p(_ich, _msg, _cb, _mod, _if, _rpc, \
                                          v));                              \
    })

/** \brief helper to send a query to a given ic, computes callback name.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_query2(_ic, _msg, _mod, _if, _rpc, ...) \
    ({  ichannel_t *_ich = (_ic);                                            \
        __ic_query(_ich, ic_build_query(_ich, _msg,                          \
            IOP_RPC_CB_REF(_mod, _if, _rpc), _mod, _if, _rpc, __VA_ARGS__)); \
    })

/** \brief helper to send a query to a given ic, computes callback name.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query2_p(_ic, _msg, _mod, _if, _rpc, v) \
    ({  ichannel_t *_ich = (_ic);                                  \
        __ic_query(_ich, ic_build_query_p(_ich, _msg,              \
            IOP_RPC_CB_REF(_mod, _if, _rpc), _mod, _if, _rpc, v)); \
    })

/** \brief helper to send a query to a given ic.
 *
 * Same as #ic_query but waits for the query to be sent before the call
 * returns. DO NOT USE unless you have a really good reason.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_query_sync(_ic, _msg, _cb, _mod, _if, _rpc, ...) \
    ({  ichannel_t *_ich = (_ic);                                         \
        __ic_query_sync(_ich, ic_build_query(_ich, _msg, _cb, _mod, _if,  \
                                              _rpc, __VA_ARGS__));        \
    })

/** \brief helper to send a query to a given ic.
 *
 * Same as #ic_query_p but waits for the query to be sent before the call
 * returns. DO NOT USE unless you have a really good reason.
 *
 * \param[in]  _ic    the #ichannel_t to send the query to.
 * \param[in]  _msg   the #ic_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query_sync_p(_ic, _msg, _cb, _mod, _if, _rpc, v) \
    ({  ichannel_t *_ich = (_ic);                                          \
        __ic_query_sync(_ich, ic_build_query_p(_ich, _msg, _cb, _mod, _if, \
                                               _rpc, v));                  \
    })

/** \brief helper to proxy a query to a given ic with header.
 *
 * It setups the message automatically so that when the reply is received it's
 * proxied back to the caller without any "human" intervention.
 *
 * \param[in]  ic     the #ichannel_t to proxy the query to.
 * \param[in]  slot   the slot of the received query.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  hdr    the #ic__hdr__t to use.
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query_proxy_hdr(ic, slot, _mod, _if, _rpc, hdr, v) \
    ic_query_p(ic, ic_msg_proxy_new(-1, slot, hdr),                         \
               (void *)IC_PROXY_MAGIC_CB, _mod, _if, _rpc, v);

/** \brief helper to proxy a query to a given ic.
 *
 * It setups the message automatically so that when the reply is received it's
 * proxied back to the caller without any "human" intervention.
 *
 * \param[in]  ic     the #ichannel_t to proxy the query to.
 * \param[in]  slot   the slot of the received query.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query_proxy(ic, slot, _mod, _if, _rpc, v) \
    ic_query_proxy_hdr(ic, slot, _mod, _if, _rpc, NULL, v)

/** \brief helper to proxy a query to a given ic with an fd.
 *
 * It setups the message automatically so that when the reply is received it's
 * proxied back to the caller without any "human" intervention.
 *
 * \param[in]  ic     the #ichannel_t to proxy the query to.
 * \param[in]  fd     the fd to send.
 * \param[in]  slot   the slot of the received query.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_args__t *</tt> value.
 */
#define ic_query_proxy_fd(ic, fd, slot, _mod, _if, _rpc, v) \
    ic_query_p(ic, ic_msg_proxy_new(fd, slot, hdr),         \
               (void *)IC_PROXY_MAGIC_CB, _mod, _if, _rpc, v);

/** \brief helper to reply to a given query (server-side).
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_res__t *</tt> value.
 */
#define ic_reply_p(ic, slot, _mod, _if, _rpc, v) \
    ({  const IOP_RPC_T(_mod, _if, _rpc, res) *__v = (v);                   \
        STATIC_ASSERT(_mod##__##_if(_rpc##__rpc__async) == 0);              \
        __ic_reply(ic, slot, IC_MSG_OK, -1,                                 \
                   IOP_RPC(_mod, _if, _rpc)->result, __v); })
/** \brief helper to reply to a given query (server-side).
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_reply(ic, slot, _mod, _if, _rpc, ...) \
    ic_reply_p(ic, slot, _mod, _if, _rpc,                                   \
               (&(IOP_RPC_T(_mod, _if, _rpc, res)){ __VA_ARGS__ }))

/** \brief helper to reply to a given query (server-side), with fd.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  fd     the file descriptor to use in the reply.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_res__t *</tt> value.
 */
#define ic_reply_fd_p(ic, slot, fd, _mod, _if, _rpc, v) \
    ({  const IOP_RPC_T(_mod, _if, _rpc, res) *__v = (v);                   \
        STATIC_ASSERT(_mod##__##_if(_rpc##__rpc__async) == 0);              \
        __ic_reply(ic, slot, IC_MSG_OK, fd,                                 \
                   IOP_RPC(_mod, _if, _rpc)->result, __v); })
/** \brief helper to reply to a given query (server-side), with fd.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  fd     the file descriptor to use in the reply.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_reply_fd(ic, slot, fd, _mod, _if, _rpc, ...) \
    ic_reply_fd_p(ic, slot, fd, _mod, _if, _rpc,                            \
                  (&(IOP_RPC_T(_mod, _if, _rpc, res)){ __VA_ARGS__ }))

/** \brief helper to reply to a given query (server-side) with an exception.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  v      a <tt>${_mod}__${_if}__${_rpc}_exn__t *</tt> value.
 */
#define ic_throw_p(ic, slot, _mod, _if, _rpc, v) \
    ({  const IOP_RPC_T(_mod, _if, _rpc, exn) *__v = (v);                   \
        STATIC_ASSERT(_mod##__##_if(_rpc##__rpc__async) == 0);              \
        __ic_reply(ic, slot, IC_MSG_EXN, -1,                                \
                   IOP_RPC(_mod, _if, _rpc)->exn, __v); })

/** \brief helper to reply to a given query (server-side) with an exception.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_throw(ic, slot, _mod, _if, _rpc, ...) \
    ic_throw_p(ic, slot, _mod, _if, _rpc,                                   \
               (&(IOP_RPC_T(_mod, _if, _rpc, exn)){ __VA_ARGS__ }))

/** \brief helper to reply to a query (server-side) with a forced exception.
 *   NB: This macro is means to be used only inside a pre_hook or
 *   implementation with a hook_ctx define.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the pre_hook/implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  ctx    the context of the query we're answering to.
 * \param[in]  _exn   the type of the exception to throw
 * \param[in]  v      a <tt>_exn *</tt> value.
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_throw_exn_p(ic, slot, ctx, _exn, v)                              \
    ({  const _exn##__t *__v = (v);                                         \
        assert(ctx && ctx->rpc && ctx->rpc->exn == &_exn##__s);             \
        __ic_reply(ic, slot, IC_MSG_EXN, -1,                                \
                   ctx->rpc->exn, __v); })

/** \brief helper to reply to a query (server-side) with a forced exception.
 *   NB: This macro is means to be used only inside a pre_hook or
 *   implementation with a hook_ctx define.
 *
 * \param[in]  ic
 *   the #ichannel_t to send the reply to, must be #NULL if the reply isn't
 *   done in the pre_hook/implementation callback synchronously.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  _exn   the type of the exception to throw
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define ic_throw_exn(ic, slot, ctx, _exn, ...)                               \
    ic_throw_exn_p(ic, slot, ctx, _exn, (&(_exn##__t){ __VA_ARGS__ }))

/** \brief Bounce an IOP answer to reply to another slot.
 *
 * This function may be used to forward an answer to another slot when
 * implementing a manual proxy. It saves the reply data packing.
 *
 * WARNINGS:
 *  - this function MUST be used before leaving the RPC callback scope.
 *  - you cannot use this function to forward a webservices answer; this is
 *    not implemented.
 *  - be really careful because this function suppose that the answer has
 *    been left unmodified since its reception. If you want to modify it
 *    before the forwarding, then *don't* use this function and use instead
 *    ic_reply_p/ic_throw_p. If you try to do this, in the best scenario
 *    your changes will be ignored, in the worst you will have a crash…
 *
 * Here an example of how to use this function:
 * <code>
 *  RPC_IMPL(pkg, foo, bar)
 *  {
 *      CHECK_ID_OK(arg->id);
 *
 *      // manual proxy
 *      ic_msg_t *imsg = ic_msg_new(sizeof(uint64_t));
 *      *(uint64_t *)imsg->priv = slot;
 *      ic_query_p(remote_ic, imsg, pkg, foo, bar, arg);
 *  }
 *
 *  RPC_CB(pkg, foo, bar)
 *  {
 *      uint64_t origin_slot = *(uint64_t *)msg->priv;
 *
 *      // automatic and efficient answer forwarding
 *      __ic_forward_reply_to(ic, origin_slot, status, res, exn);
 *  }
 * </code>
 *
 * \param[in]  ic     the ichannel_t the "thing" we proxy comes from.
 * \param[in]  slot   the slot of the query we're answering to.
 * \param[in]  cmd    the received answer status parameter.
 * \param[in]  res    the received answer result parameter.
 * \param[in]  exn    the received answer exception parameter.
 */
void __ic_forward_reply_to(ichannel_t * nonnull ic, uint64_t slot,
                           int cmd, const void * nullable res,
                           const void * nullable exn);


/** \brief Manually reply to a message with an error code.
 *
 * This function may be used in case you are manually dealing with ic_msg_t in
 * order to implement some routing layer.
 *
 * \param[in]  ic     the ichannel_t the "thing" the message come from (NULL
 *                    if unknown)
 * \param[in]  msg    the message that get replied.
 * \param[in]  status the reply status.
 */
void __ic_msg_reply_err(ichannel_t * nullable ic, ic_msg_t * nonnull msg,
                        ic_status_t status);

/* Compatibility aliases */
#define ic_reply_throw_p(...)  ic_throw_p(__VA_ARGS__)
#define ic_reply_throw(...)    ic_throw(__VA_ARGS__)

/** \brief Get dealias field of the ic header.
 *
 * This function returns the dealias field of the ic header, or unset if this
 * field is undefined.
 */
opt_bool_t ic_hdr_get_dealias(const ic__hdr__t * nullable hdr);

#endif
