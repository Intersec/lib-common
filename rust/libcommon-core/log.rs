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
#![allow(clippy::module_name_repetitions)]

//! Rust bindings for the C logging library.
//!
//! This module provides a safe Rust interface to the C `logger_t` logging facility.
//! It allows creating loggers, checking log levels, and emitting log messages.
//!
//! # Key Features
//!
//! - **Lazy evaluation**: Log arguments are not evaluated if the logger's level is too low.
//! - **Hierarchical loggers**: Loggers can inherit their level from a parent logger.
//! - **Multiple log levels**: Supports fatal, panic, error, warning, notice, info, debug, and trace.
//! - **Static loggers**: Loggers can be declared as global static variables.
//!
//! # Example
//!
//! ```
//! use libcommon_core::log::{Logger, LogLevel, LogFlags};
//! use libcommon_core::{logger_info, logger_error};
//!
//! // Dynamic logger
//! let logger = Logger::new_parent("my_logger", LogLevel::Info, LogFlags::empty());
//! logger_info!(logger, "starting application");
//!
//! // Static logger
//! static APP_LOGGER: Logger = Logger::new_static_parent("app", LogLevel::Info, LogFlags::empty());
//! logger_info!(APP_LOGGER, "static logger message");
//! ```

use std::cell::UnsafeCell;
use std::marker::PhantomData;
use std::os::raw::{c_char, c_int};
use std::ptr;

use crate::bindings::{
    __logger_fatal, __logger_log, __logger_panic, dlist_t, logger_delete, logger_get_root,
    logger_has_level, logger_new, logger_t,
};
pub use crate::bindings::{LOG_CRIT, LOG_DEBUG, LOG_ERR, LOG_INFO, LOG_NOTICE, LOG_WARNING};
use crate::lstr::{self, AsRaw as _};

// {{{ Log levels

/// Minimum level for tracing (`LOG_DEBUG` + 1).
pub const LOG_TRACE: i32 = LOG_DEBUG as i32 + 1;

/// Log level indicating inheritance from the parent logger.
pub const LOG_INHERITS: i32 = -1;

/// Internal value indicating no level has been defined.
pub const LOG_UNDEFINED: i32 = -3;

/// Log level type alias for clarity.
#[repr(i32)]
pub enum LogLevel {
    /// Critical level (fatal/panic).
    Crit = LOG_CRIT as i32,
    /// Error level.
    Error = LOG_ERR as i32,
    /// Warning level.
    Warning = LOG_WARNING as i32,
    /// Notice level.
    Notice = LOG_NOTICE as i32,
    /// Info level.
    Info = LOG_INFO as i32,
    /// Debug level.
    Debug = LOG_DEBUG as i32,
    /// Trace level (base).
    Trace = LOG_TRACE,
    /// Inherit level from parent.
    Inherits = LOG_INHERITS,
}

impl LogLevel {
    /// Convert the log level to its raw i32 value.
    #[must_use]
    pub const fn as_raw(self) -> i32 {
        self as i32
    }

    /// Create a trace level with a specific depth.
    ///
    /// Trace levels are `LOG_TRACE + depth` where depth >= 0.
    #[must_use]
    pub const fn trace_with_depth(depth: i32) -> i32 {
        LOG_TRACE + depth
    }
}

// }}}
// {{{ Log flags

/// Flags for logger configuration.
#[derive(Clone, Copy)]
pub struct LogFlags(u32);

impl LogFlags {
    /// No flags set.
    pub const fn empty() -> Self {
        Self(0)
    }

    /// Force level to be set recursively to children.
    pub const RECURSIVE: Self = Self(1 << 0);

    /// Level has been recursively forced (internal use).
    pub const FORCED: Self = Self(1 << 1);

    /// Log handler is called, but default handler does nothing.
    pub const SILENT: Self = Self(1 << 2);

    /// Get the raw bits of the flags.
    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    /// Check if flags contain the given flag.
    #[must_use]
    pub const fn contains(self, other: Self) -> bool {
        (self.0 & other.0) == other.0
    }

    /// Combine two flags.
    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }
}

// }}}
// {{{ Logger

/// Internal storage for a logger.
///
/// This enum allows `Logger` to handle three different ownership models:
/// - `Owned`: Logger allocated by C library, will be deleted on drop
/// - `Borrowed`: Pointer to externally-owned logger (e.g., root logger)
/// - `Static`: Logger data embedded directly, for static/const initialization
enum InnerLogger {
    /// Owned pointer to a logger allocated by the C library.
    Owned(*mut logger_t),
    /// Borrowed pointer to an externally-owned logger.
    Borrowed(*mut logger_t),
    /// Static logger with embedded storage.
    Static(UnsafeCell<logger_t>),
}

/// A safe wrapper around the C `logger_t` structure.
///
/// The `Logger` struct provides a safe interface to the C logging facility.
/// It supports three modes of operation:
///
/// - **Owned**: Created via `Logger::new()`, the logger is allocated by the C library
///   and will be deleted when the `Logger` is dropped.
/// - **Borrowed**: Created via `Logger::from_ptr()` or `Logger::root()`, the logger
///   points to externally-owned memory and won't be deleted on drop.
/// - **Static**: Created via `Logger::new_static_parent()`, the logger data is embedded
///   directly and can be used in `static` declarations.
///
/// # Example
///
/// ```
/// use libcommon_core::log::{Logger, LogLevel, LogFlags};
///
/// // Dynamic logger (owned)
/// let logger = Logger::new_parent("dynamic", LogLevel::Info, LogFlags::empty());
///
/// // Static logger (for global variables)
/// static LOGGER: Logger = Logger::new_static_parent("static", LogLevel::Info, LogFlags::empty());
/// ```
pub struct Logger<'a> {
    inner: InnerLogger,
    _marker: PhantomData<&'a logger_t>,
}

impl Logger<'_> {
    /// Get a raw pointer to the underlying `logger_t`.
    ///
    /// This is useful for interacting with C code that expects a `logger_t*`.
    #[must_use]
    pub const fn as_ptr(&self) -> *mut logger_t {
        match &self.inner {
            InnerLogger::Owned(ptr) | InnerLogger::Borrowed(ptr) => *ptr,
            InnerLogger::Static(cell) => cell.get(),
        }
    }

    /// Check if the logger has at least the given level enabled.
    #[must_use]
    pub fn has_level(&self, level: i32) -> bool {
        unsafe { logger_has_level(self.as_ptr(), level) }
    }

    /// Check if error level is enabled.
    #[must_use]
    pub fn has_error(&self) -> bool {
        self.has_level(LOG_ERR as i32)
    }

    /// Check if warning level is enabled.
    #[must_use]
    pub fn has_warning(&self) -> bool {
        self.has_level(LOG_WARNING as i32)
    }

    /// Check if notice level is enabled.
    #[must_use]
    pub fn has_notice(&self) -> bool {
        self.has_level(LOG_NOTICE as i32)
    }

    /// Check if info level is enabled.
    #[must_use]
    pub fn has_info(&self) -> bool {
        self.has_level(LOG_INFO as i32)
    }

    /// Check if debug level is enabled.
    #[must_use]
    pub fn has_debug(&self) -> bool {
        self.has_level(LOG_DEBUG as i32)
    }

    /// Check if trace level (at given depth) is enabled.
    #[must_use]
    pub fn has_trace(&self, depth: i32) -> bool {
        self.has_level(LOG_TRACE + depth)
    }

    /// Log a message at the given level.
    ///
    /// This is a low-level function that does NOT check the level is
    /// activated on the logger. Prefer using the logging macros instead.
    ///
    /// # Arguments
    ///
    /// * `level` - The log level.
    /// * `file` - Source file name.
    /// * `func` - Function name.
    /// * `line` - Line number.
    /// * `msg` - The message to log.
    pub fn log(&self, level: i32, file: &'static str, func: &'static str, line: i32, msg: &str) {
        unsafe {
            __logger_log(
                self.as_ptr(),
                level,
                lstr::null(),
                -1,
                lstr::raw(file),
                lstr::raw(func),
                line,
                c"%*pM".as_ptr(),
                msg.len() as c_int,
                msg.as_ptr(),
            );
        }
    }

    /// Log a fatal message and exit.
    ///
    /// This function never returns.
    pub fn fatal(&self, file: &'static str, func: &'static str, line: i32, msg: &str) -> ! {
        unsafe {
            __logger_fatal(
                self.as_ptr(),
                lstr::raw(file),
                lstr::raw(func),
                line,
                c"%*pM".as_ptr(),
                msg.len() as c_int,
                msg.as_ptr(),
            );
        }
    }

    /// Log a panic message and abort.
    ///
    /// This function never returns.
    pub fn panic(&self, file: &'static str, func: &'static str, line: i32, msg: &str) -> ! {
        unsafe {
            __logger_panic(
                self.as_ptr(),
                lstr::raw(file),
                lstr::raw(func),
                line,
                c"%*pM".as_ptr(),
                msg.len() as c_int,
                msg.as_ptr(),
            );
        }
    }
}

// {{{ Dynamic logger constructors

impl Logger<'_> {
    /// Create a new owned logger with a parent.
    ///
    /// This function allocates a new logger using `logger_new`.
    ///
    /// # Arguments
    ///
    /// * `parent` - Pointer to the parent C logger, or `None` to use the root logger.
    ///   You can pass the result of `Logger::as_ptr()` to use a Rust logger as parent,
    ///   or directly pass a pointer to a C `logger_t`. If `parent` is `Some`, the pointer
    ///   must be valid for the duration of this call. The parent logger must outlive the
    ///   created logger.
    /// * `name` - The name of the logger.
    /// * `default_level` - The default log level for this logger.
    /// * `flags` - Logger flags.
    #[must_use]
    pub fn new(
        parent: Option<*mut logger_t>,
        name: &str,
        default_level: LogLevel,
        flags: LogFlags,
    ) -> Self {
        let name_lstr = lstr::from_str(name);
        let parent_ptr = parent.unwrap_or(ptr::null_mut());

        let inner = unsafe {
            logger_new(
                parent_ptr,
                name_lstr.as_raw(),
                default_level.as_raw(),
                flags.bits(),
            )
        };

        Self {
            inner: InnerLogger::Owned(inner),
            _marker: PhantomData,
        }
    }

    /// Create a new owned logger with the root logger as parent.
    ///
    /// This is a convenience function equivalent to `Logger::new(None, name, level, flags)`.
    #[must_use]
    pub fn new_parent(name: &str, default_level: LogLevel, flags: LogFlags) -> Self {
        Self::new(None, name, default_level, flags)
    }

    /// Create a logger that inherits its level from its parent.
    #[must_use]
    pub fn new_inherits(parent: Option<*mut logger_t>, name: &str, flags: LogFlags) -> Self {
        Self::new(parent, name, LogLevel::Inherits, flags)
    }

    /// Get the root logger.
    ///
    /// Returns a borrowed reference to the global root logger.
    #[must_use]
    pub fn root() -> Logger<'static> {
        Logger {
            inner: InnerLogger::Borrowed(unsafe { logger_get_root() }),
            _marker: PhantomData,
        }
    }

    /// Create a borrowed `Logger` from a raw `logger_t` pointer.
    ///
    /// # Safety
    ///
    /// The pointer must be valid and the logger must outlive the returned `Logger`.
    #[must_use]
    pub unsafe fn from_ptr(ptr: *mut logger_t) -> Logger<'static> {
        Logger {
            inner: InnerLogger::Borrowed(ptr),
            _marker: PhantomData,
        }
    }
}

// }}}
// {{{ Static logger constructors

/// Helper to create an initialized `logger_t` for static loggers.
const fn make_static_c_logger(
    name: &'static str,
    default_level: i32,
    flags: u32,
    parent: *mut logger_t,
) -> logger_t {
    logger_t {
        conf_gen: 0,
        is_static: true,
        level: LOG_UNDEFINED,
        defined_level: LOG_UNDEFINED,
        default_level,
        level_flags: flags,
        default_level_flags: flags,
        name: lstr::from_ptr_and_len(name.as_ptr().cast::<c_char>(), name.len()),
        full_name: lstr::null(),
        parent,
        children: dlist_t {
            next: ptr::null_mut(),
            prev: ptr::null_mut(),
        },
        siblings: dlist_t {
            next: ptr::null_mut(),
            prev: ptr::null_mut(),
        },
    }
}

impl Logger<'static> {
    /// Create a new static "parent" logger with the given name, level, and flags.
    ///
    /// This is a const constructor that can be used to initialize static loggers
    /// as global variables. The logger will use the root logger as its parent.
    ///
    /// # Arguments
    ///
    /// * `name` - The name of the logger.
    /// * `default_level` - The default log level.
    /// * `flags` - Logger flags.
    ///
    /// # Example
    ///
    /// ```
    /// use libcommon_core::log::{Logger, LogLevel, LogFlags};
    ///
    /// static MY_LOGGER: Logger = Logger::new_static_parent(
    ///     "my_module",
    ///     LogLevel::Info,
    ///     LogFlags::empty(),
    /// );
    /// ```
    #[must_use]
    pub const fn new_static_parent(
        name: &'static str,
        default_level: LogLevel,
        flags: LogFlags,
    ) -> Self {
        Self {
            inner: InnerLogger::Static(UnsafeCell::new(make_static_c_logger(
                name,
                default_level.as_raw(),
                flags.bits(),
                ptr::null_mut(),
            ))),
            _marker: PhantomData,
        }
    }

    /// Create a new static logger with a parent.
    ///
    /// # Arguments
    ///
    /// * `parent` - Pointer to the parent `logger_t`. This can be:
    ///   - A pointer to a C static logger
    ///   - The result of `Logger::as_ptr()` on a Rust logger
    ///
    ///   The parent must outlive this logger.
    /// * `name` - The name of the logger.
    /// * `default_level` - The default log level.
    /// * `flags` - Logger flags.
    ///
    /// # Example
    ///
    /// ```
    /// use libcommon_core::log::{Logger, LogLevel, LogFlags};
    ///
    /// static PARENT: Logger = Logger::new_static_parent("app", LogLevel::Info, LogFlags::empty());
    /// static CHILD: Logger = Logger::new_static(
    ///     PARENT.as_ptr(),
    ///     "db",
    ///     LogLevel::Inherits,
    ///     LogFlags::empty(),
    /// );
    /// ```
    #[must_use]
    pub const fn new_static(
        parent: *mut logger_t,
        name: &'static str,
        default_level: LogLevel,
        flags: LogFlags,
    ) -> Self {
        Self {
            inner: InnerLogger::Static(UnsafeCell::new(make_static_c_logger(
                name,
                default_level.as_raw(),
                flags.bits(),
                parent,
            ))),
            _marker: PhantomData,
        }
    }
}

// }}}

impl Drop for Logger<'_> {
    fn drop(&mut self) {
        if let InnerLogger::Owned(ptr) = &mut self.inner {
            unsafe {
                logger_delete(ptr);
            }
        }
    }
}

// Safety: Logger is Send because the underlying C logger is thread-safe.
unsafe impl Send for Logger<'_> {}

// Safety: Logger is Sync because logging operations use internal synchronization.
unsafe impl Sync for Logger<'_> {}

// }}}
// {{{ Logging macros

/// Internal macro to emit a log message. Used by the public logging macros.
#[doc(hidden)]
#[macro_export]
macro_rules! __logger_log {
    ($logger:expr, $level:expr, $($arg:tt)*) => {{
        if $logger.has_level($level) {
            let msg = ::std::format!($($arg)*);
            $logger.log(
                $level,
                ::std::file!(),
                "rust",
                ::std::line!() as i32,
                &msg
            );
        }
    }};
}

/// Log a message at fatal level and exit.
///
/// This macro never returns.
///
/// # Example
///
/// ```no_run
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_fatal;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let error = "something went wrong";
/// logger_fatal!(logger, "fatal error: {}", error);
/// ```
#[macro_export]
macro_rules! logger_fatal {
    ($logger:expr, $($arg:tt)*) => {{
        let msg = ::std::format!($($arg)*);
        $logger.fatal(::std::file!(), "rust", ::std::line!() as i32, &msg)
    }};
}

/// Log a message at panic level and abort.
///
/// This macro never returns.
///
/// # Example
///
/// ```no_run
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_panic;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let reason = "something went wrong";
/// logger_panic!(logger, "panic: {}", reason);
/// ```
#[macro_export]
macro_rules! logger_panic {
    ($logger:expr, $($arg:tt)*) => {{
        let msg = ::std::format!($($arg)*);
        $logger.panic(::std::file!(), "rust", ::std::line!() as i32, &msg)
    }};
}

/// Log a message at error level.
///
/// The message arguments are only evaluated if error level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_error;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let error = "something went wrong";
/// logger_error!(logger, "error occurred: {}", error);
/// ```
#[macro_export]
macro_rules! logger_error {
    ($logger:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::bindings::LOG_ERR as i32, $($arg)*);
    }};
}

/// Log a message at warning level.
///
/// The message arguments are only evaluated if warning level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_warning;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let warning_msg = "something might be wrong";
/// logger_warning!(logger, "warning: {}", warning_msg);
/// ```
#[macro_export]
macro_rules! logger_warning {
    ($logger:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::bindings::LOG_WARNING as i32, $($arg)*);
    }};
}

/// Log a message at notice level.
///
/// The message arguments are only evaluated if notice level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_notice;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let info = "noteworthy event";
/// logger_notice!(logger, "notice: {}", info);
/// ```
#[macro_export]
macro_rules! logger_notice {
    ($logger:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::bindings::LOG_NOTICE as i32, $($arg)*);
    }};
}

/// Log a message at info level.
///
/// The message arguments are only evaluated if info level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_info;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let info = "some information";
/// logger_info!(logger, "info: {}", info);
/// ```
#[macro_export]
macro_rules! logger_info {
    ($logger:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::bindings::LOG_INFO as i32, $($arg)*);
    }};
}

/// Log a message at debug level.
///
/// The message arguments are only evaluated if debug level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_debug;
///
/// # let logger = Logger::new_parent("example", LogLevel::Debug, LogFlags::empty());
/// let debug_info = "debug details";
/// logger_debug!(logger, "debug: {}", debug_info);
/// ```
#[macro_export]
macro_rules! logger_debug {
    ($logger:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::bindings::LOG_DEBUG as i32, $($arg)*);
    }};
}

/// Log a message at trace level.
///
/// The message arguments are only evaluated if trace level at the given depth is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags};
/// # use libcommon_core::logger_trace;
///
/// # let logger = Logger::new_parent("example", LogLevel::Trace, LogFlags::empty());
/// let trace_info = "trace details";
/// logger_trace!(logger, 1, "trace: {}", trace_info);
/// ```
#[macro_export]
macro_rules! logger_trace {
    ($logger:expr, $depth:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $crate::log::LOG_TRACE + $depth, $($arg)*);
    }};
}

/// Log a message at a given level.
///
/// The message arguments are only evaluated if the given level is enabled.
///
/// # Example
///
/// ```
/// # use libcommon_core::log::{Logger, LogLevel, LogFlags, LOG_INFO};
/// # use libcommon_core::logger_log;
///
/// # let logger = Logger::new_parent("example", LogLevel::Info, LogFlags::empty());
/// let debug_info = "some info";
/// logger_log!(logger, LOG_INFO, "info: {}", debug_info);
/// ```
#[macro_export]
macro_rules! logger_log {
    ($logger:expr, $level:expr, $($arg:tt)*) => {{
        $crate::__logger_log!($logger, $level as i32, $($arg)*);
    }};
}

// }}}
// {{{ Tests

#[cfg(test)]
mod tests {
    use super::{LOG_INHERITS, LOG_TRACE, LogFlags, LogLevel, Logger};
    use crate::bindings::{LOG_CRIT, LOG_DEBUG, LOG_ERR, LOG_INFO};

    // {{{ LogLevel tests

    #[test]
    fn log_level_as_raw() {
        assert_eq!(LogLevel::Crit.as_raw(), LOG_CRIT as i32);
        assert_eq!(LogLevel::Error.as_raw(), LOG_ERR as i32);
        assert_eq!(LogLevel::Info.as_raw(), LOG_INFO as i32);
        assert_eq!(LogLevel::Debug.as_raw(), LOG_DEBUG as i32);
        assert_eq!(LogLevel::Trace.as_raw(), LOG_TRACE);
        assert_eq!(LogLevel::Inherits.as_raw(), LOG_INHERITS);
    }

    #[test]
    fn log_level_trace_with_depth() {
        assert_eq!(LogLevel::trace_with_depth(0), LOG_TRACE);
        assert_eq!(LogLevel::trace_with_depth(1), LOG_TRACE + 1);
        assert_eq!(LogLevel::trace_with_depth(5), LOG_TRACE + 5);
    }

    // }}}
    // {{{ LogFlags tests

    #[test]
    fn log_flags_empty() {
        let flags = LogFlags::empty();
        assert_eq!(flags.bits(), 0);
    }

    #[test]
    fn log_flags_constants() {
        assert_eq!(LogFlags::RECURSIVE.bits(), 1);
        assert_eq!(LogFlags::FORCED.bits(), 2);
        assert_eq!(LogFlags::SILENT.bits(), 4);
    }

    #[test]
    fn log_flags_contains() {
        let flags = LogFlags::RECURSIVE;
        assert!(flags.contains(LogFlags::RECURSIVE));
        assert!(!flags.contains(LogFlags::SILENT));

        let combined = LogFlags::RECURSIVE.union(LogFlags::SILENT);
        assert!(combined.contains(LogFlags::RECURSIVE));
        assert!(combined.contains(LogFlags::SILENT));
        assert!(!combined.contains(LogFlags::FORCED));
    }

    #[test]
    fn log_flags_union() {
        let flags = LogFlags::RECURSIVE.union(LogFlags::SILENT);
        assert_eq!(flags.bits(), 5); // 1 | 4 = 5
    }

    // }}}
    // {{{ Logger tests

    #[test]
    fn logger_root() {
        let root = Logger::root();
        assert!(!root.as_ptr().is_null());
    }

    #[test]
    fn logger_new_parent() {
        let logger = Logger::new_parent("test_logger", LogLevel::Debug, LogFlags::empty());
        assert!(!logger.as_ptr().is_null());
    }

    #[test]
    fn logger_new_with_parent() {
        let parent = Logger::new_parent("parent", LogLevel::Info, LogFlags::empty());
        let child = Logger::new(
            Some(parent.as_ptr()),
            "child",
            LogLevel::Debug,
            LogFlags::empty(),
        );

        assert!(!child.as_ptr().is_null());
    }

    #[test]
    fn logger_new_inherits() {
        let parent = Logger::new_parent("parent2", LogLevel::Info, LogFlags::empty());
        let child = Logger::new_inherits(Some(parent.as_ptr()), "child2", LogFlags::empty());

        assert!(!child.as_ptr().is_null());
    }

    #[test]
    fn logger_has_level() {
        let logger = Logger::new_parent("level_test", LogLevel::Info, LogFlags::empty());

        // Should have levels up to Info
        assert!(logger.has_level(LOG_CRIT as i32));
        assert!(logger.has_level(LOG_ERR as i32));
        assert!(logger.has_level(LOG_INFO as i32));

        // Should not have Debug level (lower priority than Info)
        assert!(!logger.has_level(LOG_DEBUG as i32));
    }

    #[test]
    fn logger_level_helpers() {
        let logger = Logger::new_parent("helpers_test", LogLevel::Info, LogFlags::empty());

        assert!(logger.has_error());
        assert!(logger.has_notice());
        assert!(logger.has_info());
        assert!(!logger.has_debug());
        assert!(!logger.has_trace(0));
    }

    #[test]
    fn logger_debug_level() {
        let logger = Logger::new_parent("debug_test", LogLevel::Debug, LogFlags::empty());

        assert!(logger.has_error());
        assert!(logger.has_info());
        assert!(logger.has_debug());
        assert!(!logger.has_trace(0));
    }

    #[test]
    fn logger_from_ptr() {
        let logger = Logger::new_parent("ptr_test", LogLevel::Info, LogFlags::empty());
        let ptr = logger.as_ptr();

        let borrowed = unsafe { Logger::from_ptr(ptr) };
        assert!(borrowed.has_info());
    }

    // }}}
    // {{{ Static logger tests

    // Define test static loggers
    static TEST_STATIC_LOGGER: Logger<'static> =
        Logger::new_static_parent("test_static", LogLevel::Info, LogFlags::empty());

    static TEST_STATIC_SILENT: Logger<'static> =
        Logger::new_static_parent("test_silent", LogLevel::Debug, LogFlags::SILENT);

    static TEST_STATIC_PARENT: Logger<'static> =
        Logger::new_static_parent("test_parent", LogLevel::Notice, LogFlags::empty());

    static TEST_STATIC_CHILD: Logger<'static> = Logger::new_static(
        TEST_STATIC_PARENT.as_ptr(),
        "test_child",
        LogLevel::Inherits,
        LogFlags::empty(),
    );

    #[test]
    fn static_logger_creation() {
        assert!(!TEST_STATIC_LOGGER.as_ptr().is_null());
        assert!(!TEST_STATIC_SILENT.as_ptr().is_null());
    }

    #[test]
    fn static_logger_has_level() {
        // TEST_STATIC_LOGGER has Info level
        assert!(TEST_STATIC_LOGGER.has_error());
        assert!(TEST_STATIC_LOGGER.has_notice());
        assert!(TEST_STATIC_LOGGER.has_info());
        assert!(!TEST_STATIC_LOGGER.has_debug());
    }

    #[test]
    fn static_logger_silent() {
        // TEST_STATIC_SILENT has Debug level with SILENT flag
        assert!(TEST_STATIC_SILENT.has_error());
        assert!(TEST_STATIC_SILENT.has_info());
        assert!(TEST_STATIC_SILENT.has_debug());
        assert!(!TEST_STATIC_SILENT.has_trace(0));
    }

    #[test]
    fn static_logger() {
        // TEST_STATIC_CHILD inherits from TEST_STATIC_PARENT (Notice level)
        assert!(TEST_STATIC_CHILD.has_error());
        assert!(TEST_STATIC_CHILD.has_notice());
        assert!(!TEST_STATIC_CHILD.has_info());
    }

    #[test]
    fn static_logger_with_macros() {
        // Use logging macros with static logger
        crate::logger_error!(TEST_STATIC_SILENT, "error from static logger");
        crate::logger_info!(TEST_STATIC_SILENT, "info from static logger");
        crate::logger_debug!(TEST_STATIC_SILENT, "debug from static logger");
    }

    #[test]
    fn static_logger_level_helpers() {
        assert!(TEST_STATIC_LOGGER.has_error());
        assert!(TEST_STATIC_LOGGER.has_warning());
        assert!(TEST_STATIC_LOGGER.has_notice());
        assert!(TEST_STATIC_LOGGER.has_info());
        assert!(!TEST_STATIC_LOGGER.has_debug());
        assert!(!TEST_STATIC_LOGGER.has_trace(0));
    }

    // }}}
    // {{{ Logging macro tests

    #[test]
    fn logging_macros_emit_logs() {
        // Use SILENT flag to avoid actual output but still exercise the code path
        let logger = Logger::new_parent("macro_test", LogLevel::Debug, LogFlags::SILENT);

        // These should all execute without panicking
        crate::logger_error!(logger, "error message");
        crate::logger_warning!(logger, "warning message");
        crate::logger_notice!(logger, "notice message");
        crate::logger_info!(logger, "info message");
        crate::logger_debug!(logger, "debug message");
        crate::logger_log!(logger, LOG_INFO, "info message");
    }

    #[test]
    fn logging_macros_with_format_args() {
        let logger = Logger::new_parent("format_test", LogLevel::Info, LogFlags::SILENT);

        let value = 42;
        let name = "test";

        crate::logger_error!(logger, "error: {} = {}", name, value);
        crate::logger_info!(logger, "info: value is {value}");
    }

    #[test]
    fn logging_macros_lazy_evaluation() {
        use std::sync::atomic::{AtomicUsize, Ordering};

        static CALL_COUNT: AtomicUsize = AtomicUsize::new(0);

        fn expensive_computation() -> &'static str {
            CALL_COUNT.fetch_add(1, Ordering::SeqCst);
            "computed"
        }

        // Logger with Info level - debug should NOT be evaluated
        let logger = Logger::new_parent("lazy_test", LogLevel::Info, LogFlags::SILENT);

        // Reset counter
        CALL_COUNT.store(0, Ordering::SeqCst);

        // This should NOT evaluate expensive_computation() because debug level is disabled
        crate::logger_debug!(logger, "debug: {}", expensive_computation());

        assert_eq!(
            CALL_COUNT.load(Ordering::SeqCst),
            0,
            "expensive_computation should not be called when debug level is disabled"
        );

        // This SHOULD evaluate expensive_computation() because info level is enabled
        crate::logger_info!(logger, "info: {}", expensive_computation());

        assert_eq!(
            CALL_COUNT.load(Ordering::SeqCst),
            1,
            "expensive_computation should be called when info level is enabled"
        );
    }

    #[test]
    fn logging_macros_trace_with_depth() {
        // Logger with Trace level at depth 2
        let logger = Logger::new_parent("trace_test", LogLevel::Trace, LogFlags::SILENT);

        // Trace at depth 0 should work
        crate::logger_trace!(logger, 0, "trace depth 0");
    }

    // }}}
}

// }}}
