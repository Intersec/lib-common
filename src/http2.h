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

#ifndef IS_LIB_INET_HTTP2_COMMON_H
#define IS_LIB_INET_HTTP2_COMMON_H

#include <src/core.h>

/* {{{ HTTP2 Header */

typedef enum http2_header_info_flags_t {
    HTTP2_HDR_FLAG_HAS_SCHEME               =  1 << 0,
    HTTP2_HDR_FLAG_HAS_METHOD               =  1 << 1,
    HTTP2_HDR_FLAG_HAS_PATH                 =  1 << 2,
    HTTP2_HDR_FLAG_HAS_AUTHORITY            =  1 << 3,
    HTTP2_HDR_FLAG_HAS_STATUS               =  1 << 4,
    /* EXTRA: either unknown or duplicated or after a regular hdr */
    HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR     =  1 << 5,
    HTTP2_HDR_FLAG_HAS_REGULAR_HEADERS      =  1 << 6,
    HTTP2_HDR_FLAG_HAS_CONTENT_LENGTH       =  1 << 7,
    HTTP2_HDR_FLAG_HAS_HOST                 =  1 << 8,
} http2_header_info_flags_t;

typedef struct http2_header_info_t {
    http2_header_info_flags_t flags;
    lstr_t scheme;
    lstr_t method;
    lstr_t path;
    lstr_t authority;
    lstr_t status;
    lstr_t content_length;
    lstr_t host;
} http2_header_info_t;

/* }}} */
/* {{{ Primary Types */

/* Standard Stream State-Changer Events (cf. RFC9113 §5.1) */
enum {
    /* Standard Events */
    HTTP2_STREAM_EV_1ST_HDRS = 1 << 0,
    HTTP2_STREAM_EV_EOS_RECV = 1 << 1,
    HTTP2_STREAM_EV_EOS_SENT = 1 << 2,
    HTTP2_STREAM_EV_RST_RECV = 1 << 3,
    HTTP2_STREAM_EV_RST_SENT = 1 << 4,
    HTTP2_STREAM_EV_PSH_RECV = 1 << 5,
    HTTP2_STREAM_EV_PSH_SENT = 1 << 6,
    /* Extension */
    HTTP2_STREAM_EV_CLOSED = 1 << 7,
    /* Standard Combination(s) */
    HTTP2_STREAM_EV_1ST_HDRS_EOS_RECV =
        HTTP2_STREAM_EV_1ST_HDRS | HTTP2_STREAM_EV_EOS_RECV,
    HTTP2_STREAM_EV_1ST_HDRS_EOS_SENT =
        HTTP2_STREAM_EV_1ST_HDRS | HTTP2_STREAM_EV_EOS_SENT,
    /* Masks */
    HTTP2_STREAM_EV_MASK_PEER_CANT_WRITE = HTTP2_STREAM_EV_EOS_RECV
                                          | HTTP2_STREAM_EV_RST_RECV
                                          | HTTP2_STREAM_EV_CLOSED,
};

/** info parsed from the frame hdr */
typedef struct http2_frame_info_t {
    uint32_t    len;
    uint32_t    stream_id;
    uint8_t     type;
    uint8_t     flags;
} http2_frame_info_t;

/* }}} */
/* {{{ HTTP2 Constants */

#define HTTP2_STREAM_ID_MASK    0x7fffffff

static const lstr_t http2_client_preface_g =
    LSTR_IMMED("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");

/* standard setting identifier values */
typedef enum setting_id_t {
    HTTP2_ID_HEADER_TABLE_SIZE      = 0x01,
    HTTP2_ID_ENABLE_PUSH            = 0x02,
    HTTP2_ID_MAX_CONCURRENT_STREAMS = 0x03,
    HTTP2_ID_INITIAL_WINDOW_SIZE    = 0x04,
    HTTP2_ID_MAX_FRAME_SIZE         = 0x05,
    HTTP2_ID_MAX_HEADER_LIST_SIZE   = 0x06,
} setting_id_t;

/* special values for stream id field */
enum {
    HTTP2_ID_NO_STREAM              = 0,
    HTTP2_ID_MAX_STREAM             = HTTP2_STREAM_ID_MASK,
};

/* length & size constants */
enum {
    HTTP2_LEN_FRAME_HDR             = 9,
    HTTP2_LEN_NO_PAYLOAD            = 0,
    HTTP2_LEN_PRIORITY_PAYLOAD      = 5,
    HTTP2_LEN_RST_STREAM_PAYLOAD    = 4,
    HTTP2_LEN_SETTINGS_ITEM         = 6,
    HTTP2_LEN_PING_PAYLOAD          = 8,
    HTTP2_LEN_GOAWAY_PAYLOAD_MIN    = 8,
    HTTP2_LEN_WINDOW_UPDATE_PAYLOAD = 4,
    HTTP2_LEN_CONN_WINDOW_SIZE_INIT = (1 << 16) - 1,
    HTTP2_LEN_WINDOW_SIZE_INIT      = (1 << 16) - 1,
    HTTP2_LEN_HDR_TABLE_SIZE_INIT   = 4096,
    HTTP2_LEN_MAX_FRAME_SIZE_INIT   = 1 << 14,
    HTTP2_LEN_MAX_FRAME_SIZE        = (1 << 24) - 1,
    HTTP2_LEN_MAX_SETTINGS_ITEMS    = HTTP2_ID_MAX_HEADER_LIST_SIZE,
    HTTP2_LEN_WINDOW_SIZE_LIMIT     = 0x7fffffff,
    HTTP2_LEN_MAX_WINDOW_UPDATE_INCR= 0x7fffffff,
};

/* standard frame type values */
typedef enum {
    HTTP2_TYPE_DATA                 = 0x00,
    HTTP2_TYPE_HEADERS              = 0x01,
    HTTP2_TYPE_PRIORITY             = 0x02,
    HTTP2_TYPE_RST_STREAM           = 0x03,
    HTTP2_TYPE_SETTINGS             = 0x04,
    HTTP2_TYPE_PUSH_PROMISE         = 0x05,
    HTTP2_TYPE_PING                 = 0x06,
    HTTP2_TYPE_GOAWAY               = 0x07,
    HTTP2_TYPE_WINDOW_UPDATE        = 0x08,
    HTTP2_TYPE_CONTINUATION         = 0x09,
} frame_type_t;

/* standard frame flag values */
enum {
    HTTP2_FLAG_NONE                 = 0x00,
    HTTP2_FLAG_ACK                  = 0x01,
    HTTP2_FLAG_END_STREAM           = 0x01,
    HTTP2_FLAG_END_HEADERS          = 0x04,
    HTTP2_FLAG_PADDED               = 0x08,
    HTTP2_FLAG_PRIORITY             = 0x20,
};

/* standard error codes */
typedef enum {
    HTTP2_CODE_NO_ERROR             = 0x0,
    HTTP2_CODE_PROTOCOL_ERROR       = 0x1,
    HTTP2_CODE_INTERNAL_ERROR       = 0x2,
    HTTP2_CODE_FLOW_CONTROL_ERROR   = 0x3,
    HTTP2_CODE_SETTINGS_TIMEOUT     = 0x4,
    HTTP2_CODE_STREAM_CLOSED        = 0x5,
    HTTP2_CODE_FRAME_SIZE_ERROR     = 0x6,
    HTTP2_CODE_REFUSED_STREAM       = 0x7,
    HTTP2_CODE_CANCEL               = 0x8,
    HTTP2_CODE_COMPRESSION_ERROR    = 0x9,
    HTTP2_CODE_CONNECT_ERROR        = 0xa,
    HTTP2_CODE_ENHANCE_YOUR_CALM    = 0xb,
    HTTP2_CODE_INADEQUATE_SECURITY  = 0xc,
    HTTP2_CODE_HTTP_1_1_REQUIRED    = 0xd,
} err_code_t;

/* }}} */
/* {{{ Decoding functions */

/** Parse a HTTP/2 frame info from the ps.
 *
 * \param[in]  ps     pstream_t that points to the HTTP/2 packet
 * \param[out] frame  frame info structure to fill
 */
__must_check__ int
http2_parse_frame_hdr(pstream_t *ps, http2_frame_info_t *frame);

/** Get the trimmed chunk of a HTTP/2 payload by removing the padding.
 *
 * \param[in]  payload      payload to trim
 * \param[in]  frame_flags  contains the PADDED flag
 * \param[out] chunk        trimmed chunk
 */
int http2_payload_get_trimmed_chunk(pstream_t payload, int frame_flags,
                                    pstream_t *chunk);

/* }}} */

#endif
