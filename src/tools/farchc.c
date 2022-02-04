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

#include <lib-common/parseopt.h>
#include <lib-common/qlzo.h>
#include <lib-common/unix.h>
#include <lib-common/farch.h>

static struct opts_g {
    const char *out;
    const char *target;
    const char *deps;
    int help;
    int verbose;
    int compress_lzo;
} opts_g;

static popt_t popt[] = {
    OPT_FLAG('h', "help",         &opts_g.help,    "show help"),
    OPT_FLAG('v', "verbose",      &opts_g.verbose, "be verbose"),
    OPT_STR('d',  "deps",         &opts_g.deps,    "build depends file"),
    OPT_STR('o',  "output",       &opts_g.out,
            "output to this file, default: stdout"),
    OPT_STR('T',  "target",       &opts_g.target,
            "add that to the dep target"),
    OPT_FLAG('c', "compress-lzo", &opts_g.compress_lzo,
             "compress files using LZO algorithm"),
    OPT_END(),
};

static void panic_purge(void)
{
    if (opts_g.out) {
        unlink(opts_g.out);
    }
    exit(EXIT_FAILURE);
}

#define TRACE(fmt, ...) \
    do { if (opts_g.verbose) e_info("farchc: "fmt, ##__VA_ARGS__); } while (0)

#define DIE_IF(tst, fmt, ...) \
    do { if (tst) { e_error(fmt, ##__VA_ARGS__); panic_purge(); } } while (0)

static void put_as_str(lstr_t chunk, FILE *out)
{
    for (int i = 0; i < chunk.len; i++) {
        fprintf(out, "\\x%x", chunk.s[i]);
    }
}

static void put_chunk(lstr_t chunk, FILE *out)
{
    fprintf(out,
            "    LSTR_IMMED(\"");
    put_as_str(chunk, out);
    fprintf(out, "\"),\n");
}

static int dump_and_obfuscate(const char *data, int len, FILE *out)
{
    int nb_chunk = 0;

    while (len > 0) {
        char buffer[FARCH_MAX_SYMBOL_SIZE];
        lstr_t obfuscated_chunk;
        uint32_t chunk_size;

        chunk_size = rand_range(FARCH_MAX_SYMBOL_SIZE / 2,
                                FARCH_MAX_SYMBOL_SIZE);
        chunk_size = MIN((uint32_t)len, chunk_size);
        obfuscated_chunk = LSTR_INIT_V(buffer, chunk_size);
        lstr_obfuscate(LSTR_INIT_V(data, chunk_size), chunk_size,
                       obfuscated_chunk);
        put_chunk(obfuscated_chunk, out);
        data += chunk_size;
        len -= chunk_size;
        nb_chunk++;
    }

    assert (len == 0);

    return nb_chunk;
}

static void dump_file(const char *path, farch_entry_t *entry, FILE *out)
{
    lstr_t file;

    DIE_IF(lstr_init_from_file(&file, path, PROT_READ, MAP_SHARED) < 0,
           "unable to open `%s` for reading: %m", path);
    entry->size = file.len;

    if (opts_g.compress_lzo) {
        t_scope;
        byte lzo_buf[LZO_BUF_MEM_SIZE];
        char *cbuf;
        int clen;

        clen = lzo_cbuf_size(file.len);
        cbuf = t_new_raw(char, clen);
        clen = qlzo1x_compress(cbuf, clen, ps_initlstr(&file), lzo_buf);

        if (clen < file.len) {
            entry->nb_chunks = dump_and_obfuscate(cbuf, clen, out);
            entry->compressed_size = clen;
        } else {
            entry->nb_chunks = dump_and_obfuscate(file.s, file.len, out);
            entry->compressed_size = file.len;
        }
    } else {
        entry->nb_chunks = dump_and_obfuscate(file.s, file.len, out);
        entry->compressed_size = file.len;
    }

    lstr_wipe(&file);
}

qvector_t(farch_entry, farch_entry_t);

static void dump_entries(const char *archname,
                         const qv_t(farch_entry) *entries, FILE *out)
{
    int chunk = 0;

    tab_for_each_ptr(entry, entries) {
        char buffer[2 * PATH_MAX];
        lstr_t obfuscated_name = LSTR_INIT(buffer, entry->name.len);

        lstr_obfuscate(entry->name, entry->nb_chunks, obfuscated_name);

        fprintf(out, "/* {""{{ %*pM */\n", LSTR_FMT_ARG(entry->name));
        fprintf(out, "{\n"
                "    .name = LSTR_IMMED(\"");
        put_as_str(obfuscated_name, out);
        fprintf(out, "\"),\n"
                "    .chunks = &%s_data[%d],\n"
                "    .size = %d,\n"
                "    .compressed_size = %d,\n"
                "    .nb_chunks = %d,\n"
                "},\n"
                "/* }""}} */\n",
                archname, chunk, entry->size, entry->compressed_size,
                entry->nb_chunks);
        chunk += entry->nb_chunks;
    }
}

static int do_work(const char *reldir, FILE *in, FILE *out, FILE *deps)
{
    t_scope;
    SB_1k(dep);
    char srcdir[PATH_MAX];
    char buf[PATH_MAX];
    char name[PATH_MAX];
    int srcdirlen = 0;
    qv_t(farch_entry) entries;

    t_qv_init(&entries, 10);

    do {
        DIE_IF(!fgets(name, sizeof(name), in), "no variable name specified: %m");
        DIE_IF(name[strlen(name) - 1] != '\n', "line 1 is too long: %m");
    } while (*skipspaces(name) == '#' || *skipspaces(name) == '\0');
    strrtrim(name);
    TRACE("creating `%s`", name);

    fprintf(out, "/* This file is generated by farchc. */\n");
    fprintf(out, "\n");
    fprintf(out, "#include <lib-common/farch.h>\n");
    fprintf(out, "\n");
    fprintf(out, "static const farch_data_t %s_data[] = {\n", name);

    if (opts_g.target)
        sb_addf(&dep, "%s%s ", reldir, opts_g.target);
    if (opts_g.out)
        sb_addf(&dep, "%s ", opts_g.out);
    sb_addf(&dep, "%s: ", opts_g.deps);

    snprintf(srcdir, sizeof(srcdir), "%s", reldir);
    path_simplify(srcdir);
    path_join(srcdir, sizeof(srcdir), "/");
    srcdirlen = strlen(srcdir);

    for (int lineno = 2; fgets(buf, sizeof(buf), in); lineno++) {
        const char *s = skipspaces(buf);
        char path[PATH_MAX];
        farch_entry_t entry;
        char *fullname;

        DIE_IF(buf[strlen(buf) - 1] != '\n', "line %d is too long", lineno);
        strrtrim(buf);
        if (!*s) {
            continue;
        }
        if (*s == '#') {
            TRACE("%s", s);
            continue;
        }

        snprintf(path, sizeof(path), "%s%s", srcdir, s);
        fullname = path + srcdirlen;

        if (deps) {
            fprintf(deps, "%s%s\n", dep.data, path);
            fprintf(deps, "%s:\n", path);
        }

        TRACE("adding `%s` as `%s`", path, fullname);
        entry.name = t_lstr_dups(fullname, -1);
        fprintf(out, "/* {""{{ %s */\n", fullname);
        dump_file(path, &entry, out);
        fprintf(out, "/* }""}} */\n");
        qv_append(&entries, entry);
    }

    fprintf(out, "};\n\n"
            "static const farch_entry_t %s[] = {\n", name);
    dump_entries(name, &entries, out);

    fprintf(out,
            "{   .name = LSTR_NULL },\n"
            "};\n");
    return 0;
}

int main(int argc, char *argv[])
{
    t_scope;
    const char *arg0 = NEXTARG(argc, argv);
    char reldir[PATH_MAX];
    FILE *in = stdin, *out = stdout, *deps = NULL;

    argc = parseopt(argc, argv, popt, 0);
    if (argc < 0 || argc > 1 || opts_g.help)
        makeusage(EXIT_FAILURE, arg0, "<farch-script>", NULL, popt);

    if (opts_g.out) {
        out = fopen(opts_g.out, "w");
        DIE_IF(!out, "unable to open `%s` for writing: %m", opts_g.out);
    }

    if (argc > 0) {
        in = fopen(argv[0], "r");
        path_dirname(reldir, sizeof(reldir), argv[0]);
        path_join(reldir, sizeof(reldir), "/");
        DIE_IF(!in, "unable to open `%s` for reading: %m", argv[0]);
    } else {
        reldir[0] = '\0';
    }

    if (opts_g.deps) {
        deps = fopen(opts_g.deps, "w");
        DIE_IF(!deps, "unable to open `%s` for writing: %m", opts_g.deps);
    }

    do_work(reldir, in, out, deps);
    p_fclose(&in);
    p_fclose(&out);
    p_fclose(&deps);
    if (opts_g.out) {
        DIE_IF(chmod(opts_g.out, 0440), "unable to chmod `%s`: %m",
               opts_g.out);
    }
    TRACE("OK !");
    return 0;
}
