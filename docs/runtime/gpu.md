# GPU — WebGPU/Dawn Context and Per-Node Textures

## Overview

Vivid uses WebGPU via Dawn for all GPU rendering. The GPU domain consists of:

- **`GpuContext`** — owns the WebGPU device, adapter, surface, queue
- **`Scheduler`** — owns per-node `WGPUTexture`/`WGPUTextureView` allocations and runs GPU operator `process_gpu()` calls
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
command encoder for this frame — passed through `gpu_state` to `Scheduler::tick()`.

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
// Scheduler:
void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                           WGPUTextureFormat format,
                           WGPUTextureUsage extra_usage = 0);
```

Resolution per node: `NodeDef::tex_width/height` if non-zero, otherwise `default_w/h` (800×600 default).
`extra_usage` is OR'd into `WGPUTextureUsage` — used by the main app to request `CopySrc` for screenshot.

`needs_gpu_realloc_` is set when a topology change adds/removes GPU nodes or resolution changes.
The main loop checks `scheduler.needs_gpu_realloc()` and calls `allocate_gpu_textures()` again.

## GPU Sink

The **GPU sink** is the terminal node of the GPU subgraph — the node whose texture is displayed:

```cpp
int find_gpu_sink() const;              // first GPU sink node index (-1 if none)
int find_effective_gpu_sink() const;    // solo-aware: returns soloed node if it has texture output
bool gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const;
WGPUTexture gpu_sink_source_texture(int sink_idx) const;
```

The main loop uses `find_effective_gpu_sink()` to locate the texture to blit to screen.

## GPU Operator Process Call

In `Scheduler::tick()`, GPU nodes receive `VividGpuContext*` with:
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
