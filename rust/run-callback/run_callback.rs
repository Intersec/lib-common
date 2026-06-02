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

use libcommon_core::thr::main_c_queue_schedule;
use tokio::sync::oneshot::{self, error::RecvError};

// {{{ run_callback

#[allow(clippy::missing_errors_doc)]
// will add doc in a dedicated commit
pub async fn run_callback<F, T>(run_fun: F) -> Result<T, RecvError>
where
    F: FnOnce(Box<dyn FnOnce(T) + Send>) + Send + 'static,
    T: Send + 'static,
{
    let (send, recv) = oneshot::channel::<T>();
    let fun = move |res: T| {
        let _unused: Result<(), T> = send.send(res);
    };
    let fun_box = Box::new(fun);

    main_c_queue_schedule(move || run_fun(fun_box));

    recv.await
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
            run_callback(run_timer).await.expect("nested task panicked");

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
