//! Context management for vivid

use std::ffi::{CStr, CString};
use std::path::Path;
use std::ptr;

use crate::chain::Chain;
use crate::error::{check_result, Error, Result};

/// Configuration for creating a vivid context
#[derive(Debug, Clone)]
pub struct ContextConfig {
    /// Render width in pixels
    pub width: u32,
    /// Render height in pixels
    pub height: u32,
    /// Enable WebGPU validation (debug mode)
    pub enable_validation: bool,
}

impl ContextConfig {
    /// Create a new context config with the given resolution
    pub fn new(width: u32, height: u32) -> Self {
        Self {
            width,
            height,
            enable_validation: false,
        }
    }

    /// Enable validation mode
    pub fn with_validation(mut self, enable: bool) -> Self {
        self.enable_validation = enable;
        self
    }
}

impl Default for ContextConfig {
    fn default() -> Self {
        Self::new(1280, 720)
    }
}

/// Compilation status information
#[derive(Debug, Clone)]
pub struct CompileStatus {
    /// Whether compilation succeeded
    pub success: bool,
    /// Error message (if failed)
    pub message: Option<String>,
    /// Line number of first error
    pub error_line: Option<u32>,
    /// Column of first error
    pub error_column: Option<u32>,
}

/// A vivid context for processing chains
///
/// The context owns the chain and manages the lifecycle of operators.
/// It accepts an external wgpu device/queue for rendering.
pub struct Context {
    ptr: *mut vivid_sys::VividContext,
}

// Context can be sent between threads (vivid is single-threaded but the handle is safe)
unsafe impl Send for Context {}

impl Context {
    /// Create a new context with a native window handle
    ///
    /// This creates a context that owns all GPU resources (instance, device, surface).
    /// vivid-core will handle all rendering including the node graph visualizer.
    ///
    /// # Arguments
    ///
    /// * `native_window` - Platform-specific window handle (NSWindow* on macOS, HWND on Windows)
    /// * `config` - Context configuration
    ///
    /// # Safety
    ///
    /// The native_window must be a valid platform window handle that remains valid
    /// for the lifetime of this context.
    pub unsafe fn with_window(
        native_window: *mut std::ffi::c_void,
        config: ContextConfig,
    ) -> Result<Self> {
        let ffi_config = vivid_sys::VividContextConfig {
            width: config.width as i32,
            height: config.height as i32,
            enable_validation: config.enable_validation,
        };

        let mut ctx_ptr: *mut vivid_sys::VividContext = ptr::null_mut();

        let result = vivid_sys::vivid_context_create_with_window(
            native_window,
            &ffi_config,
            &mut ctx_ptr,
        );

        check_result(result)?;

        if ctx_ptr.is_null() {
            return Err(Error::Internal("Context pointer is null".into()));
        }

        Ok(Self { ptr: ctx_ptr })
    }

    /// Render a complete frame (chain output + visualizer UI)
    ///
    /// This should be called once per frame. It handles all rendering including
    /// the node graph visualizer overlay.
    ///
    /// Only valid for contexts created with `with_window()`.
    pub fn render_frame(&self) -> Result<()> {
        let result = unsafe { vivid_sys::vivid_context_render_frame(self.ptr) };
        check_result(result)
    }

    /// Resize the rendering surface
    ///
    /// Call this when the window size changes.
    /// Only valid for contexts created with `with_window()`.
    pub fn resize_surface(&mut self, width: u32, height: u32) -> Result<()> {
        let result = unsafe {
            vivid_sys::vivid_context_resize_surface(self.ptr, width as i32, height as i32)
        };
        check_result(result)
    }

    /// Set visualizer UI visibility
    ///
    /// When false, only the chain output is rendered (useful for fullscreen preview).
    pub fn set_visualizer_visible(&mut self, visible: bool) {
        unsafe { vivid_sys::vivid_context_set_visualizer_visible(self.ptr, visible) }
    }

    /// Check if visualizer UI is visible
    pub fn is_visualizer_visible(&self) -> bool {
        unsafe { vivid_sys::vivid_context_is_visualizer_visible(self.ptr) }
    }

    /// Get the name of the currently selected operator in the visualizer
    ///
    /// Returns `None` if no operator is selected.
    pub fn selected_operator(&self) -> Option<String> {
        let ptr = unsafe { vivid_sys::vivid_context_get_selected_operator(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() })
        }
    }

    /// Select an operator in the visualizer by name
    ///
    /// The selection will be applied on the next render frame.
    pub fn select_operator(&mut self, name: &str) {
        if let Ok(c_name) = CString::new(name) {
            unsafe { vivid_sys::vivid_context_select_operator(self.ptr, c_name.as_ptr()) }
        }
    }

    /// Create a new context with an external wgpu device and queue
    ///
    /// # Arguments
    ///
    /// * `device` - wgpu device (must outlive the context)
    /// * `queue` - wgpu queue (must outlive the context)
    /// * `config` - Context configuration
    ///
    /// # Safety
    ///
    /// The device and queue must remain valid for the lifetime of this context.
    pub fn new(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        config: ContextConfig,
    ) -> Result<Self> {
        // Convert wgpu handles to raw pointers
        // Note: wgpu-rs doesn't directly expose raw handles, so we pass the wgpu-rs
        // objects as opaque pointers. The C API treats them as opaque anyway.
        // In the future, we may need wgpu-native or hal access for proper interop.
        let device_ptr = device as *const wgpu::Device as *mut std::ffi::c_void;
        let queue_ptr = queue as *const wgpu::Queue as *mut std::ffi::c_void;

        let ffi_config = vivid_sys::VividContextConfig {
            width: config.width as i32,
            height: config.height as i32,
            enable_validation: config.enable_validation,
        };

        let mut ctx_ptr: *mut vivid_sys::VividContext = ptr::null_mut();

        let result = unsafe {
            vivid_sys::vivid_context_create_external(
                device_ptr,
                queue_ptr,
                &ffi_config,
                &mut ctx_ptr,
            )
        };

        check_result(result)?;

        if ctx_ptr.is_null() {
            return Err(Error::Internal("Context pointer is null".into()));
        }

        Ok(Self { ptr: ctx_ptr })
    }

    /// Create a context from raw wgpu handles (native pointers)
    ///
    /// This is useful when you have raw WebGPU handles from wgpu-native or Dawn.
    ///
    /// # Safety
    ///
    /// The device and queue pointers must be valid WebGPU handles.
    pub unsafe fn from_raw(
        device: *mut std::ffi::c_void,
        queue: *mut std::ffi::c_void,
        config: ContextConfig,
    ) -> Result<Self> {
        let ffi_config = vivid_sys::VividContextConfig {
            width: config.width as i32,
            height: config.height as i32,
            enable_validation: config.enable_validation,
        };

        let mut ctx_ptr: *mut vivid_sys::VividContext = ptr::null_mut();

        let result = vivid_sys::vivid_context_create_external(
            device,
            queue,
            &ffi_config,
            &mut ctx_ptr,
        );

        check_result(result)?;

        if ctx_ptr.is_null() {
            return Err(Error::Internal("Context pointer is null".into()));
        }

        Ok(Self { ptr: ctx_ptr })
    }

    /// Load a project from a directory path
    ///
    /// The directory must contain a `chain.cpp` file.
    pub fn load_project<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
        let path_str = path.as_ref().to_string_lossy();
        let c_path = CString::new(path_str.as_ref())
            .map_err(|_| Error::InvalidArgument("Invalid path".into()))?;

        let result = unsafe { vivid_sys::vivid_context_load_project(self.ptr, c_path.as_ptr()) };

        check_result(result)
    }

    /// Reload the current project
    pub fn reload(&mut self) -> Result<()> {
        let result = unsafe { vivid_sys::vivid_context_reload(self.ptr) };
        check_result(result)
    }

    /// Unload the current project
    pub fn unload_project(&mut self) -> Result<()> {
        let result = unsafe { vivid_sys::vivid_context_unload_project(self.ptr) };
        check_result(result)
    }

    /// Get the compilation status
    pub fn compile_status(&self) -> CompileStatus {
        let status = unsafe { vivid_sys::vivid_context_get_compile_status(self.ptr) };

        let message = if status.message.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(status.message).to_string_lossy().into_owned() })
        };

        let error_line = if status.error_line > 0 {
            Some(status.error_line as u32)
        } else {
            None
        };

        let error_column = if status.error_column > 0 {
            Some(status.error_column as u32)
        } else {
            None
        };

        CompileStatus {
            success: status.success,
            message,
            error_line,
            error_column,
        }
    }

    /// Check if a project is loaded
    pub fn has_project(&self) -> bool {
        unsafe { vivid_sys::vivid_context_has_project(self.ptr) }
    }

    /// Get the loaded project path
    pub fn project_path(&self) -> Option<String> {
        let ptr = unsafe { vivid_sys::vivid_context_get_project_path(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() })
        }
    }

    /// Process a single frame
    ///
    /// # Arguments
    ///
    /// * `dt` - Delta time since last frame in seconds
    pub fn process_frame(&mut self, dt: f64) -> Result<()> {
        let result = unsafe { vivid_sys::vivid_context_process_frame(self.ptr, dt) };
        check_result(result)
    }

    /// Get the current frame number
    pub fn frame(&self) -> u64 {
        unsafe { vivid_sys::vivid_context_get_frame(self.ptr) }
    }

    /// Get elapsed time in seconds
    pub fn time(&self) -> f64 {
        unsafe { vivid_sys::vivid_context_get_time(self.ptr) }
    }

    /// Reset time and frame counter
    pub fn reset_time(&mut self) {
        unsafe { vivid_sys::vivid_context_reset_time(self.ptr) }
    }

    /// Set render resolution
    pub fn set_resolution(&mut self, width: u32, height: u32) -> Result<()> {
        let result = unsafe {
            vivid_sys::vivid_context_set_resolution(self.ptr, width as i32, height as i32)
        };
        check_result(result)
    }

    /// Get render width
    pub fn width(&self) -> u32 {
        unsafe { vivid_sys::vivid_context_get_width(self.ptr) as u32 }
    }

    /// Get render height
    pub fn height(&self) -> u32 {
        unsafe { vivid_sys::vivid_context_get_height(self.ptr) as u32 }
    }

    /// Set mouse position
    pub fn set_mouse_position(&mut self, x: f32, y: f32) {
        unsafe { vivid_sys::vivid_context_set_mouse_position(self.ptr, x, y) }
    }

    /// Set mouse button state
    pub fn set_mouse_button(&mut self, button: u32, pressed: bool) {
        unsafe { vivid_sys::vivid_context_set_mouse_button(self.ptr, button as i32, pressed) }
    }

    /// Set key state
    pub fn set_key(&mut self, keycode: u32, pressed: bool) {
        unsafe { vivid_sys::vivid_context_set_key(self.ptr, keycode as i32, pressed) }
    }

    /// Add scroll delta
    pub fn add_scroll(&mut self, dx: f32, dy: f32) {
        unsafe { vivid_sys::vivid_context_add_scroll(self.ptr, dx, dy) }
    }

    /// Get the chain
    ///
    /// Returns `None` if no project is loaded.
    pub fn chain(&self) -> Option<Chain> {
        let ptr = unsafe { vivid_sys::vivid_context_get_chain(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(Chain::from_raw(ptr))
        }
    }

    /// Get the output texture view
    ///
    /// Returns the raw WebGPU texture view pointer from the chain's output.
    /// Returns `None` if no output is set.
    pub fn output_view_raw(&self) -> Option<*mut std::ffi::c_void> {
        let ptr = unsafe { vivid_sys::vivid_context_get_output_view(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }

    /// Get the output texture
    ///
    /// Returns the raw WebGPU texture pointer from the chain's output.
    /// Returns `None` if no output is set.
    pub fn output_texture_raw(&self) -> Option<*mut std::ffi::c_void> {
        let ptr = unsafe { vivid_sys::vivid_context_get_output_texture(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }

    /// Capture the current output to a PNG file
    pub fn capture_snapshot<P: AsRef<Path>>(&self, path: P) -> Result<()> {
        let path_str = path.as_ref().to_string_lossy();
        let c_path = CString::new(path_str.as_ref())
            .map_err(|_| Error::InvalidArgument("Invalid path".into()))?;

        let result = unsafe { vivid_sys::vivid_context_capture_snapshot(self.ptr, c_path.as_ptr()) };
        check_result(result)
    }

    /// Get the raw context pointer (for advanced usage)
    pub fn as_raw(&self) -> *mut vivid_sys::VividContext {
        self.ptr
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { vivid_sys::vivid_context_destroy(self.ptr) };
        }
    }
}

/// Get the vivid version string
pub fn version() -> String {
    unsafe {
        let ptr = vivid_sys::vivid_get_version();
        if ptr.is_null() {
            "unknown".to_string()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}

/// Get the vivid API version number
pub fn api_version() -> i32 {
    unsafe { vivid_sys::vivid_get_api_version() }
}
