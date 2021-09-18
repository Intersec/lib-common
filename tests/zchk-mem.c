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

/*{{{1 Memory Pool Macros */

/* The purpose of these checks is to make sure that:
 *     1. The syntax of these helpers is not broken;
 *     2. They can run at least once without crashing immediately.
 *
 * The purpose of these checks is *not* to fully check any allocator.
 */

typedef struct {
    int foo;
    int tab[];
} z_mp_test_t;

Z_GROUP_EXPORT(mem_pool_macros) {
    Z_TEST(t_pool, "t_pool: helpers macros") {
        t_scope;
        int *p;
        z_mp_test_t *t;
        char *s;

        p = t_new_raw(int, 42);
        p = t_new(int, 42);
        t_realloc0(&p, 42, 64);
        t_realloc_from(&p, 64, 512);

        p = t_new_extra_raw(int, 42);
        p = t_new_extra(int, 16);
        t_realloc0_extra(&p, 16, 21);
        t_realloc_extra_from(&p, 21, 123);

        t = t_new_extra_field_raw(z_mp_test_t, tab, 42);
        t = t_new_extra_field(z_mp_test_t, tab, 56);
        t_realloc0_extra_field(&t, tab, 56, 256);
        t_realloc_extra_field_from(&t, tab, 256, 916);

        p = t_dup(t->tab, 256);
        s = t_dupz("toto", 4);
        IGNORE(t_strdup(s));

        Z_ASSERT(true, "execution OK");
    } Z_TEST_END

    Z_TEST(r_pool, "r_pool: helpers macros") {
        const void *frame;
        int *p;
        z_mp_test_t *t;
        char *s;

        frame = r_newframe();

        p = r_new_raw(int, 42);
        p = r_new(int, 42);
        r_realloc0(&p, 42, 64);
        r_realloc_from(&p, 64, 512);

        p = r_new_extra_raw(int, 42);
        p = r_new_extra(int, 16);
        r_realloc0_extra(&p, 16, 21);
        r_realloc_extra_from(&p, 21, 123);

        t = r_new_extra_field_raw(z_mp_test_t, tab, 42);
        t = r_new_extra_field(z_mp_test_t, tab, 56);
        r_realloc0_extra_field(&t, tab, 56, 256);
        r_realloc_extra_field_from(&t, tab, 256, 916);

        p = r_dup(t->tab, 256);
        s = r_dupz("toto", 4);
        IGNORE(r_strdup(s));

        r_release(frame);

        Z_ASSERT(true, "execution OK");
    } Z_TEST_END

    Z_TEST(mem_libc, "mem_libc pool: helpers macros") {
        int *p;
        z_mp_test_t *t;
        char *s;

        p = p_new_raw(int, 42);
        p_delete(&p);
        p = p_new(int, 42);
        p_realloc0(&p, 42, 512);
        p_realloc(&p, 386);
        p_delete(&p);

        p_delete(&p);
        p = p_new_extra_raw(int, 16);
        p_delete(&p);
        p = p_new_extra(int, 16);
        p_realloc0_extra(&p, 16, 21);
        p_realloc_extra(&p, 21);
        p_delete(&p);

        t = p_new_extra_field_raw(z_mp_test_t, tab, 42);
        p_delete(&t);
        t = p_new_extra_field(z_mp_test_t, tab, 128);
        p_realloc0_extra_field(&t, tab, 128, 256);
        p_realloc_extra_field(&t, tab, 2048);
        p_delete(&t);

        s = p_dup("toto", 5);
        p_delete(&s);
        s = p_dupz("toto", 4);
        p_delete(&s);
        s = p_strdup("toto");
        p_delete(&s);

        Z_ASSERT(true, "execution OK");
    } Z_TEST_END

    Z_TEST(mem_libc_size0, "mem_libc pool: allocation of size 0") {
        int *p;

        p = p_new(int, 0);
        Z_ASSERT_EQ((intptr_t)p, (intptr_t)MEM_EMPTY_ALLOC);
        p_delete(&p);

        p = p_new(int, 42);
        Z_ASSERT_NE((intptr_t)p, (intptr_t)MEM_EMPTY_ALLOC);
        p_realloc(&p, 0);
        Z_ASSERT_EQ((intptr_t)p, (intptr_t)MEM_EMPTY_ALLOC);
        p_delete(&p);
    } Z_TEST_END
} Z_GROUP_END

/*}}}1*/
/*{{{1 FIFO Pool */

Z_GROUP_EXPORT(fifo)
{
    Z_TEST(fifo_pool, "fifo_pool:allocate an amount near pool page size") {
        int page_size = 1 << 19;
        mem_pool_t *pool = mem_fifo_pool_new("fifo.fifo_pool", page_size);
        char vtest[2 * page_size];
        char *v;

        Z_ASSERT(pool);
        p_clear(&vtest, 1);

        v = mp_new(pool, char, page_size - 20);
        Z_ASSERT_ZERO(memcmp(v, vtest, page_size - 20));
        mp_delete(pool, &v);

        v = mp_new(pool, char, page_size);
        Z_ASSERT_ZERO(memcmp(v, vtest, page_size));
        mp_delete(pool, &v);

        v = mp_new(pool, char, page_size + 20);
        Z_ASSERT_ZERO(memcmp(v, vtest, page_size + 20));
        mp_delete(pool, &v);

        mem_fifo_pool_delete(&pool);
    } Z_TEST_END
} Z_GROUP_END

/*1}}}*/
/*{{{1 Memstack */

Z_GROUP_EXPORT(core_mem_stack) {
    Z_TEST(big_alloc_mean, "non regression on #39120") {
        mem_stack_pool_t sp;

        mem_stack_pool_init(&sp, "core_mem_stack.big_alloc_mean", 0);

        mem_stack_pool_push(&sp);

        /* First big allocation to set a big allocation mean */
        Z_ASSERT_P(mp_new_raw(&sp.funcs, char, 50 << 20));
        /* Second big allocation to make the allocator abort */
        Z_ASSERT_P(mp_new_raw(&sp.funcs, char, 50 << 20));

        mem_stack_pool_pop(&sp);
        mem_stack_pool_wipe(&sp);
    } Z_TEST_END

    Z_TEST(new_delete, "test mem_stack_new/mem_stack_delete") {
        lstr_t s;
        mem_pool_t *sp = mem_stack_new("core_mem_stack.new_delete", 0);

        mem_stack_push(sp);
        s = mp_lstr_fmt(sp, "C'qui est embêtant dans les oiseaux "
                        "c'est le bec.");
        Z_ASSERT_P(s.s);
        mem_stack_pop(sp);
        mem_stack_delete(&sp);
    } Z_TEST_END;
} Z_GROUP_END

/*}}}1*/
/*{{{1 Memring */

Z_GROUP_EXPORT(core_mem_ring) {
    Z_TEST(big_alloc_mean, "non regression on #39120") {
        mem_pool_t *rp = mem_ring_new("core_mem_ring.big_alloc_mean", 0);
        const void *rframe = mem_ring_newframe(rp);

        /* First big allocation to set a big allocation mean */
        Z_ASSERT_P(mp_new_raw(rp, char, 50 << 20));
        /* Second big allocation to make the allocator abort */
        Z_ASSERT_P(mp_new_raw(rp, char, 50 << 20));

        mem_ring_release(rframe);
        mem_ring_delete(&rp);
    } Z_TEST_END
} Z_GROUP_END

/*}}}1*/
