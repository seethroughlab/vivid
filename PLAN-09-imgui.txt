ImGUI Integration (Blocked)

The vivid-imgui addon is scaffolded but not functional due to WebGPU API version mismatch:
- wgpu-native v24.0.0.2 uses new WebGPU spec with `WGPUStringView`, changed struct names
- ImGUI's imgui_impl_wgpu.cpp (all versions) expects older APIu

**Resolution options (when ready to tackle):**
1. **Patch imgui_impl_wgpu.cpp** for new wgpu-native API (~200-300 lines of changes):
   - `const char*` -> `WGPUStringView` wrapper
   - `WGPUShaderModuleWGSLDescriptor` -> use `WGPUShaderModuleDescriptor.nextInChain`
   - `WGPUImageCopyTexture` -> `WGPUTexelCopyTextureInfo`
   - `WGPUTextureDataLayout` -> `WGPUTexelCopyBufferLayout`
   - `bool` -> `WGPUOptionalBool`
2. **Downgrade wgpu-native** to v22.1.0.x (Sep 2023) - but may break existing runtime code
3. **Wait for ImGUI update** - maintainers will likely update eventually

**Current state:**
- `addons/vivid-imgui/` has API scaffolding in place
- WebGPU access methods added to Context (webgpuDevice, webgpuQueue, webgpuTextureView)
- Example code ready in addon header comments
