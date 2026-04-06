# GPU Context

## Purpose

This directory manages the WebGPU device lifecycle, display surface, and GPU utility functions. It provides the rendering infrastructure that GPU operators and the UI's 2D renderer build on.

## Key Files

| File | Role |
|------|------|
| `gpu_context.h/cpp` | `GpuContext` — wraps WebGPU instance, adapter, device, queue, and surface swapchain |
| `fullscreen_blit.h/cpp` | `FullscreenBlit` — blits textures to the display surface with fit/fill/stretch modes |
| `wgsl_header_parser.h/cpp` | Parses self-describing WGSL shader headers (`// @name`, `// @param`) into metadata structs |
| `gpu_frame_analysis.h` | Frame analysis data structures: brightness, contrast, dominant hue, frame hash |
| `metal_interop.h` | macOS Metal interop for Syphon texture sharing |
| `syphon_output.h` | Syphon output streaming (macOS inter-app texture sharing) |
| `screenshot.cpp` | PNG export from GPU textures |

## How It's Organized

**GpuContext** handles the WebGPU lifecycle: create instance → request adapter → request device → configure surface swapchain. It provides `begin_frame()` (acquire surface texture, create command encoder) and `end_frame()` (submit commands, present surface). The implementation uses wgpu-native (Rust-based WebGPU backend) via an adapter layer.

**FullscreenBlit** is a reusable utility for drawing a source texture onto a destination with configurable fit modes. It's used both for compositing the final operator output to the display surface and for thumbnail rendering.

**WGSL Header Parser** reads structured comments from `.wgsl` files in the `filters/` directory to produce operator metadata (name, description, parameters with types and ranges) without requiring C++ registration. This is how standalone shader presets become discoverable operators.

## Relationships

- **Upstream:** `main.cpp` creates and owns `GpuContext`; initializes it before anything else GPU-related
- **Downstream:** `FrameExecutor` dispatches GPU operators via a callback that receives the command encoder; `Renderer2D` and `ThumbnailRenderer` use the GPU context for 2D drawing
- **Operators:** GPU operators receive `VividGpuContext` (defined in `src/operator_api/types.h`) which wraps the device, queue, and per-frame encoder

## See Also

- `docs/runtime/gpu.md` — GPU subsystem design and WebGPU integration details
- `docs/ARCHITECTURE.md` §5.14 — dependency manifest (wgpu-native, GLFW, glfw3webgpu)
