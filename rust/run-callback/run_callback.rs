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

//! Infrastructure to run functions that need `callback` in `main C event loop`
//!
//! This provides a `run_callback` function. `run_callback` launches a function
//! in the `main C event loop` and then awaits its termination to return the result.
//!
//! # Example
//!
//! ## Use `run_callback` with a `main C event loop` timer
//!
//! ```
//! # use run_callback::run_callback;
//! # use libcommon_core::{
//! #     bindings::{
//! #         data_t, el_blocker_register, el_loop, el_timer_register, el_unregister, ev_t,
//! #         ev_timer_flags_t,
//! #     },
//! #     module::{module_release, module_require},
//! #     thr::main_c_queue_schedule,
//! # };
//! # use tokio_c_mod::tokio_get_module;
//! #
//! # struct ElBlocker(*mut ev_t);
//! # unsafe impl Send for ElBlocker {}
//! # unsafe extern "C" fn on_el_timer_fire(_el: *mut ev_t, data: data_t) {
//! #    let promise_cb = unsafe { *Box::from_raw(data.ptr.cast::<Box<dyn FnOnce(()) + Send>>()) };
//! #    promise_cb(());
//! # }
//! # fn run_timer(promise_cb: Box<dyn FnOnce(()) + Send>) {
//! #    let ptr = Box::into_raw(Box::new(promise_cb)).cast();
//! #    let flag: ev_timer_flags_t = ev_timer_flags_t::EL_TIMER_NOMISS;
//! #    unsafe {
//! #       el_timer_register(10, 0, flag, Some(on_el_timer_fire), ptr);
//! #    }
//! # }
//!
//! module_require(tokio_get_module());
//!
//!        let blocker = ElBlocker(unsafe { el_blocker_register() });
//!
//!        tokio_c_mod::spawn(async move {
//!            run_callback(run_timer).await;
//!
//!            main_c_queue_schedule(move || {
//!                let mut blocker = blocker;
//!
//!                unsafe {
//!                    el_unregister(&raw mut blocker.0);
//!                }
//!            });
//!        });
//!
//!        unsafe {
//!            el_loop();
//!        }
//!
//!        module_release(tokio_get_module());
//! ```

use libcommon_core::thr::main_c_queue_schedule;
use std::{
    pin::Pin,
    sync::{Arc, Mutex},
    task::{Context, Poll, Waker},
};

// {{{ Structure

struct Callback<T>
where
    T: Send + 'static,
{
    result: Option<T>,
    waker: Option<Waker>,
}

struct CallbackFut<T>
where
    T: Send + 'static,
{
    callback: Arc<Mutex<Callback<T>>>,
}

impl<T> Future for CallbackFut<T>
where
    T: Send + 'static,
{
    type Output = T;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut callback = self.callback.lock().expect("couldn't lock");

        if let Some(result) = callback.result.take() {
            return Poll::Ready(result);
        }

        callback.waker = Some(cx.waker().clone());
        Poll::Pending
    }
}

// }}}
// {{{ run_callback

/// Run the input function in `main C event loop` and await its result
///
/// # Arguments
///
/// `run_fun` - a non-async function to run on `main C event loop`
///
/// # Returns
///
/// Return `T` holding the value the completion callback was invoked with.
///
/// # Limitations
///
/// The completion callback is expected to always be invoked. If `run_fun`
/// drops it without invoking it, the future will never resolve. That error
/// case is not supported for now; the caller must guarantee the callback
/// eventually runs.
///
/// # Panics
///
/// Panics if the internal mutex protecting the callback state is poisoned,
/// which only happens if another thread panics while holding the lock.
pub async fn run_callback<F, T>(run_fun: F) -> T
where
    F: FnOnce(Box<dyn FnOnce(T) + Send>) + Send + 'static,
    T: Send + 'static,
{
    let callback = Arc::new(Mutex::new(Callback {
        result: None,
        waker: None,
    }));

    let fun_callback = Arc::clone(&callback);
    let fun = move |res: T| {
        let mut callback = fun_callback.lock().expect("couldn't lock");
        callback.result = Some(res);
        if let Some(waker) = callback.waker.take() {
            waker.wake();
        }
    };
    let fun_box = Box::new(fun);

    main_c_queue_schedule(move || run_fun(fun_box));

    CallbackFut { callback }.await
}

// }}}
// {{{ Test module

#[cfg(test)]
mod tests {
    use crate::run_callback;
    use libcommon_core::{
        bindings::{
            data_t, el_blocker_register, el_loop, el_timer_register, el_unregister, ev_t,
            ev_timer_flags_t,
        },
        module::{module_is_loaded, module_release, module_require},
        thr::main_c_queue_schedule,
    };
    use tokio_c_mod::tokio_get_module;

    struct ElBlocker(*mut ev_t);
    unsafe impl Send for ElBlocker {}

    unsafe extern "C" fn on_el_timer_fire(_el: *mut ev_t, data: data_t) {
        let promise_cb = unsafe { *Box::from_raw(data.ptr.cast::<Box<dyn FnOnce(()) + Send>>()) };
        promise_cb(());
    }

    fn run_timer(promise_cb: Box<dyn FnOnce(()) + Send>) {
        let ptr = Box::into_raw(Box::new(promise_cb)).cast();
        let flag: ev_timer_flags_t = ev_timer_flags_t::EL_TIMER_NOMISS;

        unsafe {
            el_timer_register(10, 0, flag, Some(on_el_timer_fire), ptr);
        }
    }

    #[test]
    fn tokio_c_mod_async_await_with_c_event_loop() {
        module_require(tokio_get_module());
        assert!(module_is_loaded(tokio_get_module()));

        let blocker = ElBlocker(unsafe { el_blocker_register() });

        tokio_c_mod::spawn(async move {
            run_callback(run_timer).await;

            main_c_queue_schedule(move || {
                let mut blocker = blocker;

                unsafe {
                    el_unregister(&raw mut blocker.0);
                }
            });
        });

        unsafe {
            el_loop();
        }

        module_release(tokio_get_module());
        assert!(!module_is_loaded(tokio_get_module()));
    }
}

// }}}
