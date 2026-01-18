//! Raw FFI bindings to vivid-core C API
//!
//! This crate provides unsafe bindings to the vivid C API defined in `vivid_c.h`.
//! For a safe Rust API, use the `vivid` crate instead.

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::{c_char, c_double, c_float, c_int, c_void};

// =============================================================================
// Opaque WebGPU types (matching wgpu-native)
// =============================================================================

pub type VividWGPUDevice = *mut c_void;
pub type VividWGPUQueue = *mut c_void;
pub type VividWGPUTextureView = *mut c_void;
pub type VividWGPUTexture = *mut c_void;

// =============================================================================
// Opaque Handle Types
// =============================================================================

/// Opaque context handle
#[repr(C)]
pub struct VividContext {
    _private: [u8; 0],
}

/// Opaque chain handle
#[repr(C)]
pub struct VividChain {
    _private: [u8; 0],
}

/// Opaque operator handle
#[repr(C)]
pub struct VividOperator {
    _private: [u8; 0],
}

// =============================================================================
// Result Codes
// =============================================================================

/// Result codes for API calls
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VividResult {
    Ok = 0,
    ErrorInvalidArgument = 1,
    ErrorNotInitialized = 2,
    ErrorLoadFailed = 3,
    ErrorCompileFailed = 4,
    ErrorNoChain = 5,
    ErrorOperatorNotFound = 6,
    ErrorParamNotFound = 7,
    ErrorInternal = 99,
}

// =============================================================================
// Output Kind Enum
// =============================================================================

/// Output type classification for operators
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VividOutputKind {
    Texture = 0,
    CpuPixels = 1,
    Value = 2,
    ValueArray = 3,
    Geometry = 4,
    Camera = 5,
    Light = 6,
    Audio = 7,
    AudioValue = 8,
    Event = 9,
}

// =============================================================================
// Parameter Type Enum
// =============================================================================

/// Parameter types for UI/serialization
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VividParamType {
    Float = 0,
    Int = 1,
    Bool = 2,
    Vec2 = 3,
    Vec3 = 4,
    Vec4 = 5,
    Color = 6,
    String = 7,
    FilePath = 8,
    Enum = 9,
    Adsr = 10,
    DeviceList = 11,
}

// =============================================================================
// Configuration Structures
// =============================================================================

/// Configuration for creating a context
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VividContextConfig {
    pub width: c_int,
    pub height: c_int,
    pub enable_validation: bool,
}

impl Default for VividContextConfig {
    fn default() -> Self {
        Self {
            width: 1280,
            height: 720,
            enable_validation: false,
        }
    }
}

/// Compilation status
#[repr(C)]
#[derive(Debug)]
pub struct VividCompileStatus {
    pub success: bool,
    pub message: *const c_char,
    pub error_line: c_int,
    pub error_column: c_int,
}

/// Texture information
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VividTextureInfo {
    pub width: c_int,
    pub height: c_int,
    pub format: c_int,
    pub has_alpha: bool,
}

/// Parameter declaration for introspection
#[repr(C)]
#[derive(Debug)]
pub struct VividParamDecl {
    pub name: *const c_char,
    pub param_type: VividParamType,
    pub min_val: c_float,
    pub max_val: c_float,
    pub default_val: [c_float; 4],
    pub string_default: *const c_char,
    pub enum_count: c_int,
    pub enum_labels: *const *const c_char,
}

// =============================================================================
// External Functions
// =============================================================================

#[link(name = "vivid-c")]
extern "C" {
    // =========================================================================
    // Error Handling
    // =========================================================================

    /// Get the last error message
    pub fn vivid_get_last_error() -> *const c_char;

    /// Clear the last error
    pub fn vivid_clear_error();

    // =========================================================================
    // Context Lifecycle
    // =========================================================================

    /// Create a context with external WebGPU device and queue
    pub fn vivid_context_create_external(
        device: VividWGPUDevice,
        queue: VividWGPUQueue,
        config: *const VividContextConfig,
        out_ctx: *mut *mut VividContext,
    ) -> VividResult;

    /// Create a context with a native window handle
    /// On macOS, native_window is NSWindow*. On Windows, it's HWND.
    /// vivid-core will create and own all GPU resources.
    pub fn vivid_context_create_with_window(
        native_window: *mut c_void,
        config: *const VividContextConfig,
        out_ctx: *mut *mut VividContext,
    ) -> VividResult;

    /// Render a complete frame (chain output + visualizer UI)
    /// Only valid for contexts created with vivid_context_create_with_window
    pub fn vivid_context_render_frame(ctx: *mut VividContext) -> VividResult;

    /// Resize the rendering surface
    /// Only valid for contexts created with vivid_context_create_with_window
    pub fn vivid_context_resize_surface(
        ctx: *mut VividContext,
        width: c_int,
        height: c_int,
    ) -> VividResult;

    /// Set visualizer UI visibility
    pub fn vivid_context_set_visualizer_visible(ctx: *mut VividContext, visible: bool);

    /// Check if visualizer UI is visible
    pub fn vivid_context_is_visualizer_visible(ctx: *mut VividContext) -> bool;

    /// Get the name of the currently selected operator in the visualizer
    /// Returns NULL if no operator is selected
    pub fn vivid_context_get_selected_operator(ctx: *mut VividContext) -> *const c_char;

    /// Select an operator in the visualizer by name
    pub fn vivid_context_select_operator(ctx: *mut VividContext, name: *const c_char);

    /// Destroy a context and free all resources
    pub fn vivid_context_destroy(ctx: *mut VividContext);

    // =========================================================================
    // Project Loading
    // =========================================================================

    /// Load a project from a directory path
    pub fn vivid_context_load_project(ctx: *mut VividContext, path: *const c_char) -> VividResult;

    /// Reload the current project
    pub fn vivid_context_reload(ctx: *mut VividContext) -> VividResult;

    /// Unload the current project
    pub fn vivid_context_unload_project(ctx: *mut VividContext) -> VividResult;

    /// Get compilation status
    pub fn vivid_context_get_compile_status(ctx: *mut VividContext) -> VividCompileStatus;

    /// Check if a project is loaded
    pub fn vivid_context_has_project(ctx: *mut VividContext) -> bool;

    /// Get the loaded project path
    pub fn vivid_context_get_project_path(ctx: *mut VividContext) -> *const c_char;

    // =========================================================================
    // Frame Processing
    // =========================================================================

    /// Process a single frame
    pub fn vivid_context_process_frame(ctx: *mut VividContext, dt: c_double) -> VividResult;

    /// Get the current frame number
    pub fn vivid_context_get_frame(ctx: *mut VividContext) -> u64;

    /// Get elapsed time
    pub fn vivid_context_get_time(ctx: *mut VividContext) -> c_double;

    /// Reset time and frame counter
    pub fn vivid_context_reset_time(ctx: *mut VividContext);

    // =========================================================================
    // Resolution Management
    // =========================================================================

    /// Set render resolution
    pub fn vivid_context_set_resolution(
        ctx: *mut VividContext,
        width: c_int,
        height: c_int,
    ) -> VividResult;

    /// Get render width
    pub fn vivid_context_get_width(ctx: *mut VividContext) -> c_int;

    /// Get render height
    pub fn vivid_context_get_height(ctx: *mut VividContext) -> c_int;

    // =========================================================================
    // Input Injection
    // =========================================================================

    /// Set mouse position
    pub fn vivid_context_set_mouse_position(ctx: *mut VividContext, x: c_float, y: c_float);

    /// Set mouse button state
    pub fn vivid_context_set_mouse_button(ctx: *mut VividContext, button: c_int, pressed: bool);

    /// Set key state
    pub fn vivid_context_set_key(ctx: *mut VividContext, keycode: c_int, pressed: bool);

    /// Add scroll delta
    pub fn vivid_context_add_scroll(ctx: *mut VividContext, dx: c_float, dy: c_float);

    // =========================================================================
    // Chain Access
    // =========================================================================

    /// Get the chain from a context
    pub fn vivid_context_get_chain(ctx: *mut VividContext) -> *mut VividChain;

    /// Get the output texture view from the chain
    pub fn vivid_context_get_output_view(ctx: *mut VividContext) -> VividWGPUTextureView;

    /// Get the output texture from the chain
    pub fn vivid_context_get_output_texture(ctx: *mut VividContext) -> VividWGPUTexture;

    // =========================================================================
    // Operator Iteration
    // =========================================================================

    /// Get number of operators in the chain
    pub fn vivid_chain_get_operator_count(chain: *mut VividChain) -> c_int;

    /// Get operator by index
    pub fn vivid_chain_get_operator_by_index(
        chain: *mut VividChain,
        index: c_int,
    ) -> *mut VividOperator;

    /// Get operator by name
    pub fn vivid_chain_get_operator_by_name(
        chain: *mut VividChain,
        name: *const c_char,
    ) -> *mut VividOperator;

    /// Get the output operator
    pub fn vivid_chain_get_output_operator(chain: *mut VividChain) -> *mut VividOperator;

    // =========================================================================
    // Operator Information
    // =========================================================================

    /// Get operator name
    pub fn vivid_operator_get_name(op: *mut VividOperator) -> *const c_char;

    /// Get operator type name
    pub fn vivid_operator_get_type_name(op: *mut VividOperator) -> *const c_char;

    /// Get operator output kind
    pub fn vivid_operator_get_output_kind(op: *mut VividOperator) -> VividOutputKind;

    /// Check if operator is bypassed
    pub fn vivid_operator_is_bypassed(op: *mut VividOperator) -> bool;

    /// Set operator bypass state
    pub fn vivid_operator_set_bypassed(op: *mut VividOperator, bypassed: bool);

    // =========================================================================
    // Operator Outputs (Textures)
    // =========================================================================

    /// Get operator output texture view
    pub fn vivid_operator_get_output_view(op: *mut VividOperator) -> VividWGPUTextureView;

    /// Get operator output texture
    pub fn vivid_operator_get_output_texture(op: *mut VividOperator) -> VividWGPUTexture;

    /// Get texture information
    pub fn vivid_operator_get_texture_info(
        op: *mut VividOperator,
        out_info: *mut VividTextureInfo,
    ) -> bool;

    /// Get operator output value
    pub fn vivid_operator_get_output_value(op: *mut VividOperator) -> c_float;

    // =========================================================================
    // Operator Parameters
    // =========================================================================

    /// Get number of parameters
    pub fn vivid_operator_get_param_count(op: *mut VividOperator) -> c_int;

    /// Get parameter declaration by index
    pub fn vivid_operator_get_param_decl(
        op: *mut VividOperator,
        index: c_int,
        out_decl: *mut VividParamDecl,
    ) -> bool;

    /// Get parameter value
    pub fn vivid_operator_get_param(
        op: *mut VividOperator,
        name: *const c_char,
        out_value: *mut c_float,
    ) -> bool;

    /// Set parameter value
    pub fn vivid_operator_set_param(
        op: *mut VividOperator,
        name: *const c_char,
        value: *const c_float,
    ) -> bool;

    /// Get parameter string value
    pub fn vivid_operator_get_param_string(
        op: *mut VividOperator,
        name: *const c_char,
    ) -> *const c_char;

    /// Set parameter string value
    pub fn vivid_operator_set_param_string(
        op: *mut VividOperator,
        name: *const c_char,
        value: *const c_char,
    ) -> bool;

    // =========================================================================
    // Operator Inputs
    // =========================================================================

    /// Get number of inputs
    pub fn vivid_operator_get_input_count(op: *mut VividOperator) -> c_int;

    /// Get input operator by index
    pub fn vivid_operator_get_input(op: *mut VividOperator, index: c_int) -> *mut VividOperator;

    /// Get input name/label
    pub fn vivid_operator_get_input_name(op: *mut VividOperator, index: c_int) -> *const c_char;

    // =========================================================================
    // Operator Registry
    // =========================================================================

    /// Get number of registered operator types
    pub fn vivid_registry_get_operator_count() -> c_int;

    /// Get registered operator type name by index
    pub fn vivid_registry_get_operator_name(index: c_int) -> *const c_char;

    /// Get operator category by index
    pub fn vivid_registry_get_operator_category(index: c_int) -> *const c_char;

    // =========================================================================
    // Snapshot/Capture
    // =========================================================================

    /// Capture current output to a PNG file
    pub fn vivid_context_capture_snapshot(
        ctx: *mut VividContext,
        path: *const c_char,
    ) -> VividResult;

    /// Capture operator output to a PNG file
    pub fn vivid_operator_capture_snapshot(
        op: *mut VividOperator,
        path: *const c_char,
    ) -> VividResult;

    // =========================================================================
    // Version Information
    // =========================================================================

    /// Get Vivid version string
    pub fn vivid_get_version() -> *const c_char;

    /// Get Vivid API version number
    pub fn vivid_get_api_version() -> c_int;
}

// =============================================================================
// Helper Functions
// =============================================================================

impl VividResult {
    /// Check if the result is Ok
    pub fn is_ok(self) -> bool {
        self == VividResult::Ok
    }

    /// Check if the result is an error
    pub fn is_err(self) -> bool {
        self != VividResult::Ok
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_struct_sizes() {
        // Ensure structs have expected sizes for FFI compatibility
        assert_eq!(std::mem::size_of::<VividContextConfig>(), 12);
    }
}
