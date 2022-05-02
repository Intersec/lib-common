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

#include <netdb.h>
#include <lib-common/z.h>
#include <lib-common/net.h>
#include <lib-common/hash.h>

bool sockunion_equal(const sockunion_t *a1, const sockunion_t *a2)
{
    if (a1->family != a2->family)
        return false;

    switch (a1->family) {
      case AF_INET:
        return a1->sin.sin_port == a2->sin.sin_port
            && a1->sin.sin_addr.s_addr == a2->sin.sin_addr.s_addr;

      case AF_INET6:
        if (a1->sin6.sin6_port != a2->sin6.sin6_port)
            return false;
        return !memcmp(a1->sin6.sin6_addr.s6_addr, a2->sin6.sin6_addr.s6_addr,
                       sizeof(a2->sin6.sin6_addr.s6_addr));

      case AF_UNIX:
        return !strcmp(a1->sunix.sun_path, a2->sunix.sun_path);

      default:
        e_panic("unknown kind of sockaddr: %d", a1->family);
    }
}

uint32_t sockunion_hash(const sockunion_t *su)
{
    uint64_t u64;
    uint32_t u32;

    switch (su->family) {
      case AF_INET:
        u64 = su->sin.sin_family | (su->sin.sin_port << 16)
            | ((uint64_t)su->sin.sin_addr.s_addr << 32);
        return u64_hash32(u64);

      case AF_INET6:
        u32 = su->sin6.sin6_family | (su->sin6.sin6_port << 16);
        return u32 ^ mem_hash32(su->sin6.sin6_addr.s6_addr,
                                sizeof(su->sin6.sin6_addr.s6_addr));

      case AF_UNIX:
        return mem_hash32(&su->sunix, sockunion_len(su));

      default:
        e_panic("unknown kind of sockaddr: %d", su->family);
    }
}

int sockunion_gethost(const sockunion_t *su, char *buf, int size)
{
    switch (su->family) {
      case AF_INET:
        RETHROW_PN(inet_ntop(AF_INET, &su->sin.sin_addr.s_addr, buf, size));
        break;

      case AF_INET6:
        RETHROW_PN(inet_ntop(AF_INET6, &su->sin6.sin6_addr.s6_addr, buf,
                             size));
        break;

      default:
        return -1;
    }
    return strlen(buf);
}

lstr_t t_sockunion_gethost_lstr(const sockunion_t *su)
{
    int size = MAX(INET_ADDRSTRLEN, 2 + INET6_ADDRSTRLEN);
    char *buf;
    int len;

    buf = t_new(char, size);
    len = sockunion_gethost(su, buf, size);
    if (len < 0) {
        return LSTR_NULL_V;
    }

    return lstr_init_(buf, len, MEM_STACK);
}

int addr_parse_minport(pstream_t ps, pstream_t *host, in_port_t *port,
                       int minport, int defport)
{
    int i;

    PS_WANT(ps_has(&ps, 1));
    if (ps.b[0] == '[') {
        __ps_skip(&ps, 1);
        PS_CHECK(ps_get_ps_chr(&ps, ']', host));
        __ps_skip(&ps, 1);
    } else {
        if (ps_get_ps_chr(&ps, ':', host) < 0) {
            *host = ps;
            ps.b  = ps.b_end;
            goto no_port;
        }
    }
    if (!ps_has(&ps, 1)) {
      no_port:
        *port = defport;
        return defport < 0 ? -1 : 0;
    }
    PS_WANT(__ps_getc(&ps) == ':');
    PS_WANT(ps_has(&ps, 1));
    i = ps_geti(&ps);
    if (i < minport || i > UINT16_MAX)
        return -1;
    *port = i;
    return ps_done(&ps) ? 0 : -1;
}

int addr_resolve2(const char * nonnull what, const lstr_t s,
                  int minport, int defport,
                  sockunion_t * nonnull out_su,
                  pstream_t * nullable out_host,
                  in_port_t * nullable out_port,
                  sb_t * nullable err)
{
    pstream_t host;
    in_port_t port;

    if (addr_parse_minport(ps_init(s.s, s.len), &host, &port,
                           minport, defport) < 0)
    {
        if (err) {
            sb_addf(err, "unable to parse %s address `%*pM`",
                    what, LSTR_FMT_ARG(s));
        }
        return -1;
    }
    if (addr_info(out_su, AF_UNSPEC, host, port) < 0) {
        if (err) {
            sb_addf(err, "unable to resolve %s address `%*pM`",
                    what, LSTR_FMT_ARG(s));
        }
        return -1;
    }

    if (out_host) {
        *out_host = host;
    }
    if (out_port) {
        *out_port = port;
    }

    return 0;
}

const char *t_addr_fmt(const sockunion_t *su, int *slen)
{
    char buf[BUFSIZ];
    size_t pos;

    switch (su->family) {
      case AF_INET:
        inet_ntop(AF_INET, &su->sin.sin_addr.s_addr, buf, sizeof(buf));
        pos = strlen(buf);
        break;

      case AF_INET6:
        buf[0] = '[';
        inet_ntop(AF_INET6, &su->sin6.sin6_addr.s6_addr,
                  buf + 1, sizeof(buf) - 1);
        pos = strlen(buf);
        buf[pos++] = ']';
        break;

      case AF_UNIX: {
        lstr_t res;

        if (su->sunix.sun_path[0] == '\0') {
            if (su->sunix.sun_path[1] == '\0') {
                res = LSTR("unknown unix socket");
            } else {
                res = t_lstr_fmt("@%s", su->sunix.sun_path + 1);
            }
        } else {
            res = LSTR(su->sunix.sun_path);
        }
        if (slen) {
            *slen = res.len;
        }
        return res.s;
      }

      default:
        if (slen) {
            *slen = 0;
        }
        return "";
    }

    /* Add port for AF_INET and AF_INET6. */
    assert (pos < sizeof(buf));
    pos += snprintf(buf + pos, sizeof(buf) - pos, ":%d",
                    sockunion_getport(su));
    if (slen) {
        *slen = pos;
    }
    return t_dupz(buf, pos);
}

int addr_info(sockunion_t *su, sa_family_t af, pstream_t host, in_port_t port)
{
    t_scope;
    struct addrinfo *ai = NULL;
    struct addrinfo *cur;
    struct addrinfo hint = { .ai_family = af };

    RETHROW(getaddrinfo(t_dupz(host.p, ps_len(&host)), NULL, &hint, &ai));
    for (cur = ai; cur != NULL; cur = cur->ai_next) {
        switch (cur->ai_family) {
          case AF_INET:
          case AF_INET6:
          case AF_UNIX:
            if (cur->ai_addrlen > ssizeof(*su)) {
                continue;
            }
            break;

          default:

            continue;
        }
        break;
    }

    if (cur) {
        memcpy(su, cur->ai_addr, cur->ai_addrlen);
        sockunion_setport(su, port);
    }
    freeaddrinfo(ai);
    return cur ? 0 : -1;
}

int addr_filter_build(lstr_t subnet, addr_filter_t *filter)
{
    t_scope;
    pstream_t ps = ps_initlstr(&subnet);
    pstream_t ip = ps_init(NULL, 0);
    int maxmask = 32;
    struct addrinfo *ai = NULL;
    struct addrinfo hint = {
        .ai_flags = AI_NUMERICHOST,
        .ai_family = AF_UNSPEC,
    };

    if (ps_get_ps_chr_and_skip(&ps, '/', &ip) < 0) {
        SWAP(pstream_t, ip, ps);
    }

    RETHROW(getaddrinfo(t_dupz(ip.s, ps_len(&ip)), NULL, &hint, &ai));

    p_clear(filter, 1);
    filter->family = ai->ai_family;
    if (filter->family == AF_INET) {
        filter->u.v4.addr =
            ((struct sockaddr_in*)(ai->ai_addr))->sin_addr.s_addr;
    } else
    if (filter->family == AF_INET6) {
        filter->u.v6.addr = ((struct sockaddr_in6*)ai->ai_addr)->sin6_addr;
        maxmask = 128;
    } else {
        freeaddrinfo(ai);
        return -1;
    }

    freeaddrinfo(ai);

    if (!ps_done(&ps)) {
        int mask;

        errno = 0;
        mask = ps_geti(&ps);

        THROW_ERR_IF(errno != 0 || !ps_done(&ps) || mask < 0
                  || mask > maxmask);

#define NET_U32_MASK(_mask)  htonl(BITMASK_GE(uint64_t, 32 - (_mask)))
#define NET_U32_MASK_BOUNDED(_mask)  NET_U32_MASK(MAX(MIN((_mask), 32), 0))

        if (filter->family == AF_INET) {
            filter->u.v4.mask = NET_U32_MASK(mask);
            filter->u.v4.addr &= filter->u.v4.mask;
        } else {
            filter->u.v6.mask.s6_addr32[0] = NET_U32_MASK_BOUNDED(mask);
            filter->u.v6.mask.s6_addr32[1] = NET_U32_MASK_BOUNDED(mask - 32);
            filter->u.v6.mask.s6_addr32[2] = NET_U32_MASK_BOUNDED(mask - 64);
            filter->u.v6.mask.s6_addr32[3] = NET_U32_MASK_BOUNDED(mask - 96);

            filter->u.v6.addr.s6_addr32[0] &= filter->u.v6.mask.s6_addr32[0];
            filter->u.v6.addr.s6_addr32[1] &= filter->u.v6.mask.s6_addr32[1];
            filter->u.v6.addr.s6_addr32[2] &= filter->u.v6.mask.s6_addr32[2];
            filter->u.v6.addr.s6_addr32[3] &= filter->u.v6.mask.s6_addr32[3];
        }

#undef NET_U32_MASK_BOUNDED
#undef NET_U32_MASK
    } else {
        if (filter->family == AF_INET) {
            filter->u.v4.mask = UINT32_MAX;
        } else {
            filter->u.v6.mask.s6_addr32[0] = UINT32_MAX;
            filter->u.v6.mask.s6_addr32[1] = UINT32_MAX;
            filter->u.v6.mask.s6_addr32[2] = UINT32_MAX;
            filter->u.v6.mask.s6_addr32[3] = UINT32_MAX;
        }
    }

    return 0;
}

int addr_filter_matches(const addr_filter_t *filter, const sockunion_t *peer)
{
    if (peer->family != filter->family)
        return -1;

    if (filter->port && filter->port != sockunion_getport(peer))
        return -1;

    if (filter->family == AF_INET) {
        if (filter->u.v4.addr
        != (peer->sin.sin_addr.s_addr & filter->u.v4.mask))
        {
            return -1;
        }
    } else {
        /* filter->family == AF_INET6 */
        for (int i = 3; i >= 0; i--) {
            if (filter->u.v6.addr.s6_addr32[i]
            != (peer->sin6.sin6_addr.s6_addr32[i]
                  & filter->u.v6.mask.s6_addr32[i]))
            {
                return -1;
            }
        }
    }

    return 0;
}

static int getport(const char *str, const char **end)
{
    long port;

    if (strtolp(str, end, 10, &port, STRTOLP_CHECK_RANGE, 1, 65535)) {
        return -1;
    }
    return port;
}

int parse_http_url(const char *url_path, bool allow_https, http_url_t *url)
{
    static bool first_call = true;
    static ctype_desc_t ctype_isurlseparator;
    pstream_t ps;
    pstream_t host_ps, user_ps, pass_ps, path_ps;

    RETHROW_PN(url);
    p_clear(url, 1);
    url->port = 80;

    ps = ps_initstr(url_path);

    /* A URL should always start with "http://" or "https://" with proxy */
    PS_WANT(!ps_skipstr(&ps, "http"));
    if (allow_https) {
        if (ps_startswith(&ps, "s", 1)) {
            url->port = 443;
        }
        ps_skipstr(&ps, "s");
    }
    PS_WANT(!ps_skipstr(&ps, "://"));
    PS_WANT(!ps_done(&ps));

    if (unlikely(first_call)) {
        ctype_desc_build(&ctype_isurlseparator, ":/@");
        first_call = false;
    }


    host_ps = ps;
    user_ps = ps_get_cspan(&ps, &ctype_isurlseparator);

#define PS_COPY(_field, _ps)  \
    pstrcpymem(url->_field, sizeof(url->_field), _ps.s, ps_len(&_ps))

    if (!ps_done(&ps) && *ps.s == ':') {
        /* Colon is either the separator of user:passwd
         * or the separator of host:port */
        __ps_skip(&ps, 1);
        pass_ps = ps_get_cspan(&ps, &ctype_isurlseparator);
        if (!ps_done(&ps) && *ps.s == '@') {
            /* @ is the separator of user:passwd@host so assume
             * the colon was a user:passwd separator */
            __ps_skip(&ps, 1);
            PS_WANT(!ps_done(&ps));
            PS_WANT(!ps_done(&user_ps));
            PS_WANT(!ps_done(&pass_ps));
            PS_COPY(user, user_ps);
            PS_COPY(pass, pass_ps);

            host_ps = ps_get_cspan(&ps, &ctype_isurlseparator);
            if (!ps_done(&ps) && *ps.s == ':') {
                __ps_skip(&ps, 1);
                url->port = getport(ps.s, &ps.s);
            }
        } else {
            /* The colon was a port separator so the preceding
             * part was the host (not the user) */
            host_ps = user_ps;
            url->port = getport(pass_ps.s, &pass_ps.s);
        }
    } else {
        host_ps = user_ps;
    }

    PS_WANT(url->port > 0);

    /* hostname cannot be empty */
    PS_WANT(!ps_done(&host_ps));

    /* After parsing the host part, we must have reach a path separator */
    PS_WANT(ps_done(&ps) || *ps.s == '/');
    PS_COPY(host, host_ps);

    /* Parse path */
    if (ps_done(&ps)) {
        pstrcpy(url->path, sizeof(url->path), "/");
        pstrcpy(url->path_without_args, sizeof(url->path_without_args), "/");
    } else {
        PS_COPY(path, ps);
        if (ps_get_ps_chr(&ps, '?', &path_ps) < 0) {
            path_ps = ps;
            ps = ps_init(NULL, 0);
        }
        PS_COPY(path_without_args, path_ps);
        PS_COPY(args, ps);
    }
#undef PS_COPY

    return 0;
}
