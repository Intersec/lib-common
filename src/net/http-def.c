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

#include <lib-common/http.h>
#include <lib-common/core/core.iop.h>

lstr_t const http_method_str[HTTP_METHOD__MAX] = {
#define V(v) [HTTP_METHOD_##v] = LSTR_IMMED(#v)
    V(OPTIONS),
    V(GET),
    V(HEAD),
    V(POST),
    V(PUT),
    V(DELETE),
    V(TRACE),
    V(CONNECT),
#undef V
};

/* rfc 2616: §6.1.1: Status Code and Reason Phrase */
lstr_t http_code_to_str(http_code_t code)
{
    STATIC_ASSERT(IOP_HTTP_METHOD_max == HTTP_METHOD__MAX - 1);

    switch (code) {
#define CASE(c, v)  case HTTP_CODE_##c: return LSTR(v)
        CASE(CONTINUE                , "Continue");
        CASE(SWITCHING_PROTOCOL      , "Switching Protocols");

        CASE(OK                      , "OK");
        CASE(CREATED                 , "Created");
        CASE(ACCEPTED                , "Accepted");
        CASE(NON_AUTHORITATIVE       , "Non-Authoritative Information");
        CASE(NO_CONTENT              , "No Content");
        CASE(RESET_CONTENT           , "Reset Content");
        CASE(PARTIAL_CONTENT         , "Partial Content");

        CASE(MULTIPLE_CHOICES        , "Multiple Choices");
        CASE(MOVED_PERMANENTLY       , "Moved Permanently");
        CASE(FOUND                   , "Found");
        CASE(SEE_OTHER               , "See Other");
        CASE(NOT_MODIFIED            , "Not Modified");
        CASE(USE_PROXY               , "Use Proxy");
        CASE(TEMPORARY_REDIRECT      , "Temporary Redirect");

        CASE(BAD_REQUEST             , "Bad Request");
        CASE(UNAUTHORIZED            , "Unauthorized");
        CASE(PAYMENT_REQUIRED        , "Payment Required");
        CASE(FORBIDDEN               , "Forbidden");
        CASE(NOT_FOUND               , "Not Found");
        CASE(METHOD_NOT_ALLOWED      , "Method Not Allowed");
        CASE(NOT_ACCEPTABLE          , "Not Acceptable");

        CASE(PROXY_AUTH_REQUIRED     , "Proxy Authentication Required");
        CASE(REQUEST_TIMEOUT         , "Request Time-out");
        CASE(CONFLICT                , "Conflict");
        CASE(GONE                    , "Gone");
        CASE(LENGTH_REQUIRED         , "Length Required");
        CASE(PRECONDITION_FAILED     , "Precondition Failed");
        CASE(REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large");
        CASE(REQUEST_URI_TOO_LARGE   , "Request-URI Too Large");
        CASE(UNSUPPORTED_MEDIA_TYPE  , "Unsupported Media Type");
        CASE(REQUEST_RANGE_UNSAT     , "Requested range not satisfiable");
        CASE(EXPECTATION_FAILED      , "Expectation Failed");
        CASE(TOO_MANY_REQUESTS       , "Too many requests");

        CASE(INTERNAL_SERVER_ERROR   , "Internal Server Error");
        CASE(NOT_IMPLEMENTED         , "Not Implemented");
        CASE(BAD_GATEWAY             , "Bad Gateway");
        CASE(SERVICE_UNAVAILABLE     , "Service Unavailable");
        CASE(GATEWAY_TIMEOUT         , "Gateway Time-out");
        CASE(VERSION_NOT_SUPPORTED   , "HTTP Version not supported");
#undef CASE
    }
    return LSTR("<unknown>");
}
