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

#include <lib-common/z.h>
#include <lib-common/net.h>

/* {{{ net_addr */

static int
zchk_net_addr_parse_url_no_https(const char *addr, const char *user,
                                 const char *pass, const char *host, int port,
                                 const char *path, const char *args,
                                 const char *path_no_args)
{
    http_url_t url;

    Z_ASSERT_N(parse_http_url(addr, false, &url));

    Z_ASSERT_STREQUAL(url.user, user);
    Z_ASSERT_STREQUAL(url.pass, pass);
    Z_ASSERT_STREQUAL(url.host, host);
    Z_ASSERT(url.port == port);
    Z_ASSERT_STREQUAL(url.path, path);
    Z_ASSERT_STREQUAL(url.args, args);
    Z_ASSERT_STREQUAL(url.path_without_args, path_no_args);

    Z_HELPER_END;
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
        sockunion_t su;
        addr_filter_t filter;
        char buf[INET6_ADDRSTRLEN];

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
        sockunion_t su;
        addr_filter_t filter;
        char buf[INET6_ADDRSTRLEN];

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
        sockunion_t su;

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



    Z_TEST(parse_http_url, "parse_http_url")
    {
        http_url_t url;

#define T_VALID(_addr, _user, _pass, _host, _port, _path, _args,             \
                _path_no_args)                                               \
    Z_HELPER_RUN(zchk_net_addr_parse_url_no_https(                           \
        _addr, _user, _pass, _host, _port, _path, _args, _path_no_args))

        T_VALID("http://host.com/test", "", "", "host.com", 80, "/test", "",
                "/test");
        T_VALID("http://host.com/test@test", "", "", "host.com", 80,
                "/test@test", "", "/test@test");
        T_VALID("http://localhost", "", "", "localhost", 80, "/", "", "/");
        T_VALID("http://host.com", "", "", "host.com", 80, "/", "", "/");
        T_VALID("http://host.com:8080", "", "", "host.com", 8080,
                "/", "", "/");
        T_VALID("http://user:pass@host.com/", "user", "pass", "host.com", 80,
                "/", "", "/");
        T_VALID("http://host.com/test?args", "", "", "host.com", 80,
                "/test?args", "?args", "/test");
        T_VALID("http://user:pass@host.com:42/test?args", "user", "pass",
                "host.com", 42, "/test?args", "?args", "/test");
        T_VALID("http://user:pass@host.com:42/test@test", "user", "pass",
                "host.com", 42, "/test@test", "", "/test@test");
#undef T_VALID

#define T_INVALID(_addr)  Z_ASSERT_NEG(parse_http_url(_addr, false, &url))
        T_INVALID("toto");
        T_INVALID("http://");
        T_INVALID("http://user@host.com");
        T_INVALID("http://user@host.com:42/test?args");
        T_INVALID("user@host.com");
        T_INVALID("http://host.com:-8080");
        T_INVALID("http://host.com:");
        T_INVALID("http://host.com:/test?args");
        T_INVALID("http://:pass@host.com");
        T_INVALID("http://user:@host.com");
#undef T_INVALID

        /* HTTP port. */
        Z_ASSERT_N(parse_http_url("http://host.com", true, &url));
        Z_ASSERT_EQ(url.port, 80);

        /* HTTPS port. */
        Z_ASSERT_N(parse_http_url("https://host.com", true, &url));
        Z_ASSERT_EQ(url.port, 443);

    } Z_TEST_END;

} Z_GROUP_END;

/* }}} */
