# `app/src/gpu/` — wgpu context + the visuals pipeline

wgpu-native (WebGPU/Metal). Some files are macOS-locked (`.mm`); a cross-platform
abstraction is P3 in the [roadmap](../../../docs/roadmap/poc-to-product.md).

- **`gpu_context.{h,cpp}`** — wgpu instance/adapter/device/queue + the swapchain
  surface; `begin_frame`/`end_frame`/`resize`. Configured at **framebuffer
  (physical)** size (retina-correct).
- **`visual_graph.{h,cpp}`** — the `VisualGraph`: the rewireable op chain, per-node params
  (`base + modulation`), input edges, the active output, and live node thumbnails (`node_view`).
  A node's only identity is its **op type name**; it classifies itself from FACTS
  (`VisualNode::is_generator()` = the descriptor declares no texture inputs; `is_output()` /
  `is_video()` = the two host contracts). There is no op enum — one used to exist and mapped
  every unrecognized name to Plasma (ADR-0016 / S2).
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
- **`shader_file_op.*` / `shader_library.*`** — **ADR-0016: a shader FILE is an operator.** A
  `.wgsl`/`.glsl` carries a JSON header declaring its name, its 0..2 texture inputs and its params;
  `ShaderLibrary` scans the three tiers (user > project > bundled, first wins) and registers EACH
  FILE as a type in the same `OpRegistry` the dylibs use, so a shader reaches the Tab chooser,
  `list_operators`, the inspector, wires, mappings and persistence with no special case anywhere.
  `ShaderFileOp` builds its pipeline, its bind group and its uniform packing from ONE declaration
  (`operator_api/shader_meta.h`), so the struct and the bytes cannot drift apart. Editing a file
  hot-reloads it (a body edit recompiles in the live node; a header edit rebuilds its nodes,
  keeping values by name); a broken edit keeps the LAST GOOD pipeline and shows the error on the
  node card. Ten of the visual operators are these files now — see `app/shaders/` and
  [docs/shaders.md](../../../docs/shaders.md).
  **`ShaderDef` is never freed** while the app runs: `ParamBase::name` and the cached descriptors
  are raw `const char*` INTO it.
- **`shader_op.*` / `effect_op.*`** — the OLDER fixed-four-uniform GLSL pass behind the
  `CustomShader` node (`u_warp`/`u_hue`/`u_density`/`u_glow`), plus the FBO-effect helper the host
  uses for the final present blit (`visual_graph`). Not to be confused with `shader_file_op` above:
  this one's uniform contract is hardcoded, which is the thing ADR-0016 exists to undo.
- **`render_target.*`** — FBO/ping-pong targets.  **`texture_source.*`** — the shared
  image/video source texture (+ `gen_test_pattern`).
- **`video_player.mm`** — AVFoundation clip decode (`video_open/next_frame/play/close`).
- **`gpu_util.h`** — small wgpu helpers (`FrameState`, etc.).

The `VisualGraph` is owned by `App` (shared model); the on-screen viewer is drawn
per-`Window`.
