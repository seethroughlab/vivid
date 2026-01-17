//! Operator management for vivid

use std::ffi::{CStr, CString};

/// Output type classification for operators
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OutputKind {
    Texture,
    CpuPixels,
    Value,
    ValueArray,
    Geometry,
    Camera,
    Light,
    Audio,
    AudioValue,
    Event,
}

impl From<vivid_sys::VividOutputKind> for OutputKind {
    fn from(kind: vivid_sys::VividOutputKind) -> Self {
        match kind {
            vivid_sys::VividOutputKind::Texture => OutputKind::Texture,
            vivid_sys::VividOutputKind::CpuPixels => OutputKind::CpuPixels,
            vivid_sys::VividOutputKind::Value => OutputKind::Value,
            vivid_sys::VividOutputKind::ValueArray => OutputKind::ValueArray,
            vivid_sys::VividOutputKind::Geometry => OutputKind::Geometry,
            vivid_sys::VividOutputKind::Camera => OutputKind::Camera,
            vivid_sys::VividOutputKind::Light => OutputKind::Light,
            vivid_sys::VividOutputKind::Audio => OutputKind::Audio,
            vivid_sys::VividOutputKind::AudioValue => OutputKind::AudioValue,
            vivid_sys::VividOutputKind::Event => OutputKind::Event,
        }
    }
}

/// Parameter types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParamType {
    Float,
    Int,
    Bool,
    Vec2,
    Vec3,
    Vec4,
    Color,
    String,
    FilePath,
    Enum,
    Adsr,
    DeviceList,
}

impl From<vivid_sys::VividParamType> for ParamType {
    fn from(ty: vivid_sys::VividParamType) -> Self {
        match ty {
            vivid_sys::VividParamType::Float => ParamType::Float,
            vivid_sys::VividParamType::Int => ParamType::Int,
            vivid_sys::VividParamType::Bool => ParamType::Bool,
            vivid_sys::VividParamType::Vec2 => ParamType::Vec2,
            vivid_sys::VividParamType::Vec3 => ParamType::Vec3,
            vivid_sys::VividParamType::Vec4 => ParamType::Vec4,
            vivid_sys::VividParamType::Color => ParamType::Color,
            vivid_sys::VividParamType::String => ParamType::String,
            vivid_sys::VividParamType::FilePath => ParamType::FilePath,
            vivid_sys::VividParamType::Enum => ParamType::Enum,
            vivid_sys::VividParamType::Adsr => ParamType::Adsr,
            vivid_sys::VividParamType::DeviceList => ParamType::DeviceList,
        }
    }
}

/// Texture information
#[derive(Debug, Clone)]
pub struct TextureInfo {
    pub width: u32,
    pub height: u32,
    pub format: i32,
    pub has_alpha: bool,
}

/// Parameter declaration
#[derive(Debug, Clone)]
pub struct ParamDecl {
    pub name: String,
    pub param_type: ParamType,
    pub min_val: f32,
    pub max_val: f32,
    pub default_val: [f32; 4],
    pub string_default: Option<String>,
    pub enum_labels: Vec<String>,
}

/// A reference to a vivid operator
///
/// Operators are owned by the chain. This is a lightweight handle.
pub struct Operator {
    ptr: *mut vivid_sys::VividOperator,
}

impl Operator {
    /// Create an operator handle from a raw pointer
    pub(crate) fn from_raw(ptr: *mut vivid_sys::VividOperator) -> Self {
        Self { ptr }
    }

    /// Get the operator name (instance name in the chain)
    pub fn name(&self) -> String {
        unsafe {
            let ptr = vivid_sys::vivid_operator_get_name(self.ptr);
            if ptr.is_null() {
                String::new()
            } else {
                CStr::from_ptr(ptr).to_string_lossy().into_owned()
            }
        }
    }

    /// Get the operator type name (e.g., "Noise", "Blur")
    pub fn type_name(&self) -> String {
        unsafe {
            let ptr = vivid_sys::vivid_operator_get_type_name(self.ptr);
            if ptr.is_null() {
                String::new()
            } else {
                CStr::from_ptr(ptr).to_string_lossy().into_owned()
            }
        }
    }

    /// Get the output kind
    pub fn output_kind(&self) -> OutputKind {
        let kind = unsafe { vivid_sys::vivid_operator_get_output_kind(self.ptr) };
        kind.into()
    }

    /// Check if the operator is bypassed
    pub fn is_bypassed(&self) -> bool {
        unsafe { vivid_sys::vivid_operator_is_bypassed(self.ptr) }
    }

    /// Set the bypass state
    pub fn set_bypassed(&mut self, bypassed: bool) {
        unsafe { vivid_sys::vivid_operator_set_bypassed(self.ptr, bypassed) }
    }

    /// Get the output texture view (raw pointer)
    ///
    /// Returns `None` if not a texture operator or no output available.
    pub fn output_view_raw(&self) -> Option<*mut std::ffi::c_void> {
        let ptr = unsafe { vivid_sys::vivid_operator_get_output_view(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }

    /// Get the output texture (raw pointer)
    pub fn output_texture_raw(&self) -> Option<*mut std::ffi::c_void> {
        let ptr = unsafe { vivid_sys::vivid_operator_get_output_texture(self.ptr) };
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }

    /// Get texture information
    pub fn texture_info(&self) -> Option<TextureInfo> {
        let mut info = vivid_sys::VividTextureInfo {
            width: 0,
            height: 0,
            format: 0,
            has_alpha: false,
        };

        let has_info = unsafe { vivid_sys::vivid_operator_get_texture_info(self.ptr, &mut info) };

        if has_info {
            Some(TextureInfo {
                width: info.width as u32,
                height: info.height as u32,
                format: info.format,
                has_alpha: info.has_alpha,
            })
        } else {
            None
        }
    }

    /// Get the output value (for Value operators)
    pub fn output_value(&self) -> f32 {
        unsafe { vivid_sys::vivid_operator_get_output_value(self.ptr) }
    }

    /// Get the number of parameters
    pub fn param_count(&self) -> usize {
        let count = unsafe { vivid_sys::vivid_operator_get_param_count(self.ptr) };
        count.max(0) as usize
    }

    /// Get a parameter declaration by index
    pub fn param_decl(&self, index: usize) -> Option<ParamDecl> {
        let mut decl = vivid_sys::VividParamDecl {
            name: std::ptr::null(),
            param_type: vivid_sys::VividParamType::Float,
            min_val: 0.0,
            max_val: 1.0,
            default_val: [0.0; 4],
            string_default: std::ptr::null(),
            enum_count: 0,
            enum_labels: std::ptr::null(),
        };

        let success = unsafe {
            vivid_sys::vivid_operator_get_param_decl(self.ptr, index as i32, &mut decl)
        };

        if !success {
            return None;
        }

        let name = if decl.name.is_null() {
            String::new()
        } else {
            unsafe { CStr::from_ptr(decl.name).to_string_lossy().into_owned() }
        };

        let string_default = if decl.string_default.is_null() {
            None
        } else {
            Some(unsafe { CStr::from_ptr(decl.string_default).to_string_lossy().into_owned() })
        };

        let enum_labels = if decl.enum_labels.is_null() || decl.enum_count <= 0 {
            Vec::new()
        } else {
            let mut labels = Vec::with_capacity(decl.enum_count as usize);
            for i in 0..decl.enum_count {
                unsafe {
                    let label_ptr = *decl.enum_labels.offset(i as isize);
                    if !label_ptr.is_null() {
                        labels.push(CStr::from_ptr(label_ptr).to_string_lossy().into_owned());
                    }
                }
            }
            labels
        };

        Some(ParamDecl {
            name,
            param_type: decl.param_type.into(),
            min_val: decl.min_val,
            max_val: decl.max_val,
            default_val: decl.default_val,
            string_default,
            enum_labels,
        })
    }

    /// Get all parameter declarations
    pub fn params(&self) -> Vec<ParamDecl> {
        (0..self.param_count())
            .filter_map(|i| self.param_decl(i))
            .collect()
    }

    /// Get a parameter value
    ///
    /// Returns `None` if the parameter doesn't exist.
    pub fn get_param(&self, name: &str) -> Option<[f32; 4]> {
        let c_name = CString::new(name).ok()?;
        let mut value = [0.0f32; 4];

        let success = unsafe {
            vivid_sys::vivid_operator_get_param(self.ptr, c_name.as_ptr(), value.as_mut_ptr())
        };

        if success {
            Some(value)
        } else {
            None
        }
    }

    /// Set a parameter value
    ///
    /// Returns `true` if successful.
    pub fn set_param(&mut self, name: &str, value: &[f32; 4]) -> bool {
        let c_name = match CString::new(name) {
            Ok(s) => s,
            Err(_) => return false,
        };

        unsafe { vivid_sys::vivid_operator_set_param(self.ptr, c_name.as_ptr(), value.as_ptr()) }
    }

    /// Set a float parameter
    pub fn set_param_float(&mut self, name: &str, value: f32) -> bool {
        self.set_param(name, &[value, 0.0, 0.0, 0.0])
    }

    /// Set a vec2 parameter
    pub fn set_param_vec2(&mut self, name: &str, x: f32, y: f32) -> bool {
        self.set_param(name, &[x, y, 0.0, 0.0])
    }

    /// Set a vec3 parameter
    pub fn set_param_vec3(&mut self, name: &str, x: f32, y: f32, z: f32) -> bool {
        self.set_param(name, &[x, y, z, 0.0])
    }

    /// Set a vec4/color parameter
    pub fn set_param_vec4(&mut self, name: &str, x: f32, y: f32, z: f32, w: f32) -> bool {
        self.set_param(name, &[x, y, z, w])
    }

    /// Get the number of inputs
    pub fn input_count(&self) -> usize {
        let count = unsafe { vivid_sys::vivid_operator_get_input_count(self.ptr) };
        count.max(0) as usize
    }

    /// Get an input operator by index
    pub fn input(&self, index: usize) -> Option<Operator> {
        let ptr = unsafe { vivid_sys::vivid_operator_get_input(self.ptr, index as i32) };
        if ptr.is_null() {
            None
        } else {
            Some(Operator::from_raw(ptr))
        }
    }

    /// Get an input name by index
    pub fn input_name(&self, index: usize) -> String {
        unsafe {
            let ptr = vivid_sys::vivid_operator_get_input_name(self.ptr, index as i32);
            if ptr.is_null() {
                String::new()
            } else {
                CStr::from_ptr(ptr).to_string_lossy().into_owned()
            }
        }
    }

    /// Get the raw operator pointer
    pub fn as_raw(&self) -> *mut vivid_sys::VividOperator {
        self.ptr
    }
}

// =============================================================================
// Operator Registry
// =============================================================================

/// Information about a registered operator type
#[derive(Debug, Clone)]
pub struct RegistryEntry {
    pub name: String,
    pub category: String,
}

/// Get the number of registered operator types
pub fn registry_count() -> usize {
    let count = unsafe { vivid_sys::vivid_registry_get_operator_count() };
    count.max(0) as usize
}

/// Get a registered operator type by index
pub fn registry_entry(index: usize) -> Option<RegistryEntry> {
    if index >= registry_count() {
        return None;
    }

    let name = unsafe {
        let ptr = vivid_sys::vivid_registry_get_operator_name(index as i32);
        if ptr.is_null() {
            return None;
        }
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    };

    let category = unsafe {
        let ptr = vivid_sys::vivid_registry_get_operator_category(index as i32);
        if ptr.is_null() {
            String::from("Unknown")
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    };

    Some(RegistryEntry { name, category })
}

/// Get all registered operator types
pub fn registry_entries() -> Vec<RegistryEntry> {
    (0..registry_count())
        .filter_map(registry_entry)
        .collect()
}
