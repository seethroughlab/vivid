# `app/src/gpu/` — wgpu context + the visuals pipeline

wgpu-native (WebGPU/Metal). Some files are macOS-locked (`.mm`); a cross-platform
abstraction is P3 in the [roadmap](../../../docs/roadmap/poc-to-product.md).

- **`gpu_context.{h,cpp}`** — wgpu instance/adapter/device/queue + the swapchain
  surface; `begin_frame`/`end_frame`/`resize`. Configured at **framebuffer
  (physical)** size (retina-correct).
- **`visual_graph.{h,cpp}`** — the `VisualGraph`: the rewireable op chain
  (`VOp` Plasma/Video/Feedback/Blur/Output), per-node params (`base + modulation`),
  input edges, the active output, and live node thumbnails (`node_view`).
  `run_chain()` renders every node into its own RT; `present_to()` blits the output into
  a surface (the floating preview / the pop-out window), letterboxed per the fit mode —
  the two steps are separate so the preview can be drawn *over* the graph (ADR-0014).
- **`output_format.h`** — the output's FORMAT (pure, unit-tested): the aspect/size preset
  tables + `blit_fit()` (the Fit/Fill/Stretch UV window). The **Output node's params** own
  the output's identity — its `aspect`/`height` size every render target, its `fit` decides
  how it fills a surface, and `preview`/`launch`/`display` decide where it is shown.
  Operators must derive aspect from their real target dims (`ctx->output_width/height`,
  `u.res`) — never a hard-coded display aspect.
- **`op_runtime.*`** — the in-process `OpRegistry` (name→factory) + `OpInstance` +
  `build_descriptor`/`sync_params` (operator-based visuals; wgpu-free, headless).
- **`operator_loader.*` / `loaded_operator.*` / `operator_scan.*`** — the loadable-op
  path: `dlopen` + ABI/descriptor validation (`OperatorLoader`), the `LoadedOperator`
  adapter that mirrors a dylib descriptor onto `OperatorBase`/`GpuProcessable`, and the
  startup scan of the bundle `PlugIns/` (or `$VIVID_OPERATORS_DIR`). **Every visual
  operator is an auto-discovered package dylib** (`app/operators/packages/core-visuals/`);
  there are no compiled-in visual built-ins. Ops flow through `OpRegistry` by descriptor
  name; a built-in (audio only) wins a name clash.
- **`shader_op.*` / `effect_op.*`** — GLSL fullscreen + FBO-effect helpers; the host uses
  `EffectOp` for the final present blit (`visual_graph`).
- **`render_target.*`** — FBO/ping-pong targets.  **`texture_source.*`** — the shared
  image/video source texture (+ `gen_test_pattern`).
- **`video_player.mm`** — AVFoundation clip decode (`video_open/next_frame/play/close`).
- **`gpu_util.h`** — small wgpu helpers (`FrameState`, etc.).

The `VisualGraph` is owned by `App` (shared model); the on-screen viewer is drawn
per-`Window`.
