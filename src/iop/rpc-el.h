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

/* IOP Channels with internal event loop.
 *
 * When using languages or systems that uses an internal event loop, it is
 * required, in order to use IOP Channels, to start an Intersec event loop in
 * a thread.
 *
 * This can be tricky to do it right. This header and its source file provides
 * high-level functions to do it.
 *
 * ic_el_module_init() must be called before using any other functions.
 * ic_el_module_stop() and ic_el_module_cleanup() must be called at the end to
 * stop the internal thread and resources.
 *
 * Internally, the functions are protected of by an internal lock.
 *
 * WARNING: Unless specified, all functions *MUST* be called with external
 * locks released to avoid dead-lock.
 * For example, in Python, the GIL must be released before calling the
 * functions.
 *
 * The callbacks are called with the internal lock released.
 * WARNING: The callbacks are called in the internal thread, and not in the
 * thread of the caller.
 */

#ifndef IS_LIB_COMMON_IOP_RPC_EL_H
#define IS_LIB_COMMON_IOP_RPC_EL_H

#include <lib-common/core.h>
#include <lib-common/iop-rpc.h>

/** Result of synchronous functions. */
typedef enum ic_el_sync_res_t {
    IC_EL_SYNC_OK = 0,
    IC_EL_SYNC_ERR = -1,
    IC_EL_SYNC_SIGINT = -2,
} ic_el_sync_res_t;

/* {{{ Server */

/** IC EL server representation. */
typedef struct ic_el_server_t ic_el_server_t;

/** Configuration of the callbacks of an IC EL server. */
typedef struct ic_el_server_cb_cfg_t {
    /** Callback called when a request is made to an RPC.
     *
     * This callback must be implemented.
     *
     * \param[in]  server The IC EL server.
     * \param[in]  ic     The ichannel of the request.
     * \param[in]  slot   The slot of the request.
     * \param[in]  arg    The RPC argument of the request.
     * \param[out] res    The RPC result of the reply.
     * \param[out] res_st The IOP type description of the reply.
     * \return  The status of the reply. If the status is not IC_MSG_OK or
     *          IC_MSG_EXN, \p res and \p res_desc are ignored.
     */
    ic_status_t (*t_on_rpc)(ic_el_server_t *server, ichannel_t *ic,
                            uint64_t slot, void *arg, const ic__hdr__t *hdr,
                            void **res, const iop_struct_t **res_st);

    /** Callback called when a peer is connecting to the server.
     *
     *  This callback is optional.
     *
     * \param[in] server      The IC EL server.
     * \param[in] server_uri  The URI the IC EL server is listening to.
     * \param[in] remote_addr The address of the peer.
     */
    void (*nullable on_connect)(ic_el_server_t *server, lstr_t server_uri,
                                lstr_t remote_addr);

    /** Callback called when a peer is disconnecting from the server.
     *
     *  This callback is optional.
     *
     * \param[in] server      The IC EL server.
     * \param[in] server_uri  The URI the IC EL server is listening to.
     * \param[in] remote_addr The address of the peer.
     */
    void (*nullable on_disconnect)(ic_el_server_t *server,
                                   lstr_t server_uri, lstr_t remote_addr);
} ic_el_server_cb_cfg_t;

/** Create an IC EL Server.
 *
 * \param[in] cb_cfg The configuration of the callbacks of the IC server.
 */
ic_el_server_t *ic_el_server_create(const ic_el_server_cb_cfg_t *cb_cfg);

/** Destroy an IC EL server.
 *
 * Stop the IC EL server and delete it.
 *
 * \param[in,out] server_ptr The pointer to the server to destroy. Will be set
 *                           to NULL afterwards.
 */
void ic_el_server_destroy(ic_el_server_t **server_ptr);

/** Set the IC EL server external object.
 *
 * \warning: The external object is not protected by the internal lock and is
 * not thread-safe. You must protect the external object with an external
 * lock.
 */
void ic_el_server_set_ext_obj(ic_el_server_t *server,
                              void * nullable ext_obj);

/** Get the IC EL server external object.
 *
 * \warning: The external object is not protected by the internal lock and is
 * not thread-safe. You must protect the external object with an external
 * lock.
 */
void * nullable ic_el_server_get_ext_obj(ic_el_server_t *server);

/** Make the IC EL server start listening.
 *
 * \param[in]  server The IC EL server.
 * \param[in]  uri    The uri the IC server should listen to.
 * \param[out] err    The error description in case of error.
 * \return     -1 in case of error, 0 otherwise.
 */
int ic_el_server_listen(ic_el_server_t *server, lstr_t uri, sb_t *err);

/** Make the IC EL server start listening until timeout elapsed or the server
 * has been stopped.
 *
 * \param[in]  server  The IC EL server.
 * \param[in]  uri     The uri the IC server should listen to.
 * \param[in]  timeout Number of seconds to listen.
 * \param[out] err     The error description in case of error.
 * \return IC_EL_SYNC_OK if the server has been stopped manually or the
 *         timeout elapsed.
 *         IC_EL_SYNC_ERR if an error occured, \p err contains the description
 *         of the error.
 *         IC_EL_SYNC_SIGINT if a sigint occurred.
 */
ic_el_sync_res_t
ic_el_server_listen_block(ic_el_server_t *server, lstr_t uri, double timeout,
                          sb_t *err);

/** Stop the IC EL server.
 *
 * Do nothing if the server is not listening.
 *
 * \param[in] server The IC EL server to stop.
 *
 * \return IC_EL_SYNC_OK if the server has been successfully stopped.
 *         IC_EL_SYNC_SIGINT if a sigint occurred during the stop.
 *         This function cannot return IC_EL_SYNC_ERR.
 */
ic_el_sync_res_t ic_el_server_stop(ic_el_server_t *server);

/** Register an RPC to the IC EL server.
 *
 * \param[in] server The IC EL server.
 * \param[in] rpc    The RPC to register.
 * \param[in] cmd    The command index of the RPC.
 */
void ic_el_server_register_rpc(ic_el_server_t *server,
                               const iop_rpc_t *rpc, uint32_t cmd);

/** Unregister an RPC from the IC EL server.
 *
 * \param[in] server The IC EL server.
 * \param[in] cmd    The command index of the RPC.
 */
void ic_el_server_unregister_rpc(ic_el_server_t *server, uint32_t cmd);

/** Is the IC EL server listening.
 *
 * \param[in] server The IC EL server.
 * \return true if the IC EL server is listening, false otherwise.
 */
bool ic_el_server_is_listening(const ic_el_server_t *server);

/* }}} */
/* {{{ Client */

/** IC EL client representation. */
typedef struct ic_el_client_t ic_el_client_t;

/** Configuration of the callbacks of an IC EL client. */
typedef struct ic_el_client_cb_cfg_t {
    /** Callback called when the client has been connected.
     *
     * This callback is optional.
     *
     * \param[in] client The IC EL client.
     */
    void (*nullable on_connect)(ic_el_client_t *client);

    /** Callback called when the client has been disconnected.
     *
     * This callback is optional.
     *
     * \param[in] client    The IC EL client.
     * \param[in] connected True if the client has been connected before,
     *                      false otherwise.
     */
    void (*nullable on_disconnect)(ic_el_client_t *client, bool connected);
} ic_el_client_cb_cfg_t;

/** Create an IC EL client.
 *
 * \param[in]  uri            The uri the IC client should connect to.
 * \param[in]  no_act_timeout The inactivity timeout before closing the
 *                            connection in seconds.
 * \param[in]  cb_cfg         The configuration of the callbacks of the IC
 *                            client.
 *                            0 or a negative number means forever.
 * \param[out] err            The error description in case of error.
 * \return The new IC EL client.
 */
ic_el_client_t *ic_el_client_create(lstr_t uri, double no_act_timeout,
                                    const ic_el_client_cb_cfg_t *cb_cfg,
                                    sb_t *err);

/** Destroy the IC EL client.
 *
 * \param[in,out] client_ptr The pointer to the client to destroy. Will be set
 *                           to NULL afterwards.
 */
void ic_el_client_destroy(ic_el_client_t **client_ptr);

/** Set the IC EL client external object.
 *
 * \warning: The external object is not protected by the internal lock and is
 * not thread-safe. You must protect the external object with an external
 * lock.
 */
void ic_el_client_set_ext_obj(ic_el_client_t *client,
                              void * nullable ext_obj);

/** Get the IC EL client external object.
 *
 * \warning: The external object is not protected by the internal lock and is
 * not thread-safe. You must protect the external object with an external
 * lock.
 */
void * nullable ic_el_client_get_ext_obj(ic_el_client_t *client);

/** Synchronously connect the IC EL client.
 *
 * \param[in]  client  The IC EL client.
 * \param[in]  timeout The timeout it should wait for the connection in
 *                     seconds. -1 means forever.
 * \param[out] err     The error description in case of error.
 * \return IC_EL_SYNC_OK if the client has been successfully connected.
 *         IC_EL_SYNC_ERR if an error occured, \p err contains the description
 *         of the error.
 *         IC_EL_SYNC_SIGINT if a sigint occurred during the connection.
 */
ic_el_sync_res_t
ic_el_client_sync_connect(ic_el_client_t *client, double timeout, sb_t *err);

/** The callback used when asynchronously connect the IC EL client.
 *
 * \warning: This callback can be called either in the thread of the caller or
 * in the event loop thread.
 *
 * \param[in] err    The error description in case of error.
 *                   If NULL, the client is successfully connected.
 *                   If not NULL, the client cannot be connected.
 * \param[in] cb_arg The custom user variable passed to
 *                   \ref ic_el_client_async_connect.
 */
typedef void (*ic_client_async_connect_f)(const sb_t *nullable err,
                                          void *nullable cb_arg);

/** Aynchronously connect the IC EL client.
 *
 * \param[in]  client  The IC EL client.
 * \param[in]  timeout The timeout it should wait for the connection in
 *                     seconds. -1 means forever.
 * \param[in]  cb      The callback to be called on result.
 * \param[in]  cb_arg  A custom user variable to be passed to the callback.
 */
void ic_el_client_async_connect(ic_el_client_t *client, double timeout,
                                ic_client_async_connect_f cb,
                                void *nullable cb_arg);

/** Disconnect the IC EL client.
 *
 * \param[in] client The IC EL client.
 */
void ic_el_client_disconnect(ic_el_client_t *client);

/** Returns whether the IC EL client is connected or not.
 *
 * \param[in] client The IC EL client.
 * \return true if the associated IC channel is connected, false otherwise
 */
bool ic_el_client_is_connected(ic_el_client_t *client);

/** Synchronously call an RPC with the IC EL client.
 *
 * \param[in]  client  The IC EL client.
 * \param[in]  rpc     The RPC to call.
 * \param[in]  cmd     The command index of the RPC.
 * \param[in]  hdr     The ic header.
 * \param[in]  timeout The timeout of the request in seconds. -1 means
 *                     forever.
 * \param[in]  arg     The argument value of the RPC.
 * \param[out] status  The query result status.
 * \param[out] res     The query result value. Only set when \p status is set
 *                     to IC_MSG_OK or IC_MSG_EXN, and when the RPC is not
 *                     asynchronous, i.e. with `out null`.
 *                     This value is allocated on the heap and *MUST* be freed
 *                     with p_delete().
 * \param[out] err     The error description in case of error.
 * \return IC_EL_SYNC_OK if the query has been run and returned. You must check
 *         if the query has been successful with \p status.
 *         IC_EL_SYNC_ERR if an error occured, \p err contains the description
 *         of the error.
 *         IC_EL_SYNC_SIGINT if a sigint occurred during the query.
 */
ic_el_sync_res_t
ic_el_client_sync_call(ic_el_client_t *client, const iop_rpc_t *rpc,
                       int32_t cmd, const ic__hdr__t *hdr, double timeout,
                       const void *arg, ic_status_t *status, void **res,
                       sb_t *err);

/** The callback used when asynchronously call an RPC with the IC EL client.
 *
 * \warning: This callback can be called either in the thread of the caller or
 * in the event loop thread.
 *
 * \param[in] err    The error description in case of connection error or
 *                   timeout.
 *                   If NULL, the query was successfully started, and
 *                   \p status is set and representative.
 *                   If not NULL, the query is not successful, and \p status
 *                   is not set and not representative.
 * \param[in] status The query result status.
 * \param[in] res    The query result value. Only set when \p status is set
 *                   to IC_MSG_OK or IC_MSG_EXN, and when the RPC is not
 *                   asynchronous, i.e. with `out null`.
 * \param[in] cb_arg The custom user variable passed to
 *                   \ref ic_el_client_async_call.
 */
typedef void (*ic_client_async_call_f)(const sb_t *nullable err,
                                       ic_status_t status,
                                       const void *nullable res,
                                       void *nullable cb_arg);

/** Asynchronously call an RPC with the IC EL client.
 *
 * \param[in]  client  The IC EL client.
 * \param[in]  rpc     The RPC to call.
 * \param[in]  cmd     The command index of the RPC.
 * \param[in]  hdr     The ic header.
 * \param[in]  timeout The timeout of the request in seconds. -1 means
 *                     forever.
 * \param[in]  arg     The argument value of the RPC.
 * \param[in]  cb      The callback to be called on result.
 * \param[in]  cb_arg  A custom user variable to be passed to the callback.
 */
void ic_el_client_async_call(ic_el_client_t *client, const iop_rpc_t *rpc,
                             int32_t cmd, const ic__hdr__t *hdr,
                             double timeout, const void *arg,
                             ic_client_async_call_f cb,
                             void *nullable cb_arg);

/* }}} */
/* {{{ Module init */

/** Initialize the IC EL C module. */
void ic_el_module_init(void);

/** Stop the IC EL C module.
 *
 * When this function is called, the different callbacks can still be called
 * through the internal event loop.
 */
void ic_el_module_stop(void);

/** Clean up the IC EL C module.
 *
 * ic_el_module_stop() must have been called before calling this function.
 *
 * When this function is called, the internal event loop has already been
 * stopped, and no callbacks will be called.
 */
void ic_el_module_cleanup(void);

/* }}} */

#endif /* IS_LIB_COMMON_IOP_RPC_EL_H */
