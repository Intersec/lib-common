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
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll, Wake, Waker};

struct CMainWaker<F, C>
where
    F: Future + Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    future: Mutex<Pin<Box<F>>>,
    on_done: Mutex<Option<C>>,
}

impl<F, C> Wake for CMainWaker<F, C>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    fn wake(self: Arc<Self>) {
        poll_future(&self);
    }
}

fn poll_future<F, C>(c_main_waker: &Arc<CMainWaker<F, C>>)
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    let waker = Waker::from(Arc::clone(c_main_waker));
    let mut context = Context::from_waker(&waker);

    let poll_res = {
        let mut future = c_main_waker.future.lock().expect("couldn't lock future");
        future.as_mut().poll(&mut context)
    };

    if let Poll::Ready(res) = poll_res {
        let on_done = c_main_waker
            .on_done
            .lock()
            .expect("couldn't lock on_done")
            .take()
            .expect("future resolved but on_done was already consumed");

        thr::main_c_queue_schedule(move || {
            on_done(res);
        });
    }
}

pub fn run_future<F, C>(future: F, on_done: C)
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
    C: FnOnce(F::Output) + Send + 'static,
{
    let c_main_waker = Arc::new(CMainWaker {
        future: Mutex::new(Box::pin(future)),
        on_done: Mutex::new(Some(on_done)),
    });
    poll_future(&c_main_waker);
}
