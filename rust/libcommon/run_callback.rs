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
    sync::{Arc, Mutex, MutexGuard, PoisonError},
    task::{Context, Poll, Waker},
};

// {{{ Structure

struct Inner<T>
where
    T: Send + 'static,
{
    result: Option<T>,
    waker: Option<Waker>,
    /// The completion callback has run: the operation is over.
    completed: bool,
    /// The future was dropped before the operation completed.
    abandoned: bool,
    /// Cancels the operation, if the C API can.
    cancel: Option<Box<dyn FnOnce() + Send>>,
}

impl<T> Inner<T>
where
    T: Send + 'static,
{
    fn new() -> Self {
        Self {
            result: None,
            waker: None,
            completed: false,
            abandoned: false,
            cancel: None,
        }
    }
}

/// Locks `inner`. A panic under the lock leaves the state consistent: each
/// critical section only writes plain fields.
fn lock<T: Send + 'static>(inner: &Mutex<Inner<T>>) -> MutexGuard<'_, Inner<T>> {
    inner.lock().unwrap_or_else(PoisonError::into_inner)
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
    /// If the future is gone, the result is dropped here, on the thread of
    /// the completion callback.
    pub fn call(self, res: T) {
        // Release the lock before waking: on the main C thread the waker
        // polls the future back synchronously (`main_c_queue_schedule` runs
        // its callbacks inline), and `CallbackFut::poll` takes this same
        // non-reentrant mutex.
        let mut inner = lock(&self.inner);

        inner.completed = true;
        // Dropped without the lock too: what the hook captures may take it.
        let cancel = inner.cancel.take();
        let waker = if inner.abandoned {
            None
        } else {
            inner.result = Some(res);
            inner.waker.take()
        };

        drop(inner);
        drop(cancel);
        if let Some(waker) = waker {
            waker.wake();
        }
    }

    /// Sets how to cancel the operation, for a future dropped before the
    /// operation completes. Without it, the operation runs to its end, and
    /// its result is dropped.
    ///
    /// `cancel` runs on the main C thread, only while the operation has not
    /// completed. It may complete the operation itself: it runs without the
    /// lock of the callback.
    ///
    /// Returns `false`, and drops `cancel`, if the future is already gone:
    /// the operation has nobody to report to, so it should not start.
    #[must_use = "an operation whose future is gone should not start"]
    pub fn on_cancel<F>(&self, cancel: F) -> bool
    where
        F: FnOnce() + Send + 'static,
    {
        let mut inner = lock(&self.inner);

        if inner.abandoned {
            return false;
        }
        inner.cancel = Some(Box::new(cancel));
        true
    }

    /// Whether the future was dropped: the operation has nobody to report
    /// to, so it may as well not start.
    fn is_abandoned(&self) -> bool {
        lock(&self.inner).abandoned
    }
}

impl<T> Future for CallbackFut<T>
where
    T: Send + 'static,
{
    type Output = T;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut inner = lock(&self.inner);

        if let Some(result) = inner.result.take() {
            return Poll::Ready(result);
        }

        inner.waker = Some(cx.waker().clone());
        Poll::Pending
    }
}

impl<T> Drop for CallbackFut<T>
where
    T: Send + 'static,
{
    /// Cancels the operation if it has not completed, as
    /// `tokio::time::timeout()` expects of a future that it drops.
    fn drop(&mut self) {
        let cancel = {
            let mut inner = lock(&self.inner);

            if inner.completed {
                return;
            }
            inner.abandoned = true;
            inner.cancel.take()
        };
        let Some(cancel) = cancel else {
            return;
        };
        let inner = Arc::clone(&self.inner);

        main_c_queue_schedule(move || {
            // On another thread, the drop raced with the completion, which
            // may have released what `cancel` uses.
            let completed = lock(&inner).completed;

            if !completed {
                cancel();
            }
        });
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
/// # Cancellation
///
/// Dropping the future before the result comes runs the hook that `run_fun`
/// gave to [`Callback::on_cancel`], if any. Without a hook, the operation
/// runs to its end, and its result is dropped. A future dropped before
/// `run_fun` runs skips `run_fun`.
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

    main_c_queue_schedule(move || {
        if !task.is_abandoned() {
            run_fun(task);
        }
    });
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
    let inner = Arc::new(Mutex::new(Inner::new()));
    let task = Callback {
        inner: Arc::clone(&inner),
    };

    (task, CallbackFut { inner })
}

// }}}
// {{{ Test module

#[cfg(test)]
mod tests {
    use super::{Callback, CallbackFut, Inner, callback_pair, run_callback, run_now};
    use libcommon_core::{
        bindings::{
            data_t, el_blocker_register, el_loop, el_timer_register, el_unregister, ev_t,
            ev_timer_flags_t, thr_get_module, thr_queue_main_drain,
        },
        c_event_loop_test,
        module::{module_is_loaded, module_release, module_require},
        thr::{is_main_c_thread, main_c_queue_schedule},
    };
    use std::future::Future as _;
    use std::pin::pin;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    use std::sync::{Arc, Mutex};
    use std::task::{Context, Poll, Wake, Waker};
    use std::thread;
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
        let inner = Arc::new(Mutex::new(Inner::new()));
        let mut fut = pin!(CallbackFut {
            inner: Arc::clone(&inner),
        });
        let waker = Waker::from(Arc::new(LockingWaker(Arc::clone(&inner))));
        let mut cx = Context::from_waker(&waker);

        assert!(fut.as_mut().poll(&mut cx).is_pending());
        Callback { inner }.call(());
        assert!(fut.as_mut().poll(&mut cx).is_ready());
    }

    /// A capture of a cancel hook that takes the lock of the callback when
    /// it drops, as a capture holding the future of the pair would.
    struct LockingCapture(Arc<Mutex<Inner<()>>>);

    impl Drop for LockingCapture {
        fn drop(&mut self) {
            assert!(self.0.try_lock().is_ok(), "the hook drops under the lock");
        }
    }

    #[test]
    fn a_callback_drops_its_cancel_hook_without_holding_its_lock() {
        let inner = Arc::new(Mutex::new(Inner::new()));
        let cb = Callback {
            inner: Arc::clone(&inner),
        };
        let capture = LockingCapture(Arc::clone(&inner));

        assert!(cb.on_cancel(move || drop(capture)));
        cb.call(());
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

    // {{{ Cancellation

    /// Counts the cancels of an operation, and where they ran.
    #[derive(Default)]
    struct Cancels {
        count: AtomicUsize,
        off_main_c_thread: AtomicBool,
    }

    /// The completion side of an operation, until someone completes it.
    type PendingCallback = Arc<Mutex<Option<Callback<i32>>>>;

    /// Starts an operation that its cancel completes with `-1`, as
    /// `ic_msg_cancel()` completes a query with `IC_MSG_CANCELED`.
    fn cancellable(cancels: &Arc<Cancels>) -> (PendingCallback, CallbackFut<i32>) {
        let (cb, fut) = callback_pair();
        let cb = Arc::new(Mutex::new(Some(cb)));
        let (cb_c, cancels) = (Arc::clone(&cb), Arc::clone(cancels));

        let set = cb
            .lock()
            .expect("poisoned")
            .as_ref()
            .expect("taken")
            .on_cancel(move || {
                cancels.count.fetch_add(1, Ordering::Relaxed);
                if !is_main_c_thread() {
                    cancels.off_main_c_thread.store(true, Ordering::Relaxed);
                }
                if let Some(cb) = cb_c.lock().expect("poisoned").take() {
                    cb.call(-1);
                }
            });

        assert!(set);
        (cb, fut)
    }

    #[c_event_loop_test]
    fn dropping_a_pending_future_cancels_its_operation() {
        module_require(unsafe { thr_get_module() });
        let cancels = Arc::default();
        let (cb, fut) = cancellable(&cancels);

        drop(fut);
        unsafe {
            thr_queue_main_drain();
        }

        assert_eq!(cancels.count.load(Ordering::Relaxed), 1);
        assert!(
            cb.lock().expect("poisoned").is_none(),
            "the cancel completed it"
        );
        module_release(unsafe { thr_get_module() });
    }

    #[c_event_loop_test]
    fn dropping_a_completed_future_cancels_nothing() {
        module_require(unsafe { thr_get_module() });
        let cancels = Arc::default();
        let (cb, fut) = cancellable(&cancels);

        cb.lock().expect("poisoned").take().expect("taken").call(1);
        drop(fut);
        unsafe {
            thr_queue_main_drain();
        }

        assert_eq!(cancels.count.load(Ordering::Relaxed), 0);
        module_release(unsafe { thr_get_module() });
    }

    /// The cancel of a future dropped on another thread runs on the main C
    /// thread, the only one where a C operation can be cancelled.
    #[c_event_loop_test]
    fn a_future_dropped_on_another_thread_cancels_on_the_main_c_thread() {
        module_require(unsafe { thr_get_module() });
        let cancels = Arc::<Cancels>::default();
        let (_cb, fut) = cancellable(&cancels);

        thread::spawn(move || drop(fut)).join().expect("panicked");
        unsafe {
            thr_queue_main_drain();
        }

        assert_eq!(cancels.count.load(Ordering::Relaxed), 1);
        assert!(!cancels.off_main_c_thread.load(Ordering::Relaxed));
        module_release(unsafe { thr_get_module() });
    }

    /// Counts its drops.
    struct DropCount(Arc<AtomicUsize>);

    impl Drop for DropCount {
        fn drop(&mut self) {
            self.0.fetch_add(1, Ordering::Relaxed);
        }
    }

    /// A drop on another thread that races with the completion: the cancel
    /// never runs on a completed operation, and the result drops once.
    #[c_event_loop_test]
    fn a_drop_racing_with_the_completion_cancels_nothing() {
        const ROUNDS: usize = 1000;

        module_require(unsafe { thr_get_module() });
        let cancels = Arc::new(AtomicUsize::new(0));
        let drops = Arc::new(AtomicUsize::new(0));

        for _ in 0..ROUNDS {
            let (cb, fut) = callback_pair::<DropCount>();
            let cancels = Arc::clone(&cancels);

            assert!(cb.on_cancel(move || {
                cancels.fetch_add(1, Ordering::Relaxed);
            }));

            let dropper = thread::spawn(move || drop(fut));

            cb.call(DropCount(Arc::clone(&drops)));
            dropper.join().expect("panicked");
            unsafe {
                thr_queue_main_drain();
            }
        }

        assert_eq!(cancels.load(Ordering::Relaxed), 0);
        assert_eq!(drops.load(Ordering::Relaxed), ROUNDS);
        module_release(unsafe { thr_get_module() });
    }

    /// A future dropped before its hook is set: the operation learns that
    /// nobody waits for it, and the hook never runs.
    #[test]
    fn a_hook_set_after_the_drop_is_refused() {
        let (cb, fut) = callback_pair::<()>();

        drop(fut);
        assert!(!cb.on_cancel(|| panic!("the operation never started")));
    }

    #[c_event_loop_test]
    fn run_callback_skips_an_operation_whose_future_is_gone() {
        module_require(unsafe { thr_get_module() });
        let ran = Arc::new(AtomicBool::new(false));
        let ran_c = Arc::clone(&ran);

        thread::spawn(move || {
            drop(run_callback(move |_cb: Callback<()>| {
                ran_c.store(true, Ordering::Relaxed);
            }));
        })
        .join()
        .expect("panicked");
        unsafe {
            thr_queue_main_drain();
        }

        assert!(!ran.load(Ordering::Relaxed));
        module_release(unsafe { thr_get_module() });
    }

    // }}}
}

// }}}
