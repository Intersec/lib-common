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

//! Turn a C function that reports its result through a callback into a [`Future`].
//!
//! [`run_callback`] runs a function on the main C event loop, and returns a [`CallbackFut`]. The
//! function gets a [`Callback`], and the future resolves to the value that it passes to
//! [`Callback::call`].
//!
//! # Example
//!
//! ## Use `run_callback` with a `main C event loop` timer
//!
//! ```
//! # use libcommon::run_callback::{Callback, run_callback};
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
//! #    let promise_cb = unsafe { *Box::from_raw(data.ptr.cast::<Callback<()>>()) };
//! #    promise_cb.call(());
//! # }
//! # fn run_timer(promise_cb: Callback<()>) {
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

struct Inner<T>
where
    T: Send + 'static,
{
    result: Option<T>,
    waker: Option<Waker>,
}

/// Producer handle used by `run_fun` to deliver the result.
pub struct Callback<T>
where
    T: Send + 'static,
{
    inner: Arc<Mutex<Inner<T>>>,
}

/// Future returned by `run_callback`, resolving to the result.
pub struct CallbackFut<T>
where
    T: Send + 'static,
{
    inner: Arc<Mutex<Inner<T>>>,
}

impl<T> Callback<T>
where
    T: Send + 'static,
{
    /// Deliver the completion result and wake the pending future.
    ///
    /// # Panics
    ///
    /// Panics if the internal mutex protecting the callback state is poisoned,
    /// which only happens if another thread panics while holding the lock.
    pub fn call(self, res: T) {
        // Release the lock before waking: on the main C thread the waker
        // polls the future back synchronously (`main_c_queue_schedule` runs
        // its callbacks inline), and `CallbackFut::poll` takes this same
        // non-reentrant mutex.
        let waker = {
            let mut inner = self.inner.lock().expect("couldn't lock");

            inner.result = Some(res);
            inner.waker.take()
        };

        if let Some(waker) = waker {
            waker.wake();
        }
    }
}

impl<T> Future for CallbackFut<T>
where
    T: Send + 'static,
{
    type Output = T;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut inner = self.inner.lock().expect("couldn't lock");

        if let Some(result) = inner.result.take() {
            return Poll::Ready(result);
        }

        inner.waker = Some(cx.waker().clone());
        Poll::Pending
    }
}

// }}}
// {{{ run_callback

/// Run the input function in `main C event loop` and return a `CallbackFut<T>`
///
/// # Arguments
///
/// `run_fun` - a non-async function to run on `main C event loop`. It receives
/// a `Callback<T>` and must invoke `Callback::call` with the result to resolve
/// the returned future.
///
/// # Returns
///
/// Return `CallbackFut<T>` which resolves to the result value `T` once the
/// completion callback runs.
///
/// # Limitations
///
/// The completion callback is expected to always be invoked. If `run_fun`
/// drops it without invoking it, the future will never resolve. That error
/// case is not supported for now; the caller must guarantee the callback
/// eventually runs.
pub fn run_callback<F, T>(run_fun: F) -> CallbackFut<T>
where
    F: FnOnce(Callback<T>) + Send + 'static,
    T: Send + 'static,
{
    let (task, fut) = callback_pair();

    main_c_queue_schedule(move || run_fun(task));
    fut
}

/// Run the input function right away on the current thread, and return a
/// `CallbackFut<T>`.
///
/// Unlike [`run_callback`], `run_fun` may borrow: it has returned when this
/// function returns, and only the `Callback<T>` it received outlives it.
/// The caller must already be on the thread that `run_fun` needs.
pub fn run_now<F, T>(run_fun: F) -> CallbackFut<T>
where
    F: FnOnce(Callback<T>),
    T: Send + 'static,
{
    let (task, fut) = callback_pair();

    run_fun(task);
    fut
}

/// Creates a `Callback<T>` and the `CallbackFut<T>` that it resolves.
fn callback_pair<T>() -> (Callback<T>, CallbackFut<T>)
where
    T: Send + 'static,
{
    let inner = Arc::new(Mutex::new(Inner {
        result: None,
        waker: None,
    }));
    let task = Callback {
        inner: Arc::clone(&inner),
    };

    (task, CallbackFut { inner })
}

// }}}
// {{{ Test module

#[cfg(test)]
mod tests {
    use super::{Callback, CallbackFut, Inner, run_callback, run_now};
    use libcommon_core::{
        bindings::{
            data_t, el_blocker_register, el_loop, el_timer_register, el_unregister, ev_t,
            ev_timer_flags_t,
        },
        c_event_loop_test,
        module::{module_is_loaded, module_release, module_require},
        thr::main_c_queue_schedule,
    };
    use std::future::Future as _;
    use std::pin::pin;
    use std::sync::{Arc, Mutex};
    use std::task::{Context, Poll, Wake, Waker};
    use tokio_c_mod::tokio_get_module;

    struct ElBlocker(*mut ev_t);
    unsafe impl Send for ElBlocker {}

    unsafe extern "C" fn on_el_timer_fire(_el: *mut ev_t, data: data_t) {
        let promise_cb = unsafe { *Box::from_raw(data.ptr.cast::<Callback<()>>()) };
        promise_cb.call(());
    }

    fn run_timer(promise_cb: Callback<()>) {
        let ptr = Box::into_raw(Box::new(promise_cb)).cast();
        let flag: ev_timer_flags_t = ev_timer_flags_t::EL_TIMER_NOMISS;

        unsafe {
            el_timer_register(10, 0, flag, Some(on_el_timer_fire), ptr);
        }
    }

    #[c_event_loop_test]
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

    /// A waker that takes the lock of the callback, as the main C thread
    /// does: it polls a woken future back at once.
    struct LockingWaker(Arc<Mutex<Inner<()>>>);

    impl Wake for LockingWaker {
        fn wake(self: Arc<Self>) {
            assert!(self.0.try_lock().is_ok(), "the waker runs under the lock");
        }
    }

    #[test]
    fn a_callback_wakes_its_future_without_holding_its_lock() {
        let inner = Arc::new(Mutex::new(Inner {
            result: None,
            waker: None,
        }));
        let mut fut = pin!(CallbackFut {
            inner: Arc::clone(&inner),
        });
        let waker = Waker::from(Arc::new(LockingWaker(Arc::clone(&inner))));
        let mut cx = Context::from_waker(&waker);

        assert!(fut.as_mut().poll(&mut cx).is_pending());
        Callback { inner }.call(());
        assert!(fut.as_mut().poll(&mut cx).is_ready());
    }

    #[test]
    fn run_now_runs_its_function_before_returning() {
        // The function borrows `ran`, which `run_callback` would refuse. A
        // result that it delivers at once is ready at the first poll.
        let mut ran = false;
        let mut fut = pin!(run_now(|cb: Callback<i32>| {
            ran = true;
            cb.call(7);
        }));
        let mut cx = Context::from_waker(Waker::noop());

        assert!(ran);
        assert!(matches!(fut.as_mut().poll(&mut cx), Poll::Ready(7)));
    }
}

// }}}
