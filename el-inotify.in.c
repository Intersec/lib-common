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

#include <sys/inotify.h>

static struct {
    qm_t(ev)  watches;     /* el_t's for fs watch events                 */
    el_t      el;          /* inotify el_t                               */
    int       fd;          /* fd for inotify                             */
} inotify_g = {
    .fd      = -1,
    .watches = QM_INIT(ev, inotify_g.watches),
};

static void inotify_shutdown(void)
{
    assert (qm_len(ev, &inotify_g.watches) == 0);

    if (inotify_g.fd == -1) {
        assert (inotify_g.el == NULL);
        return;
    }

    el_fd_unregister(&inotify_g.el);
    inotify_g.fd = -1;
    qm_wipe(ev, &inotify_g.watches);
    qm_init(ev, &inotify_g.watches);
}

static el_data_t el_fs_watch_disable(ev_t **evp, bool unregister)
{
    if (EV_FLAG_HAS(*evp, FSW_ACTIVE)) {
        ev_t *ev = *evp;

        if (unregister) {
            inotify_rm_watch(inotify_g.fd, ev->fs_watch.ctx.u32);
        } else {
            e_notice("watched object `%s` disapeared", ev->fs_watch.path);
        }

        EV_FLAG_RST(ev, FSW_ACTIVE);
        *evp = NULL;
        if (!EV_FLAG_HAS(ev, IS_BLK)) {
            return ev->priv;
        }
    } else {
        /* watcher removed due to file deletion  */
        p_delete(&(*evp)->fs_watch.path);
        return el_destroy(evp);
    }
    return (el_data_t)NULL;
}

static int inotify_cb(el_t el, int fd, short flags, data_t data)
{
    byte buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    assert (el == inotify_g.el);
    assert (fd == inotify_g.fd);

    if (!(flags & POLLIN)) {
        return 0;
    }

    for (;;) {
        byte *next = buf;
        int   ret  = read(fd, buf, sizeof(buf));

        if (ret < 0 && errno == EAGAIN) {
            return 0;
        }
        RETHROW(ret);

        while (ret > 0) {
            struct inotify_event *e = (struct inotify_event *)next;
            int len = ROUND_UP(sizeof(struct inotify_event) + e->len, sizeof(void *));
            int pos;
            el_t wel;
            lstr_t name = LSTR_NULL;

            next += len;
            ret  -= len;
            assert (ret >= 0);

            /* Process event */
            pos = qm_find(ev, &inotify_g.watches, e->wd);
            if (pos < 0) {
                if (e->mask & IN_Q_OVERFLOW) {
                    e_fatal("overflow of the inotify queue. Too many events "
                            "occured in a short amount of time. You should "
                            "consider increasing the value of "
                            "/proc/sys/fs/inotify/max_queued_events");
                } else
                if (!(e->mask & IN_IGNORED)) {
                    e_panic("received inotify event for an unknown watch "
                            "descriptor %d", e->wd);
                }
                continue;
            }

            wel = inotify_g.watches.values[pos];
            assert ((int)wel->fs_watch.ctx.u32 == e->wd);
            if (e->mask & IN_IGNORED) {
                /* watcher deleted */
                qm_del_key(ev, &inotify_g.watches, wel->fs_watch.ctx.u32);
                el_fs_watch_disable(&wel, false);
            } else
            if (EV_FLAG_HAS(wel, FSW_ACTIVE)) {
                if (e->len) {
                    /* XXX We cannot use e->len - 1 as the string length
                     *     because e->name can be padded by several null bytes
                     */
                    name = LSTR(e->name);
                }

                el_fs_watch_fire(wel, e->mask, e->cookie, name);
            }
        }
        if (qm_len(ev, &inotify_g.watches) == 0) {
            inotify_shutdown();
            return 0;
        }
    }
}

static void inotify_initialize(void)
{
    if (unlikely(inotify_g.fd == -1)) {
        assert (inotify_g.el == NULL);

        inotify_g.fd = inotify_init();
        if (inotify_g.fd < 0)
            e_panic(E_UNIXERR("inotify_init"));
        fd_set_features(inotify_g.fd, O_NONBLOCK | O_CLOEXEC);

        inotify_g.el = el_fd_register(inotify_g.fd, true, POLLIN, inotify_cb,
                                      NULL);
    }
}

el_t el_fs_watch_register_d(const char *path, uint32_t flags,
                            el_fs_watch_f *cb, data_t priv)
{
    int pos;
    ev_t *ev;
    int  wd;

    inotify_initialize();
    wd = RETHROW_NP(inotify_add_watch(inotify_g.fd, path, flags));
    pos = qm_put(ev, &inotify_g.watches, wd, NULL, 0);
    if (pos & QHASH_COLLISION) {
        e_panic("you tried to add several watches on %s", path);
    }

    ev = el_create(EV_FS_WATCH, cb, priv, true);
    ev->fs_watch.path = p_strdup(path);
    ev->fs_watch.ctx.u32 = wd;
    EV_FLAG_SET(ev, FSW_ACTIVE);
    inotify_g.watches.keys[pos] = wd;
    inotify_g.watches.values[pos] = ev;
    return ev;
}

data_t el_fs_watch_unregister(ev_t **evp)
{
    if (*evp) {
        CHECK_EV_TYPE(*evp, EV_FS_WATCH);
        return el_fs_watch_disable(evp, true);
    }
    return (data_t)NULL;
}

int el_fs_watch_change(el_t el, uint32_t flags)
{
    int wd;

    CHECK_EV_TYPE(el, EV_FS_WATCH);
    wd = RETHROW(inotify_add_watch(inotify_g.fd, el->fs_watch.path, flags));
    assert (wd == (int)el->fs_watch.ctx.u32);
    return 0;
}

static void el_fs_watch_shutdown(void)
{
    if (qm_len(ev, &inotify_g.watches) == 0) {
        qm_wipe(ev, &inotify_g.watches);
        qm_init(ev, &inotify_g.watches);
    }
}
