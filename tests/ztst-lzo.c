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

#include <lib-common/qlzo.h>

#define MODE_C  0
#define MODE_D  1

static int compress(const char *in, const char *out)
{
    byte buf[LZO_BUF_MEM_SIZE];
    sb_t sb, sbout;
    size_t sz;
    char path[PATH_MAX];

    sb_init(&sb);
    sb_init(&sbout);

    if (in) {
        RETHROW(sb_read_file(&sb, in));
    } else {
        while (RETHROW(sb_read(&sb, STDIN_FILENO, 0)) > 0) {
        }
    }

    sb_add(&sbout, &sb.len, 4);
    sz = lzo_cbuf_size(sb.len);
    sz = qlzo1x_compress(sb_growlen(&sbout, sz), sz,
                         ps_init(sb.data, sb.len), buf);
    __sb_fixlen(&sbout, 4 + sz);

    if (!out) {
        out = path;
        snprintf(path, sizeof(path), "%s.lzo", in ?: "out");
    }
    RETHROW(sb_write_file(&sbout, out));
    sb_wipe(&sb);
    sb_wipe(&sbout);
    return 0;
}

static int do_self_test(void)
{
    char *ibuf, *cbuf, *obuf;
    ssize_t ilen, clen, olen;
    byte dict[LZO_BUF_MEM_SIZE];

    for (int i = 0;; i++) {
        t_scope;

        ilen = rand_range(0, (64 << 10) - 1);
        ibuf  = t_new_raw(char, ilen + 1);
        obuf = t_new_raw(char, ilen);

        for (ssize_t j = 0; j < ilen / 2; j++)
            ((uint16_t *)ibuf)[j] = rand();

        clen = lzo_cbuf_size(ilen);
        cbuf = t_new_raw(char, clen + LZO_INPUT_PADDING);
        clen = qlzo1x_compress(cbuf, clen, ps_init(ibuf, ilen), dict);

        olen = qlzo1x_decompress(obuf, ilen, ps_init(cbuf, clen));
        assert (olen == ilen && memcmp(ibuf, obuf, ilen) == 0);
        e_trace(0, "%d: %zd bytes ok", i, ilen);
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *out = NULL;
    int mode = MODE_C;
    int c;

    while ((c = getopt(argc, argv, "cdo:r")) >= 0) {
        switch (c) {
          case 'c':
            mode = MODE_C;
            break;
          case 'd':
            mode = MODE_D;
            break;
          case 'o':
            out = optarg;
            break;
          case 'r':
            return do_self_test();
          default:
            fprintf(stderr, "error: unknown option '%c'", c);
            exit(1);
        }
    }
    if (argc - optind > 1) {
        fprintf(stderr, "error: too many arguments");
        exit(1);
    }

    if (mode == MODE_C) {
        return compress(argv[argc - 1], out);
    } else {
        //decompress(argv[argc - 1], out);
        return -1;
    }
}
