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



## Why ImGui can be integrated with wgpu-native v24.0.0.2

ImGui’s WebGPU backend (imgui_impl_wgpu.cpp) supports the current official WebGPU C API used by wgpu-native v24.0.0.2. Recent ImGui versions (post-2025-02 updates) include full compatibility with the new WebGPU header changes—specifically the transition from legacy const char* fields and WGPUShaderModuleWGSLDescriptor to the new WGPUStringView-based descriptors, updated chained structs, and modern shader module creation API. Because of this, the backend correctly constructs WGPUStringView for WGSL source and entry points, uses the modern WGPUShaderSourceWGSL chain type, and matches the device/queue/swapchain interfaces exposed by wgpu-native.

As long as the project:

- Uses a recent ImGui backend revision (matching the new header API),
- Includes the same WebGPU headers that wgpu-native was compiled with, and
- Initializes ImGui_ImplWGPU_Init() with the device, queue, and render target format from wgpu-native,

then ImGui can render into any WebGPU render pass produced by wgpu-native. No API incompatibility remains. Thus, ImGui is fully integrable with wgpu-native v24.0.0.2 without patches.