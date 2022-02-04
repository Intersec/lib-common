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

#if !defined(IS_LIB_COMMON_NET_H) || defined(IS_LIB_COMMON_NET_ADDR_H)
#  error "you must include net.h instead"
#else
#define IS_LIB_COMMON_NET_ADDR_H

typedef struct addr_filter_t {
    union {
      struct {
        in_addr_t addr; /* 0 = 'any' */
        in_addr_t mask;
      } v4;
      struct {
        struct in6_addr addr;
        struct in6_addr mask;
      } v6;
    } u;
    uint16_t  port; /* 0 = 'any' */
    sa_family_t family;
} addr_filter_t;

typedef union sockunion_t {
    struct sockaddr_storage ss;
    struct sockaddr_in      sin;
    struct sockaddr_in6     sin6;
    struct sockaddr_un      sunix;
    struct sockaddr         sa;
    sa_family_t             family;
} sockunion_t;

bool sockunion_equal(const sockunion_t * nonnull,
                     const sockunion_t * nonnull);

static inline int sockunion_getport(const sockunion_t * nonnull su)
{
    switch (su->family) {
      case AF_INET:  return ntohs(su->sin.sin_port);
      case AF_INET6: return ntohs(su->sin6.sin6_port);
      default:       return 0;
    }
}
static inline void sockunion_setport(sockunion_t * nonnull su, int port)
{
    switch (su->family) {
      case AF_INET:  su->sin.sin_port   = htons(port); break;
      case AF_INET6: su->sin6.sin6_port = htons(port); break;
      default:       e_panic("should not happen");
    }
}

/** Convert IPv4 and IPv6 addresses into a string.
 *
 * This function is a helper that call inet_ntop() with appropriate arguments
 * according to "su->family" value.
 *
 * On error, "errno" is set appropriately. See inet_ntop(3) for more details.
 *
 * \return length of string written in "buf"
 * \retval -1 on error
 */
int sockunion_gethost(const sockunion_t * nonnull su, char * nonnull buf,
                      int size);

/** Convert IPv4 and IPv6 addresses into a string.
 *
 * This helper is a wrapper around sockunion_gethost() that allocates memory
 * into the t_stack.
 *
 * \see sockunion_gethost()
 *
 * \return network address as a lstr_t
 * \return LSTR_NULL_V on error
 */
lstr_t t_sockunion_gethost_lstr(const sockunion_t * nonnull su);

static inline socklen_t sockunion_len(const sockunion_t * nonnull su)
{
    switch (su->family) {
      case AF_INET:
        return sizeof(struct sockaddr_in);
      case AF_INET6:
        return sizeof(struct sockaddr_in6);
      case AF_UNIX:
        /* XXX: the +1 isn't a bug, it's really what we mean
         *      it's to support linux abstract sockets
         */
        return offsetof(struct sockaddr_un, sun_path)
            + 1 + strlen(su->sunix.sun_path + 1);
      default:
        return (socklen_t)-1;
    }
}
__attribute__((pure))
uint32_t sockunion_hash(const sockunion_t * nonnull su);

/* This helper allows to iterate on an array of sockunion_t where each 'su'
 * can have different lengths (e.g. by mixing IPv4 and IPv6).
 */
#define sockunion_for_each(su, sus, len)                                     \
    for (typeof(sus) su = (sus), __su_start = su, __su_broken = su + (len);  \
         __su_broken-- != __su_start;                                        \
         su = (typeof(sus))((byte *)su + sockunion_len(su)))

/* -1 as defport means port is mandatory */
int addr_parse_minport(pstream_t ps, pstream_t * nonnull host,
                       in_port_t * nonnull port, int minport, int defport);
static inline int addr_parse(pstream_t ps, pstream_t * nonnull host,
                             in_port_t * nonnull port, int defport)
{
    return addr_parse_minport(ps, host, port, 1, defport);
}
int addr_info(sockunion_t * nonnull, sa_family_t, pstream_t host, in_port_t);

/** Convert a TCP/IPv4, TCP/IPv6 or UNIX address into a string.
 *
 * Examples:
 *
 * IPv4:
 *    x.x.x.x:port
 *
 * IPv6:
 *    [x:x:x:x:x:x:x:x]:port
 *
 * UNIX:
 *    @/var/run/zpf-master.ctl
 *
 * \param[in]  su    sockunion filled with a network address
 * \param[out] slen  length of the formatted string
 *
 * \return string allocated in t_stack
 */
const char * nonnull t_addr_fmt(const sockunion_t * nonnull su,
                                int * nullable slen);
static inline lstr_t t_addr_fmt_lstr(const sockunion_t * nonnull su)
{
    int len;
    const char *s = t_addr_fmt(su, &len);
    return lstr_init_(s, len, MEM_STACK);
}

static inline int
addr_parse_str(const char * nonnull s, pstream_t * nonnull host,
               in_port_t * nonnull port, int defport)
{
    return addr_parse(ps_initstr(s), host, port, defport);
}

static inline int
addr_info_str(sockunion_t * nonnull su, const char * nonnull host, int port,
              int af)
{
    return addr_info(su, af, ps_initstr(host), port);
}

/** Build addr filter from a subnetwork.
 *
 * A subnetwork is identified by its CIDR notation, e.g. 192.168.0.1/24 or
 * fe80::202:b3ff:fe1e:8329/32, or by a single IP address, 192.168.0.12 or
 * ff09::1234:abcd.
 *
 * \param[in]  subnet the subnetwork.
 * \param[out] filter resulting filter.
 * \return -1 in case of error, 0 otherwise.
 */
int addr_filter_build(lstr_t subnet, addr_filter_t * nonnull filter);

int addr_filter_matches(const addr_filter_t * nonnull filter,
                        const sockunion_t * nonnull peer);

/** Resolve an address.
 *
 * \param[in]  what     description of the address to resolve
 * \param[in]  s        address to resolve
 * \param[in]  minport  minimum port authorized (if 0, port can be missing)
 * \param[in]  defport  default port, if not present in the address
 *
 * \param[out] out_su    sockunion_t to fill with the resolution of the
 *                       address
 * \param[out] out_host  host of the address
 * \param[out] out_port  port of the address
 * \param[out] err       buffer to fill in case of error
 *
 * \return  0 if success, -1 if error
 */
int addr_resolve2(const char * nonnull what, const lstr_t s,
                  int minport, int defport,
                  sockunion_t * nonnull out_su,
                  pstream_t * nullable out_host,
                  in_port_t * nullable out_port,
                  sb_t * nullable err);

static inline int addr_resolve(const char * nonnull what, const lstr_t s,
                               sockunion_t * nonnull out)
{
    SB_1k(err);
    pstream_t host;
    in_port_t port;

    if (addr_resolve2(what, s, 1, -1, out, &host, &port, &err) < 0) {
        e_error("%*pM", SB_FMT_ARG(&err));
        return -1;
    }
    return 0;
}

#define HTTP_URL_CREDS_SIZE  128
#define HTTP_URL_HOST_SIZE   128
#define HTTP_URL_PATH_SIZE   512

typedef struct http_url_t {
    char user[HTTP_URL_CREDS_SIZE];
    char pass[HTTP_URL_CREDS_SIZE];
    char host[HTTP_URL_HOST_SIZE];
    int port;
    char path[HTTP_URL_PATH_SIZE];
    char args[HTTP_URL_PATH_SIZE];
    char path_without_args[HTTP_URL_PATH_SIZE];
} http_url_t;

/** Parse an http url into its components (http_url_t).
 *
 * \param[in]  url_path    full http(s) url to parse
 * \param[in]  allow_https whether to accept https urls
 *
 * \param[out] url         parsed url components
 *
 * \return  0 if success, -1 if error
 */
int parse_http_url(const char *nonnull url_path, bool allow_https,
                   http_url_t *nonnull url);

#endif
