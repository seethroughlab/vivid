//! Safe Rust wrapper for vivid-core
//!
//! This crate provides a safe, ergonomic Rust API for vivid-core.
//!
//! # Example
//!
//! ```no_run
//! use vivid::{Context, ContextConfig};
//!
//! // Create context with external wgpu device
//! let config = ContextConfig::new(1920, 1080);
//! let mut ctx = Context::new(&device, &queue, config)?;
//!
//! // Load a project
//! ctx.load_project("/path/to/project")?;
//!
//! // Process frames
//! loop {
//!     ctx.process_frame(1.0 / 60.0)?;
//!
//!     // Get output texture for rendering
//!     if let Some(view) = ctx.output_view() {
//!         // Render the texture...
//!     }
//! }
//! ```

mod context;
mod chain;
mod operator;
mod error;

pub use context::{Context, ContextConfig, CompileStatus, version, api_version};
pub use chain::Chain;
pub use operator::{Operator, OutputKind, ParamType, ParamDecl, TextureInfo};
pub use operator::{RegistryEntry, registry_count, registry_entry, registry_entries};
pub use error::{Error, Result};

/// Re-export vivid-sys for advanced usage
pub use vivid_sys as ffi;
