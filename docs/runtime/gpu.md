# GPU — WebGPU/Dawn Context and Per-Node Textures

## Overview

Vivid uses WebGPU via Dawn for all GPU rendering. The GPU domain consists of:

- **`GpuContext`** — owns the WebGPU device, adapter, surface, queue
- **`RuntimeCore`** — owns per-node `WGPUTexture`/`WGPUTextureView` allocations and runs GPU operator `process_gpu()` calls
- **`NodeState`** — holds GPU resource handles per node

## `GpuContext`

```cpp
bool init(GLFWwindow* window, uint32_t width, uint32_t height);
void resize(uint32_t width, uint32_t height);
void shutdown();
```

### Accessors
```cpp
WGPUInstance instance() const;
WGPUAdapter  adapter() const;
WGPUDevice   device() const;
WGPUQueue    queue() const;
WGPUTextureFormat surface_format() const;
bool surface_supports_copy_src() const;    // for screenshot capture
bool bc_texture_compression_enabled() const; // BC texture compression
uint32_t width() const;
uint32_t height() const;
```

### Frame Lifecycle
```cpp
struct FrameState {
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUCommandEncoder encoder;
};

bool begin_frame(FrameState& frame);    // acquire surface texture, create encoder
bool end_frame(const FrameState& frame); // submit commands, present
void discard_frame(const FrameState& frame); // drop without presenting (window minimized, etc.)
```

`begin_frame()` acquires the current surface texture. The `FrameState::encoder` is the
command encoder for this frame — passed through `gpu_state` to `RuntimeCore::tick()`.

## Per-Node Textures in `NodeState`

Each GPU-domain node gets its own offscreen texture:

```cpp
WGPUTexture      gpu_texture;           // per-node output texture
WGPUTextureView  gpu_texture_view;
uint32_t         gpu_tex_width;
uint32_t         gpu_tex_height;
bool             gpu_tex_inherited;     // true if sharing texture from upstream node

// Input textures (resolved before process_gpu()):
std::vector<uint32_t>    texture_input_port_indices;
std::vector<WGPUTextureView> resolved_tex_inputs;
std::vector<WGPUTexture>    resolved_tex_raw;
std::vector<uint32_t>       resolved_tex_widths;
std::vector<uint32_t>       resolved_tex_heights;

// GPU sink detection:
bool is_gpu_sink;  // true ↔ ≥1 GPU_TEXTURE input, 0 GPU_TEXTURE outputs
```

### Auxiliary Texture Outputs

For GPU nodes with multiple GPU_TEXTURE output ports (e.g. multi-pass effects):
```cpp
std::vector<int32_t>         aux_texture_output_port_indices;
std::vector<WGPUTexture>     aux_gpu_textures;
std::vector<WGPUTextureView> aux_gpu_texture_views;
```

## Texture Allocation

```cpp
// RuntimeCore:
void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                           WGPUTextureFormat format,
                           WGPUTextureUsage extra_usage = 0);
```

Resolution per node: `NodeDef::tex_width/height` if non-zero, otherwise `default_w/h` (1280×720 default).
`extra_usage` is OR'd into `WGPUTextureUsage` — used by the main app to request `CopySrc` for screenshot.

`needs_gpu_realloc_` is set when a topology change adds/removes GPU nodes or resolution changes.
The main loop checks `runtime.needs_gpu_realloc()` and calls `allocate_gpu_textures()` again.

## GPU Sink

The **GPU sink** is the terminal node of the GPU subgraph — the node whose texture is displayed:

```cpp
int find_gpu_sink() const;              // first GPU sink node index (-1 if none)
int find_effective_gpu_sink() const;    // solo-aware: returns soloed node if it has texture output
bool gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const;
WGPUTexture gpu_sink_source_texture(int sink_idx) const;
```

The main loop uses `find_effective_gpu_sink()` to locate the texture to blit to screen.

When the graph UI is visible, the main-window preview presents `Fit` output as a
filled background so the visualization remains visible behind the editor instead
of showing only letterbox checkerboard in the exposed preview area. Dedicated
output windows keep the selected `fit_mode` exactly.

## GPU Operator Process Call

In `RuntimeCore::tick()`, GPU nodes receive `VividGpuContext*` with:
- `device`, `queue`, `encoder` — WebGPU objects
- Input `WGPUTextureView`s (resolved from upstream nodes)
- Output `WGPUTextureView` (the node's own `gpu_texture_view`)
- Node dimensions, time, frame count

Operators render into their output texture. The encoder is shared across all GPU operators
in a single frame and submitted once at `end_frame()`.

## `WgslFilterBase` (GPU operator base class)

Most GPU operators inherit from `WgslFilterBase` (operator_api/wgsl_filter.h).
It handles: pipeline creation, uniform buffer, vertex shader, render pass setup.
Operators only need to provide the fragment shader in a `.wgsl` file.
WGSL shaders hot-reload without a dylib recompile.

## Surface Suppression

During certain macOS window transitions the surface may be transiently invalid.
Calling `begin_frame()` / `end_frame()` (and therefore `wgpuQueueSubmit` with a
surface-bound command buffer) during these windows can trigger a fatal abort
inside wgpu-native that bypasses error scopes.

Known transition sources:
- **Resize** — framebuffer size changes mid-frame
- **Fullscreen** — borderless-fullscreen enter/exit
- **Drag-and-drop tracking** — macOS enters a nested `NSEventTrackingRunLoopMode`
  runloop during `NSCoreDragReceiveMessageProc`; the CFRunLoop timer fires ticks
  inside this nested loop while the surface is unstable

The main loop handles all of these via `surface_settle_frames`: when a transition
is detected, surface presentation is suppressed for a small number of frames.
During suppression the app continues ticking offscreen (runtime, audio, compute
operators) using a standalone command encoder, and resumes surface presentation
once the settle window expires.

`discard_frame()` is the safe escape hatch when a surface-acquired frame must be
abandoned mid-flight (e.g. framebuffer size changed between `begin_frame()` and
the end-of-frame submit).

## Error Handling

GPU shader compilation errors are reported through:
```cpp
// NodeState:
bool        gpu_shader_error;
std::string gpu_shader_error_msg;
```
Unlike `errored`, `gpu_shader_error` does NOT permanently block processing — it clears each tick
to allow recovery when a corrected shader is loaded.

GPU device errors are captured in `GpuContext::last_error_` via the WebGPU uncaptured error callback.

GPU frame analysis reads the texture a node visually represents. Texture-producing
operators analyze their own output texture; GPU sinks such as `video_out` analyze
their resolved input texture because they do not allocate an output texture.
