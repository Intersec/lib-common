/**************************************************************************/
/*                                                                        */
/*  Copyright (C) INTERSEC SA                                             */
/*                                                                        */
/*  Should you receive a copy of this source code, you must check you     */
/*  have a proper, written authorization of INTERSEC to hold it. If you   */
/*  don't have such an authorization, you must DELETE all source code     */
/*  files in your possession, and inform INTERSEC of the fact you obtain  */
/*  these files. Should you not comply to these terms, you can be         */
/*  prosecuted in the extent permitted by applicable law.                 */
/*                                                                        */
/**************************************************************************/

#include <lib-common/qps-bitmap.h>
#include <lib-common/parseopt.h>
#include <lib-common/datetime.h>

static struct {
    bool help;
    bool nullable_bitmap;
    bool specialized_impl;
    bool unsafe_impl;
    int repeat;
} settings_g = {
    .repeat = 1,
};

static popt_t popts_g[] = {
    OPT_FLAG('h', "help", &settings_g.help, "show this help"),
    OPT_FLAG('n', "nullable", &settings_g.nullable_bitmap,
             "test a nullable bitmap"),
    OPT_FLAG('p', "specialized", &settings_g.specialized_impl,
             "use specialized bitmap enumerator"),
    OPT_FLAG('u', "unsafe", &settings_g.unsafe_impl,
             "use unsafe bitmap enumerator"),
    OPT_INT('r', "repeat", &settings_g.repeat,
            "repeat the scan <value> time(s) to get smoother results"),
    /* TODO set dir */
    /* TODO nb elements */
    OPT_END(),
};

static void z_qps_bitmap_fill(qps_bitmap_t *bitmap, int nb_elements,
                              bool is_nullable)
{
    for (int i = 0; i < nb_elements; i++) {
        if (is_nullable && (rand() & 1)) {
            qps_bitmap_reset(bitmap, rand());
        } else {
            qps_bitmap_set(bitmap, rand());
        }
    }
}

static void z_qps_bitmap_scan(qps_bitmap_t *bitmap, bool is_nullable,
                              bool generic, bool safe, int repeat)
{
    int nb_elements = 0;
    proctimer_t pt;
    int elapsed;

    proctimer_start(&pt);

#define BITMAP_SCAN_LOOP(_bitmap, _next_func, _repeat, _safe)                \
    for (int i = 0; i < _repeat; i++) {                                      \
        for (qps_bitmap_enumerator_t en =                                    \
                 qps_bitmap_get_enumerator(_bitmap);                         \
             !en.end; _next_func(&en, _safe))                                \
        {                                                                    \
            nb_elements += en.value;                                         \
        }                                                                    \
    }

    /* Force unrolling of "safe" parameter. */
#define BITMAP_SCAN(_bitmap, _next_func, _repeat, _safe)                     \
    if (_safe) {                                                             \
        BITMAP_SCAN_LOOP(_bitmap, _next_func, _repeat, true);                \
    } else {                                                                 \
        BITMAP_SCAN_LOOP(_bitmap, _next_func, _repeat, false);               \
    }

    if (generic) {
        /* Generic implementation. */
        BITMAP_SCAN(bitmap, qps_bitmap_enumerator_next, repeat, safe);
    } else if (is_nullable) {
        /* Specialized nullable implementation. */
        BITMAP_SCAN(bitmap, qps_bitmap_enumerator_next_nu, repeat, safe);
    } else {
        /* Specialized non-nullable implementation. */
        BITMAP_SCAN(bitmap, qps_bitmap_enumerator_next_nn, repeat, safe);
    }

#undef BITMAP_SCAN
#undef BITMAP_SCAN_LOOP

    elapsed = proctimer_stop(&pt);
    printf("\t(%s %s scan)\t%d element(s) scanned %d time(s) in %d.%06d s\n",
           safe ? "safe" : "unsafe",
           generic ? "generic" : "specialized",
           nb_elements, repeat,
           elapsed / 1000000, elapsed % 1000000);
}

static void z_qps_bitmap_bench(qps_t *qps, int nb_elements, bool is_nullable,
                               bool generic, bool safe, int repeat)
{
    qps_handle_t bitmap_handle;
    qps_bitmap_t bitmap;
    proctimer_t pt;
    int elapsed;

    printf("QPS bitmap bench %d element(s), nullable=%d\n", nb_elements,
           is_nullable);
    bitmap_handle = qps_bitmap_create(qps, is_nullable);
    qps_bitmap_init(&bitmap, qps, bitmap_handle);

    proctimer_start(&pt);
    z_qps_bitmap_fill(&bitmap, nb_elements, is_nullable);
    elapsed = proctimer_stop(&pt);
    printf("\tbitmap filled with %d element(s) in %d.%06d s\n",
           nb_elements, elapsed / 1000000, elapsed % 1000000);

    z_qps_bitmap_scan(&bitmap, is_nullable, generic, safe, repeat);

    qps_bitmap_destroy(&bitmap);
}

int main(int argc, char **argv)
{
    char tmpdir[] = "qps-bitmap-spool-XXXXXX";
    qps_t *qps;
    bool safe;
    bool generic;

    argc = parseopt(argc, argv, popts_g, 0);
    if (settings_g.help) {
        makeusage(0, argv[0], "", NULL, popts_g);
    }
    safe = !settings_g.unsafe_impl;
    generic = !settings_g.specialized_impl;

    if (!mkdtemp(tmpdir)) {
        fprintf(stderr, "failed to create tmp dir %s: %m\n", tmpdir);
        return EXIT_FAILURE;
    }

    MODULE_REQUIRE(qps);

    qps = qps_create(tmpdir, "bitmap", 0755, NULL, 0);
    if (!qps) {
        fprintf(stderr, "cannot create QPS in tmp dir %s\n", tmpdir);
        return EXIT_FAILURE;
    }

    z_qps_bitmap_bench(qps, 64 << 20,
                       settings_g.nullable_bitmap,
                       generic, safe,
                       settings_g.repeat);

    qps_close(&qps);
    MODULE_RELEASE(qps);

    if (rmdir_r(tmpdir, false) < 0) {
        fprintf(stderr, "failed to remove tmp dir %s: %m\n", tmpdir);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
