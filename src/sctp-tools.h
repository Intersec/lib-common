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

/**
 * This file contains functions that can be used to handle SCTP
 * connections. Those functions uses functions defined in
 * lib-common/src/net/sctp.* and provides callbacks when connecting,
 * disconnecting and receiving data
 */

#ifndef IS_SCTP_TOOLS_H
#define IS_SCTP_TOOLS_H

#include <lib-common/el.h>
#include <lib-common/iop/internals.h>
#include <lib-common/log.h>

typedef struct sctp_conn_t sctp_conn_t;

qvector_t(sctp_conn, sctp_conn_t *);

/**
 * \brief Callback when an outgoing connection succeed or failed.
 *
 * This callback is used on the client side only (on connections contexts
 * returned by \fn sctp_connect()).
 *
 * If the connection succeed and the callback returns an error (<0) then the
 * connection will be closed.
 */
typedef int (*sctp_on_connect_f)(sctp_conn_t * nonnull conn, bool success,
                                 int err);

/**
 * \brief Callback when the connection is closed, to clean context.
 *
 * This callback is used on the active connections. This includes contexts
 * returned by the function \fn sctp_connect() and contexts provided by the \p
 * sctp_on_accept_f callback. This excludes the listening contexts that are
 * returned by the \fn sctp_listen() function.
 *
 * The connection can be closed when an error is encountered so this might not
 * be trigerred by the \fn sctp_conn_close() function nor by the remote
 * endpoint disconnecting. In this case the connection will be cleaned and the
 * \fn sctp_conn_close() function does not need to be called.
 */
typedef void (*sctp_on_disconnect_f)(sctp_conn_t * nonnull conn);

/**
 * \brief Callback called when a message is received.
 *
 * This callback is used on the active connections. This includes contexts
 * returned by the function \fn sctp_connect() and contexts provided by the \p
 * sctp_on_accept_f callback. This excludes the listening contexts that are
 * returned by the \fn sctp_listen() function.
 *
 * This function will be called one extra time after the last message of an
 * SCTP packet with the flag \p no_more_msgs set to \p true to allow user
 * application to batch multiple messages received at the same time. This last
 * call will not have data.
 */
typedef int (*sctp_on_data_f)(sctp_conn_t * nonnull conn,
                              const sb_t * nullable data,
                              bool no_more_msgs);

/**
 * \brief Callback when a remote entity connects to the listening socket.
 *
 * This callback is used on listening contexts. This includes the contexts
 * returned by the \fn sctp_listen() function and this excludes contexts
 * returned by the \fn sctp_connect() function and the \p sctp_on_accept_f
 * callbacks.
 *
 * The provided connection's context must be cleaned eventually. If the remote
 * end disconnect first or upon errors, the connection will be cleaned by the
 * library and it's disconnection callback will be called. Otherwise the user
 * must call \fn sctp_conn_close().
 */
typedef int (*sctp_on_accept_f)(sctp_conn_t * nonnull conn);

typedef struct sctp_conn_t {
    /**
     * \brief Used to identify the remote in the logs.
     *
     * This can be the index of the entity in the configuration file, the IP
     * address, a human readable name, ...
     */
    lstr_t entity_id;

    /**
     * \brief User context.
     *
     * It will be accessible from the callback functions.
     */
    void *priv;

    /**
     * \brief Logger associated to the connection.
     */
    logger_t * nonnull logger;

    /**
     * \brief Host name associated to the connections.
     */
    lstr_t host;

    /**
     * \brief Port associated to the connections.
     */
    int port;
} sctp_conn_t;

/**
 * \brief Listen to incoming SCTP connections.
 *
 * This function will return a listening context.
 *
 * The returned context needs to be deallocated eventually.
 *
 * The purpose of the returrned context is to listen for new connections. It
 * doesn't allow the user to send messages and the disconnection callback will
 * not be called for this context.
 *
 * \param[in] addrs  addresses to listen to, multiple IPs means multi-homing
 * \param[in] port  the port to listen to
 * \param[in] sndbuf  maximum size of sending buffer (man 7 socket, SO_SNDBUF)
 * \param[in] entity_id  the name of the remote endpoint (see sctp_conn_t)
 * \param[in] priv  user context that will be accessible from callbacks
 * \param[in] on_accept_cb  function called when a new connection is received
 * \param[in] on_data_cb  callback that will be provided to new connections
 * \param[in] on_disconnect_cb  callback provided to new connections
 *
 * \retval NULL in case of failure
 * \retval a pointer to a sctp_conn_t
 */
sctp_conn_t * nullable
sctp_listen(const lstr__array_t * nonnull addrs,
            int port, size_t sndbuf, lstr_t entity_id, void * nullable priv,
            sctp_on_accept_f nonnull on_accept_cb,
            sctp_on_data_f nonnull on_data_cb,
            sctp_on_disconnect_f nullable on_disconnect_cb);

/**
 * \brief Connect to an SCTP endpoint.
 *
 * This function begins to establish a connection with the remote. The
 * connection is not established at the end of the function. When it will be
 * established, the \p on_connect_cb function will be called with its status
 * (successfully connected or failed to connect).
 *
 * The returned pointer will be necessary to send messages and/or close the
 * connection.
 *
 * \param[in] source_addrs  the IPs used as source, can be empty
 * \param[in] dest_addrs  the destination IPs, multiple IPs means multi-homing
 * \param[in] port  the port to connect to on the remote end
 * \param[in] sndbuf  maximum size of sending buffer (man 7 socket, SO_SNDBUF)
 * \param[in] entity_id  the name of the remote endpoint (see sctp_conn_t)
 * \param[in] priv  user context that will be accessible from callbacks
 * \param[in] on_connect_cb  function called on connection success/failure
 * \param[in] on_data_cb  function called when data is received
 * \param[in] on_disconnect_cb  function called when connection ends
 *
 * \retval NULL in case of failure
 * \retval a pointer to a sctp_conn_t
 */
sctp_conn_t * nullable
sctp_connect(qv_t(lstr) source_addrs,
             qv_t(lstr) dest_addrs,
             uint16_t port, size_t sndbuf, lstr_t entity_id,
             void * nullable priv,
             sctp_on_connect_f nonnull on_connect_cb,
             sctp_on_data_f nonnull on_data_cb,
             sctp_on_disconnect_f nullable on_disconnect_cb);

/**
 * \brief Enqueue a message to be sent.
 *
 * Note that sending is asynchronous. The message will be sent when the socket
 * allows it.
 *
 * \param[in] conn  the connection context
 * \param[in] payload  the message's content
 * \param[in] payload_protocol_id  defined in the protocol specification
 */
void sctp_send_msg(sctp_conn_t * nonnull conn, lstr_t payload,
                   uint32_t payload_protocol_id);

/**
 * \brief Close the connection.
 *
 * This is required when the connection is not automatically closed:
 *
 *   - If the remote end did not close the connection first;
 *
 *   - If no error were previously detected, in which case the connection
 *     would have closed itself;
 *
 * \param[in] conn  the connection context
 */
void sctp_conn_close(sctp_conn_t * nullable * nullable pconn);

#endif  /* IS_SCTP_TOOLS_H */
