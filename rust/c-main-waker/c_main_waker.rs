/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

use libcommon_core::thr;
use std::cell::UnsafeCell;
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::sync::atomic::{AtomicU8, Ordering};
use std::task::{Context, Poll, Wake, Waker};

const IDLE: u8 = 0;
const POLLING: u8 = 1;
const NOTIFIED: u8 = 2;
const DONE: u8 = 3;

// {{{ Struct basic implementation

struct Task<F, C> {
    future: Pin<Box<F>>,
    on_done: C,
}

struct CMainWaker<F, C> {
    task: UnsafeCell<Option<Task<F, C>>>,
    state: AtomicU8,
}

unsafe impl<F, C> Sync for CMainWaker<F, C>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
}

// }}}
// {{{ Waker

impl<F, C> Wake for CMainWaker<F, C>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    fn wake(self: Arc<Self>) {
        self.wake_by_ref();
    }

    fn wake_by_ref(self: &Arc<Self>) {
        loop {
            match self
                .state
                .compare_exchange(IDLE, POLLING, Ordering::AcqRel, Ordering::Acquire)
            {
                // The task was Idle: we now own the poll, so schedule it on
                // the main C thread.
                Ok(_) => {
                    let c_main_waker = Arc::clone(self);
                    thr::main_c_queue_schedule(move || c_main_waker.poll_future());
                    return;
                }
                // A poll is already in progress: flag it so it polls again
                // once done (Polling -> Notified). If the poll finished in the
                // meantime (Polling -> Idle), loop to schedule it ourselves.
                Err(POLLING) => {
                    if let Err(IDLE) = self.state.compare_exchange(
                        POLLING,
                        NOTIFIED,
                        Ordering::AcqRel,
                        Ordering::Acquire,
                    ) {
                        continue;
                    }
                    return;
                }
                // Already Notified (a re-poll is pending) or Done: nothing to
                // do.
                Err(_) => return,
            }
        }
    }
}

// }}}
// {{{ Poll

impl<F, C> CMainWaker<F, C>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    fn poll_future(self: Arc<Self>) {
        loop {
            // SAFETY: `task` is only ever accessed here in poll_future, and the
            // atomic state machine guarantees at most one poll_future runs at a
            // time, so this exclusive borrow is unique.
            let slot = unsafe { &mut *self.task.get() };

            // The task was already taken (the future has completed): mark the
            // state Done and stop.
            let Some(mut task) = slot.take() else {
                self.state.store(DONE, Ordering::Release);
                return;
            };

            let waker = Waker::from(Arc::clone(&self));
            let mut context = Context::from_waker(&waker);

            // The future completed: mark Done, run the completion callback and
            // stop.
            if let Poll::Ready(output) = task.future.as_mut().poll(&mut context) {
                self.state.store(DONE, Ordering::Release);
                let on_done = task.on_done;
                on_done(output);
                return;
            }

            // Still pending: keep the task for the next poll.
            *slot = Some(task);

            // Go back to Idle and wait for the next wake. If a wake arrived
            // while we were polling, the state is Notified instead: switch back
            // to Polling and poll again right away.
            match self
                .state
                .compare_exchange(POLLING, IDLE, Ordering::AcqRel, Ordering::Acquire)
            {
                Err(NOTIFIED) => {
                    self.state.store(POLLING, Ordering::Release);
                }
                Ok(_) | Err(_) => return,
            }
        }
    }
}

// }}}
// {{{ Runner

pub fn run_future<F, C>(future: F, on_done: C)
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    let c_main_waker = Arc::new(CMainWaker {
        task: UnsafeCell::new(Some(Task {
            future: Box::pin(future),
            on_done,
        })),
        state: AtomicU8::new(POLLING),
    });

    thr::main_c_queue_schedule(move || c_main_waker.poll_future());
}

// }}}
