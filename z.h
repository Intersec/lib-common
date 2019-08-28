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

#ifndef IS_LIB_COMMON_Z_H
#define IS_LIB_COMMON_Z_H

#include <Block.h>
#include "core.h"
#include "container-qvector.h"

/** \defgroup Z Intersec Unit Testing Framework.
 * \brief Intersec Unit Testing Framework.
 *
 * \{
 *
 * \section Z_C Pure C framework.
 *
 * Tests are groupped into #Z_GROUP. Those are run in a separate process.
 * In a #Z_GROUP, tests are defined in a \code Z_TEST { } Z_TEST_END; \endcode
 * block.
 *
 * Beware, do not use anything but:
 * - variable declarations;
 * - #z_todo_start, #z_todo_end, #z_skip_start, #z_skip_end
 * between #Z_TEST blocks, because a #Z_GROUP is called several times, with
 * possible non linear (#Z_TEST are mostly groups of gotos) execution.
 *
 * If you need setup/teardowns, please use the blocks interface.
 *
 *
 * \section Z_BLK C + Blocks framework.
 *
 * This part of the framework is more convenient for metageneration of tests.
 *
 * Here the #Z_GROUP is a \a z_blkgrp, which has the following members:
 *
 * - \a before that is run at the beginning of a group execution. If it fails
 *   (return non zero) then the group is aborted and nothing else is done.
 * - \a after that is run after all tests are run. It is always run (but for
 *   the case where \a before failed).
 * - \a setup and \a teardown. Those are run before and after each test.
 *   \a setup cannot directly fail, but can use the Z_ASSERT* macros or
 *   #Z_SKIP. Its failure will hence be seen as a test failure, so #Z_SKIP is
 *   mostly prefered as a failure to setup() isn't a test failure, rather a
 *   reason to skip it.
 * - \a test is an array of \a z_blktst containing a name and the \a run field
 *   that is the test itself.
 *
 * #z_register_blkgroup is used to register a group into Z. Note that this
 * will fully duplicate the z_blkgrp, so if it was allocated, #z_blkgrp_wipe
 * can be of help.
 *
 * \section Z_ENV Environment.
 *
 * Tests are run in a separate process, on a group basis. If a group crashes,
 * some tests may be skipped, but other groups will run.
 *
 * A temporary directory #z_tmpdir_g is created, and emptied before each test
 * run, so that tests don't need to clean-up files.
 *
 * A temporary directory #z_grpdir_g is created, and emptied before each group
 * run, but are kept for the whole group run.
 *
 * #z_cmddir_g is the path to the directory containing the command being run.
 *
 * Tests can use chdir() (usually to do Z_ASSERT_N(chdir(z_tmpdir_g.s))),
 * because the current working directory is reset before each test run.
 */
/** \} */

typedef void (*z_cb_f)(void);
qvector_t(z_cbs, z_cb_f);

struct z_export {
    struct z_export *prev;
    const char      *file;
    z_cb_f           cb;
};

#ifdef __has_blocks
struct z_blktst {
    lstr_t  name;
    block_t run;
};

struct z_blkgrp {
    lstr_t name;

    int  (BLOCK_CARET before)(void);
    block_t after;

    block_t setup;
    block_t teardown;

    struct z_blktst *tests;
    size_t           len;
};

qvector_t(z_blk, struct z_blkgrp);

/** Wips a z_blkgrp.
 * Performs a #Block_release() on each block.
 * #p_delete()'s tests if \a delete_tests is true.
 */
void z_blkgrp_wipe(struct z_blkgrp *, bool delete_tests);
#endif

/** Name of the scratch directory, has a trailing '/' */
extern lstr_t z_tmpdir_g;
extern int    z_tmpdfd_g;
extern lstr_t z_grpdir_g;
extern int    z_grpdfd_g;
extern lstr_t z_cmddir_g;
extern int    z_cmddfd_g;
extern uint32_t z_modes_g;

enum z_mode {
    Z_MODE_FAST,
};

#define Z_HAS_MODE(Name)  TST_BIT(&z_modes_g, Z_MODE_##Name)

/* private implementations {{{ */

/* z_val_t {{{ */

enum z_val_type_t {
    Z_VAL_TYPE_I,
    Z_VAL_TYPE_U,
    Z_VAL_TYPE_D,
    Z_VAL_TYPE_B,
    Z_VAL_TYPE_C,
};

typedef struct z_val_t {
    enum z_val_type_t type;
    union {
        int64_t i;
        uint64_t u;
        double d;
        bool b;
        int c;
    };
} z_val_t;

#define _Z_VAL_TEST(type_t)  \
    __builtin_types_compatible_p(typeof(_z_val_build_v), type_t)

/* Detect the type for '_v' and fill a 'z_val_t' structure. */
#define _Z_VAL_BUILD(_v)                                                     \
    ({                                                                       \
        z_val_t _z_val_build_res;                                            \
        typeof(_v) _z_val_build_v = (_v);                                    \
                                                                             \
        p_clear(&_z_val_build_res, 1);                                       \
        if (_Z_VAL_TEST(int8_t) || _Z_VAL_TEST(int16_t)                      \
        ||  _Z_VAL_TEST(int32_t) || _Z_VAL_TEST(int64_t)                     \
        ||  _Z_VAL_TEST(int) || _Z_VAL_TEST(long) || _Z_VAL_TEST(long long)) \
        {                                                                    \
            _z_val_build_res.type = Z_VAL_TYPE_I;                            \
            _z_val_build_res.i = _z_val_build_v;                             \
        } else                                                               \
        if (_Z_VAL_TEST(uint8_t) || _Z_VAL_TEST(uint16_t)                    \
        ||  _Z_VAL_TEST(uint32_t) || _Z_VAL_TEST(uint64_t)                   \
        ||  _Z_VAL_TEST(unsigned) || _Z_VAL_TEST(unsigned long)              \
        ||  _Z_VAL_TEST(unsigned long long))                                 \
        {                                                                    \
            _z_val_build_res.type = Z_VAL_TYPE_U;                            \
            _z_val_build_res.u = _z_val_build_v;                             \
        } else                                                               \
        if (_Z_VAL_TEST(float) || _Z_VAL_TEST(double)) {                     \
            _z_val_build_res.type = Z_VAL_TYPE_D;                            \
            _z_val_build_res.d = _z_val_build_v;                             \
        } else                                                               \
        if (_Z_VAL_TEST(bool)) {                                             \
            _z_val_build_res.type = Z_VAL_TYPE_B;                            \
            _z_val_build_res.b = _z_val_build_v;                             \
        } else                                                               \
        if (_Z_VAL_TEST(char)) {                                             \
            _z_val_build_res.type = Z_VAL_TYPE_C;                            \
            _z_val_build_res.c = _z_val_build_v;                             \
        } else {                                                             \
            __error__(#_v " has an unsupported type (for now)");             \
        }                                                                    \
        _z_val_build_res;                                                    \
    })

/* }}} */


extern struct z_export *z_exports_g;

void _z_group_start(const char *name);
bool _z_group_process(void);
void _z_group_done(void);

int  _z_step_run(const char *name);
bool _z_step_is_skipped(int unused, ...) __attribute__((sentinel));
void _z_step_skip(const char *reason, ...) __attr_printf__(1, 2);
void _z_step_todo(const char *reason, ...) __attr_printf__(1, 2);
void _z_step_report(void);

__attr_printf__(9, 10)
bool _z_assert_cmp(const char *file, int lno, const char *op, bool res,
                   const char *lvs, z_val_t lv,
                   const char *rvs, z_val_t rv,
                   const char *fmt, ...);

__attr_printf__(7, 8)
bool _z_assert_lstrequal(const char *file, int lno,
                         const char *lhs, lstr_t lh,
                         const char *rhs, lstr_t rh,
                         const char *fmt, ...);

struct iop_struct_t;
__attr_printf__(8, 9)
bool _z_assert_iopequal(const char *file, int lno,
                        const struct iop_struct_t *st,
                        const char *lhs, const void *lh,
                        const char *rhs, const void *rh,
                        const char *fmt, ...);

__attr_printf__(8, 9)
bool _z_assert_iopjsonequal(const char *file, int lno,
                            const struct iop_struct_t *st,
                            const char *lhs, const void *lh,
                            const char *rhs, lstr_t json,
                            const char *fmt, ...);

__attr_printf__(5, 6)
bool _z_assert(const char *file, int lno, const char *expr, bool res,
               const char *fmt, ...);

__attr_printf__(4, 5)
void _z_helper_failed(const char *file, int lno, const char *expr,
                      const char *fmt, ...);

/* }}} */

/****************************************************************************/
/* Writing tests                                                            */
/****************************************************************************/

#ifdef __has_blocks

#define Z_BLKGROUP(_grp)                                                  \
    __cold static void z_##_grp(void) {                                   \
        static struct z_blkgrp *grp = &_grp;                              \
                                                                          \
        z_register_blkgroup(grp);                                         \
    }

#ifdef NDEBUG
#define Z_BLKGROUP_EXPORT(_grp)  \
    __attribute__((unused)) Z_BLKGROUP(_grp)
#else
#define Z_BLKGROUP_EXPORT(_grp)  \
    __attribute__((constructor)) Z_BLKGROUP(_grp)
#endif /* NDEBUG */

#endif /* __has_blocks */

#define Z_GROUP(name) \
    __cold static void z_##name(void)                                     \
    {                                                                     \
        _z_group_start(#name);                                            \
        while (_z_group_process()) {

#ifdef NDEBUG
#define Z_GROUP_EXPORT(name) \
    __attribute__((unused)) Z_GROUP(name)
#else
#define Z_GROUP_EXPORT(name) \
    static void z_##name(void);                                           \
    static __attribute__((constructor)) void z_##name##_export(void) {    \
        static struct z_export ex = { .cb = z_##name, .file = __FILE__ }; \
                                                                          \
        ex.prev = z_exports_g;                                            \
        z_exports_g = &ex;                                                \
    }                                                                     \
    Z_GROUP(name)
#endif

#define Z_GROUP_END \
        }                                                                 \
        _z_group_done();                                                  \
    }

#define Z_TEST(name, desc) \
    switch (_z_step_run(#name)) {                                         \
        __label__ _z_step_end;                                            \
      case 0:                                                             \
        break;                                                            \
      case 1:                                                             \
        {

#define Z_TEST_FLAGS(...) \
    ({ if (_z_step_is_skipped(0, ##__VA_ARGS__, NULL)) goto _z_step_end; })

#define Z_TEST_END \
        }                                                                 \
        /* FALLTHROUGH */                                                 \
      default:                                                            \
      _z_step_end:                                                        \
        _z_step_report();                                                 \
        break;                                                            \
    }

/** Trailer to use in a sub-function where you want to use.
 *
 * \example
 *   \code
 *   static int some_helper(int arg)
 *   {
 *       Z_ASSERT(something_with_arg(arg));
 *       Z_HELPER_END;
 *   }
 *   \endcode
 */
#define Z_HELPER_END        return 0; _z_step_end: return -1
#define Z_HELPER_RUN(expr, ...) \
    ({  if ((expr) < 0) {                                                 \
            _z_helper_failed(__FILE__, __LINE__, #expr, ""__VA_ARGS__);   \
            goto _z_step_end;                                             \
        }                                                                 \
    })

#define Z_BLKTEST_END  { _z_step_end: return; }

#define Z_SKIP(fmt, ...) \
    ({ _z_step_skip(fmt, ##__VA_ARGS__); goto _z_step_end; })

#define Z_TODO(fmt, ...)  _z_step_todo(fmt, ##__VA_ARGS__)

#define Z_ASSERT(e, ...) \
    ({  bool _z_res = (e);                                                \
        if (_z_assert(__FILE__, __LINE__, #e, _z_res, ""__VA_ARGS__))     \
            goto _z_step_end;                                             \
        assert (_z_res);                                                  \
    })

#define Z_ASSERT_N(e, ...)     Z_ASSERT((e) >= 0, ##__VA_ARGS__)
#define Z_ASSERT_P(e, ...)     Z_ASSERT((e) != NULL, ##__VA_ARGS__)
#define Z_ASSERT_NEG(e, ...)   Z_ASSERT((e) < 0, ##__VA_ARGS__)
#define Z_ASSERT_NULL(e, ...)  Z_ASSERT((e) == NULL, ##__VA_ARGS__)

#define Z_ASSERT_CMP(lhs, op, rhs, ...)                                      \
    ({                                                                       \
        typeof(lhs) _z_assert_cmp_l = (lhs);                                 \
        typeof(rhs) _z_assert_cmp_r = (rhs);                                 \
        z_val_t _z_assert_cmp_lv = _Z_VAL_BUILD(_z_assert_cmp_l);            \
        z_val_t _z_assert_cmp_rv = _Z_VAL_BUILD(_z_assert_cmp_r);            \
        bool _z_res = _z_assert_cmp_l op _z_assert_cmp_r;                    \
                                                                             \
        if (_z_assert_cmp(__FILE__, __LINE__, #op, _z_res, #lhs,             \
                          _z_assert_cmp_lv, #rhs, _z_assert_cmp_rv,          \
                          ""__VA_ARGS__))                                    \
        {                                                                    \
            goto _z_step_end;                                                \
        }                                                                    \
        assert (_z_res); /* avoid false positive in clang-analyzer */        \
    })
#define Z_ASSERT_EQ(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, ==, rhs, ##__VA_ARGS__)
#define Z_ASSERT_NE(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, !=, rhs, ##__VA_ARGS__)
#define Z_ASSERT_LT(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, <,  rhs, ##__VA_ARGS__)
#define Z_ASSERT_LE(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, <=, rhs, ##__VA_ARGS__)
#define Z_ASSERT_GT(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, >,  rhs, ##__VA_ARGS__)
#define Z_ASSERT_GE(lhs, rhs, ...)  Z_ASSERT_CMP(lhs, >=, rhs, ##__VA_ARGS__)
#define Z_ASSERT_ZERO(e, ...)       Z_ASSERT_EQ(e, (typeof(e))0, ##__VA_ARGS__)

#define Z_ASSERT_LSTREQUAL(lhs, rhs, ...) \
    ({ if (_z_assert_lstrequal(__FILE__, __LINE__, #lhs, lhs, #rhs, rhs,  \
                               ""__VA_ARGS__))                            \
        goto _z_step_end; })
#define Z_ASSERT_STREQUAL(lhs, rhs, ...) \
    ({ if (_z_assert_lstrequal(__FILE__, __LINE__, #lhs,                     \
                               LSTR(lhs), #rhs, LSTR(rhs), ""__VA_ARGS__))   \
        goto _z_step_end; })

#define Z_ASSERT_EQUAL(lt, ll, rt, rl, ...) \
    ({  STATIC_ASSERT(__builtin_types_compatible_p(                       \
               typeof(*(lt)) const *, typeof(*(rt)) const *));            \
        if (_z_assert_lstrequal(__FILE__, __LINE__,                       \
               #lt, LSTR_INIT_V((void *)(lt), sizeof((lt)[0]) * (ll)),    \
               #rt, LSTR_INIT_V((void *)(rt), sizeof((rt)[0]) * (rl)),    \
               ""__VA_ARGS__))                                            \
            goto _z_step_end; })

#define Z_ASSERT_IOPEQUAL(st, _lhs, _rhs, ...) \
    ({  const st##__t *lhs = _lhs;                                        \
        const st##__t *rhs = _rhs;                                        \
                                                                          \
        if (_z_assert_iopequal(__FILE__, __LINE__, &st##__s,              \
                               #_lhs, lhs, #_rhs, rhs, ""__VA_ARGS__))    \
        {                                                                 \
            goto _z_step_end;                                             \
        }                                                                 \
    })

#define Z_ASSERT_IOPEQUAL_DESC(st, lhs, rhs, ...) \
    ({  if (_z_assert_iopequal(__FILE__, __LINE__, st,                    \
                               #lhs, lhs, #rhs, rhs, ""__VA_ARGS__))      \
        {                                                                 \
            goto _z_step_end;                                             \
        }                                                                 \
    })

#define Z_ASSERT_IOPJSONEQUAL(st, lhs, rhs, ...) \
    ({  if (_z_assert_iopjsonequal(__FILE__, __LINE__, &st##__s,          \
                               #lhs, lhs, #rhs, rhs, ""__VA_ARGS__))      \
        {                                                                 \
            goto _z_step_end;                                             \
        }                                                                 \
    })

#define Z_ASSERT_IOPJSONEQUAL_DESC(st, lhs, rhs, ...) \
    ({  if (_z_assert_iopjsonequal(__FILE__, __LINE__, st,                \
                               #lhs, lhs, #rhs, rhs, ""__VA_ARGS__))      \
        {                                                                 \
            goto _z_step_end;                                             \
        }                                                                 \
    })

#define Z_ASSERT_OPT_EQ(lhs, rhs, ...)                                    \
    ({  bool _z_res = OPT_EQUAL(lhs, rhs);                                \
        if (_z_assert(__FILE__, __LINE__, #lhs " == " #rhs, _z_res,       \
                      ""__VA_ARGS__))                                     \
        {                                                                 \
            goto _z_step_end;                                             \
        }                                                                 \
        assert (_z_res);                                                  \
    })

/****************************************************************************/
/* Z helpers                                                                */
/****************************************************************************/

void z_skip_start(const char *reason, ...) __attr_printf__(1, 2);
void z_skip_end(void);
void z_todo_start(const char *reason, ...) __attr_printf__(1, 2);
void z_todo_end(void);

int  z_setup(int argc, char **argv);
void z_register_exports(const char *prefix);
void z_register_group(z_cb_f cb);
#ifdef __has_blocks
void z_register_blkgroup(struct z_blkgrp const *);
#endif
int  z_run(void);
bool z_is_list_mode_on(void);

#endif
