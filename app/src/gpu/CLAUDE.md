# `app/src/gpu/` — wgpu context + the visuals pipeline

wgpu-native (WebGPU/Metal). Some files are macOS-locked (`.mm`); a cross-platform
abstraction is P3 in the [roadmap](../../../docs/roadmap/poc-to-product.md).

- **`gpu_context.{h,cpp}`** — wgpu instance/adapter/device/queue + the swapchain
  surface; `begin_frame`/`end_frame`/`resize`. Configured at **framebuffer
  (physical)** size (retina-correct).
- **`visual_graph.{h,cpp}`** — the `VisualGraph`: the rewireable op chain
  (`VOp` Plasma/Video/Feedback/Blur/Output), per-node params (`base + modulation`),
  input edges, the active output, and live node thumbnails (`node_view`).
- **`shader_op.*` / `effect_op.*`** — generator + FBO-effect ops (feedback, blur).
- **`render_target.*`** — FBO/ping-pong targets.  **`texture_source.*`** — the shared
  image/video source texture (+ `gen_test_pattern`).
- **`video_player.mm`** — AVFoundation clip decode (`video_open/next_frame/play/close`).
- **`gpu_util.h`** — small wgpu helpers (`FrameState`, etc.).

The `VisualGraph` is owned by `App` (shared model); the on-screen viewer is drawn
per-`Window`.
