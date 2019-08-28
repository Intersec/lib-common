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

#include <netdb.h>
#include "z.h"
#include "net.h"
#include "hash.h"

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

Z_GROUP_EXPORT(net_addr)
{
    t_scope;
#define NET_ADDR_IPV4  "1.1.1.1"
#define NET_ADDR_IPV6  "1:1:1:1:1:1:1:1"
#define NET_ADDR_PORT  4242

    lstr_t ipv4 = LSTR(NET_ADDR_IPV4);
    lstr_t ipv6 = LSTR(NET_ADDR_IPV6);
    lstr_t tcp_ipv4 = LSTR(NET_ADDR_IPV4 ":" TOSTR(NET_ADDR_PORT));
    lstr_t tcp_ipv6 = LSTR("[" NET_ADDR_IPV6 "]:" TOSTR(NET_ADDR_PORT));
    sockunion_t su;
    addr_filter_t filter;
    char buf[INET6_ADDRSTRLEN];

#define CHECK_FILTER(_res, _cidr, _addr, _mask)                              \
    do {                                                                     \
        Z_ASSERT_N(addr_filter_build(LSTR(_cidr), &filter));                 \
        Z_ASSERT_EQ((_res), addr_filter_matches(&filter, &su));              \
                                                                             \
        if (filter.family == AF_INET) {                                      \
            inet_ntop(AF_INET, &filter.u.v4.addr, buf, sizeof(buf));         \
            Z_ASSERT_LSTREQUAL(LSTR(buf), LSTR(_addr));                      \
            inet_ntop(AF_INET, &filter.u.v4.mask, buf, sizeof(buf));         \
            Z_ASSERT_LSTREQUAL(LSTR(buf), LSTR(_mask));                      \
        } else {                                                             \
            inet_ntop(AF_INET6, &filter.u.v6.addr, buf, sizeof(buf));        \
            Z_ASSERT_LSTREQUAL(LSTR(buf), LSTR(_addr));                      \
            inet_ntop(AF_INET6, &filter.u.v6.mask, buf, sizeof(buf));        \
            Z_ASSERT_LSTREQUAL(LSTR(buf), LSTR(_mask));                      \
        }                                                                    \
    } while (0)

    Z_TEST(ipv4, "IPv4") {
        Z_ASSERT_N(addr_info(&su, AF_INET, ps_initlstr(&ipv4),
                             NET_ADDR_PORT));
        Z_ASSERT_LSTREQUAL(ipv4, t_sockunion_gethost_lstr(&su));
        Z_ASSERT_EQ(NET_ADDR_PORT, sockunion_getport(&su));
        Z_ASSERT_LSTREQUAL(t_addr_fmt_lstr(&su), tcp_ipv4);

        CHECK_FILTER(0, "1.1.1.2/25", "1.1.1.0", "255.255.255.128");
        CHECK_FILTER(-1, "1.1.1.130/25", "1.1.1.128", "255.255.255.128");
        CHECK_FILTER(-1, "192.168.0.1/16", "192.168.0.0", "255.255.0.0");
        CHECK_FILTER(-1, "1.1.1.3/32", "1.1.1.3", "255.255.255.255");
        CHECK_FILTER(0, "2.2.2.2/0", "0.0.0.0", "0.0.0.0");
        CHECK_FILTER(0, "1.1.1.1", "1.1.1.1", "255.255.255.255");
        CHECK_FILTER(-1, "1.1.1.4", "1.1.1.4", "255.255.255.255");

    } Z_TEST_END;

    Z_TEST(ipv6, "IPv6") {
        Z_ASSERT_N(addr_info(&su, AF_INET6, ps_initlstr(&ipv6),
                             NET_ADDR_PORT));
        Z_ASSERT_LSTREQUAL(ipv6, t_sockunion_gethost_lstr(&su));
        Z_ASSERT_EQ(NET_ADDR_PORT, sockunion_getport(&su));
        Z_ASSERT_LSTREQUAL(t_addr_fmt_lstr(&su), tcp_ipv6);

        CHECK_FILTER(0, "1:1:1:1:1:1:1:2/65", "1:1:1:1::",
                     "ffff:ffff:ffff:ffff:8000::");
        CHECK_FILTER(-1, "1:1:1:1:abcd:1:1:2/65", "1:1:1:1:8000::",
                     "ffff:ffff:ffff:ffff:8000::");
        CHECK_FILTER(-1, "fe80::202:b3ff:fe1e:8329/32",
                     "fe80::", "ffff:ffff::");
        CHECK_FILTER(-1, "1:1:1:1:1:1:1:3/128", "1:1:1:1:1:1:1:3",
                     "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
        CHECK_FILTER(0, "2:2:2:2:2:2:2:2/0", "::", "::");
        CHECK_FILTER(0, "1:1:1:1:1:1:1:1", "1:1:1:1:1:1:1:1",
                     "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
        CHECK_FILTER(-1, "1:1:1:1:1:1:1:3", "1:1:1:1:1:1:1:3",
                     "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    } Z_TEST_END;
    Z_TEST(sockunion_for_each, "sockunion_for_each") {
        lstr_t ip = LSTR("127.0.0.1:1337");
        SB_1k(sus_buf);
        int idx = 0;

        addr_resolve("IPv4", tcp_ipv4, &su);
        sb_add(&sus_buf, &su, sockunion_len(&su));
        addr_resolve("IPv6", tcp_ipv6, &su);
        sb_add(&sus_buf, &su, sockunion_len(&su));
        addr_resolve("IP", ip, &su);
        sb_add(&sus_buf, &su, sockunion_len(&su));

        sockunion_for_each(sock, (sockunion_t *)sus_buf.data, 3) {
            switch (idx) {
              case 0:
                Z_ASSERT_LSTREQUAL(t_addr_fmt_lstr(sock), tcp_ipv4);
                break;
              case 1:
                Z_ASSERT_LSTREQUAL(t_addr_fmt_lstr(sock), tcp_ipv6);
                break;
              case 2:
                Z_ASSERT_LSTREQUAL(t_addr_fmt_lstr(sock), ip);
                break;
              default:
                Z_ASSERT(false);
                break;
            }
            idx++;
        }
        Z_ASSERT_EQ(idx, 3);
    } Z_TEST_END;

#undef CHECK_FILTER
#undef NET_ADDR_PORT
#undef NET_ADDR_IPV6
#undef NET_ADDR_IPV4

} Z_GROUP_END;
