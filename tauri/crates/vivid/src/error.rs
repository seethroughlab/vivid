//! Error types for the vivid crate

use std::ffi::CStr;
use thiserror::Error;
use vivid_sys::VividResult;

/// Result type alias for vivid operations
pub type Result<T> = std::result::Result<T, Error>;

/// Error types for vivid operations
#[derive(Error, Debug)]
pub enum Error {
    /// Invalid argument passed to a function
    #[error("Invalid argument: {0}")]
    InvalidArgument(String),

    /// Context not initialized
    #[error("Context not initialized")]
    NotInitialized,

    /// Failed to load project
    #[error("Failed to load project: {0}")]
    LoadFailed(String),

    /// Compilation failed
    #[error("Compilation failed: {0}")]
    CompileFailed(String),

    /// No chain loaded
    #[error("No chain loaded")]
    NoChain,

    /// Operator not found
    #[error("Operator not found: {0}")]
    OperatorNotFound(String),

    /// Parameter not found
    #[error("Parameter not found: {0}")]
    ParamNotFound(String),

    /// Internal error
    #[error("Internal error: {0}")]
    Internal(String),
}

impl Error {
    /// Create an error from a VividResult code
    pub fn from_result(result: VividResult) -> Self {
        let message = get_last_error().unwrap_or_default();

        match result {
            VividResult::Ok => unreachable!("from_result called with Ok"),
            VividResult::ErrorInvalidArgument => Error::InvalidArgument(message),
            VividResult::ErrorNotInitialized => Error::NotInitialized,
            VividResult::ErrorLoadFailed => Error::LoadFailed(message),
            VividResult::ErrorCompileFailed => Error::CompileFailed(message),
            VividResult::ErrorNoChain => Error::NoChain,
            VividResult::ErrorOperatorNotFound => Error::OperatorNotFound(message),
            VividResult::ErrorParamNotFound => Error::ParamNotFound(message),
            VividResult::ErrorInternal => Error::Internal(message),
        }
    }
}

/// Check a VividResult and convert to Result
pub fn check_result(result: VividResult) -> Result<()> {
    if result.is_ok() {
        Ok(())
    } else {
        Err(Error::from_result(result))
    }
}

/// Get the last error message from vivid
fn get_last_error() -> Option<String> {
    unsafe {
        let ptr = vivid_sys::vivid_get_last_error();
        if ptr.is_null() {
            None
        } else {
            Some(CStr::from_ptr(ptr).to_string_lossy().into_owned())
        }
    }
}
