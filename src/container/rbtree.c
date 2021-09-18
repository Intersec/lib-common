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

#include <lib-common/container-rbtree.h>

#define rb_parent(n)      ((rb_node_t *)((n)->__parent & ~1))
#define rb_color(n)       (n->__parent & 1)
#define rb_is_red(n)      ({ rb_node_t *__n = (n); __n && !rb_color(__n); })
#define rb_is_black(n)    ({ rb_node_t *__n = (n); !__n || rb_color(__n); })
#define rb_set_red(n)     ((n)->__parent &= ~1)
#define rb_set_black(n)   ((n)->__parent |= 1)
#define rb_set_black2(n)  ({ rb_node_t *__n = (n); if (__n) rb_set_black(__n); })

static void rb_set_parent(rb_node_t *n, rb_node_t *p) {
    n->__parent = (n->__parent & 1) | (uintptr_t)p;
}
static void rb_copy_color(rb_node_t *n, rb_node_t *n2) {
    n->__parent = (n->__parent & ~1) | (n2->__parent & 1);
}

#if 0 /* DEBUG RBTREES */
static void check_rbnode(rb_node_t *p, bool do_colors)
{
    rb_node_t *l = p->left;
    rb_node_t *r = p->right;

    if (rb_is_red(p) && do_colors) {
        assert (rb_is_black(l));
        assert (rb_is_black(r));
    }

    if (l) {
        assert (rb_parent(l) == p);
        check_rbnode(l, do_colors);
    }
    if (r) {
        assert (rb_parent(r) == p);
        check_rbnode(r, do_colors);
    }
}
static void check_rb(rb_node_t **root, bool do_colors)
{
    if (*root) {
        assert (rb_parent(*root) == NULL);
        check_rbnode(*root, do_colors);
    }
}
#else
#  define check_rb(...)
#endif


/*
 *
 *      p                                              p
 *      |                                              |
 *      x        ----[ rotate_left(rb, x) ]--->        y
 *     / \                                            / \
 *    a   y      <---[ rotate_right(rb, y) ]---      x   c
 *       / \                                        / \
 *      b   c                                      a   b
 *
 */

static ALWAYS_INLINE void
rb_reparent(rb_node_t **root, rb_node_t *p, rb_node_t *old, rb_node_t *new)
{
    if (p) {
        if (old == p->left) {
            p->left = new;
        } else {
            p->right = new;
        }
    } else {
        *root = new;
    }
}

static void rb_rotate_left(rb_node_t **root, rb_node_t *x)
{
    rb_node_t *p = rb_parent(x);
    rb_node_t *y = x->right;

    if ((x->right = y->left))
        rb_set_parent(y->left, x);
    y->left = x;
    rb_set_parent(y, p);
    rb_reparent(root, p, x, y);
    rb_set_parent(x, y);
}

static void rb_rotate_right(rb_node_t **root, rb_node_t *y)
{
    rb_node_t *p = rb_parent(y);
    rb_node_t *x = y->left;

    if ((y->left = x->right))
        rb_set_parent(x->right, y);
    x->right = y;
    rb_set_parent(x, p);
    rb_reparent(root, p, y, x);
    rb_set_parent(y, x);
}

static ALWAYS_INLINE void rb_add_fix_color(rb_node_t **root, rb_node_t *z)
{
    rb_node_t *p_z, *y;

    while (rb_is_red(p_z = rb_parent(z))) {
        rb_node_t *gp_z = rb_parent(p_z);

        if (p_z == gp_z->left) {
            if (rb_is_red(y = gp_z->right)) {
                rb_set_black(p_z);
                rb_set_black(y);
                rb_set_red(gp_z);
                z = gp_z;
                continue;
            }

            if (p_z->right == z) {
                rb_rotate_left(root, p_z);
                SWAP(rb_node_t *, z, p_z);
            }

            rb_set_black(p_z);
            rb_set_red(gp_z);
            rb_rotate_right(root, gp_z);
        } else {
            if (rb_is_red(y = gp_z->left)) {
                rb_set_black(y);
                rb_set_black(p_z);
                rb_set_red(gp_z);
                z = gp_z;
                continue;
            }

            if (p_z->left == z) {
                rb_rotate_right(root, p_z);
                SWAP(rb_node_t *, z, p_z);
            }

            rb_set_black(p_z);
            rb_set_red(gp_z);
            rb_rotate_left(root, gp_z);
        }
    }

    rb_set_black(*root);
}

void rb_add_node(rb_node_t **root, rb_node_t *parent, rb_node_t *node)
{
    node->__parent = (uintptr_t)parent;  /* insert it red */
    node->left = node->right = NULL;
    check_rb(root, false);
    rb_add_fix_color(root, node);
    check_rb(root, true);
}


static ALWAYS_INLINE void
rb_del_fix_color(rb_node_t **root, rb_node_t *p, rb_node_t *z)
{
    rb_node_t *w;

    while (rb_is_black(z) && z != *root) {
        if (p->left == z) {
            if (rb_is_red(w = p->right)) {
                rb_set_black(w);
                rb_set_red(p);
                rb_rotate_left(root, p);
                w = p->right;
            }
            assert (w);
            if (rb_is_black(w->left) && rb_is_black(w->right)) {
                rb_set_red(w);
                z = p;
                p = rb_parent(z);
            } else {
                if (rb_is_black(w->right)) {
                    rb_set_black2(w->left);
                    rb_set_red(w);
                    rb_rotate_right(root, w);
                    w = p->right;
                }
                rb_copy_color(w, p);
                rb_set_black(p);
                rb_set_black2(w->right);
                rb_rotate_left(root, p);
                z = *root;
                break;
            }
        } else {
            if (rb_is_red(w = p->left)) {
                rb_set_black(w);
                rb_set_red(p);
                rb_rotate_right(root, p);
                w = p->left;
            }
            assert (w);
            if (rb_is_black(w->left) && rb_is_black(w->right)) {
                rb_set_red(w);
                z = p;
                p = rb_parent(z);
            } else {
                if (rb_is_black(w->left)) {
                    rb_set_black2(w->right);
                    rb_set_red(w);
                    rb_rotate_left(root, w);
                    w = p->left;
                }
                rb_copy_color(w, p);
                rb_set_black(p);
                rb_set_black2(w->left);
                rb_rotate_right(root, p);
                z = *root;
                break;
            }
        }
    }
    rb_set_black2(z);
}

void rb_del_node(rb_node_t **root, rb_node_t *z)
{
    struct rb_node_t *child, *p;
    int color;

    if (z->left && z->right) {
        rb_node_t *old = z;

        z     = __rb_next(z);
        child = z->right;
        p     = rb_parent(z);
        color = rb_color(z);

        if (child)
            rb_set_parent(child, p);
        if (p == old) {
            p->right = child;
            p = z;
        } else {
            p->left = child;
        }
        *z = *old;

        rb_reparent(root, rb_parent(old), old, z);
        rb_set_parent(old->left, z);
        if (old->right)
            rb_set_parent(old->right, z);
    } else {
        child = z->right ?: z->left;
        p     = rb_parent(z);
        color = rb_color(z);
        if (child)
            rb_set_parent(child, p);
        rb_reparent(root, p, z, child);
    }
    check_rb(root, false);

    if (color) /* it's black */
        rb_del_fix_color(root, p, child);
    check_rb(root, true);
}


rb_node_t *__rb_next(rb_node_t *n)
{
    rb_node_t *p;

    if (n->right) {
        n = n->right;
        while (n->left)
            n = n->left;
        return n;
    }

    while ((p = rb_parent(n)) != NULL && n == p->right)
        n = p;
    return p;
}
rb_node_t *__rb_prev(rb_node_t *n)
{
    rb_node_t *p;

    if (n->left) {
        n = n->left;
        while (n->right)
            n = n->right;
        return n;
    }

    while ((p = rb_parent(n)) != NULL && n == p->left)
        n = p;
    return p;
}
