# Phase 3: Rendering, UI, And GPU Runtime

Status: done (audited 2026-07-31)

## Verdict

**PASS with follow-ups — no P0/P1.** The render path is well-owned (RAII `GpuContext`,
explicit `VisualGraph::shutdown`, per-frame surface texture released each frame), surface-
unavailable and WGPU device-loss are *contained* (drop the frame / freeze cleanly, never
crash or hang), shader-compile failure keeps the last-good pipeline and badges the node, the
visual-op path is CrashGuard-attributed, and the instanced bind-group UAF fix is live. The
strongest result: **draw and hit-test geometry share one source on every primary UI surface**
(acceptance criterion met across all six). Findings are 3×P2 + 4×P3 — GPU device-loss has no
recovery path, and several failure signals are incomplete (startup scan errors stderr-only,
blank-vs-empty not distinguished, diagnostics/health omit shader errors).

## Purpose

Verify that rendering, UI drawing, GPU operator execution, previews, and visual graph behavior are
stable enough for first-release creative use.

## User Task

Build and manipulate a visual graph while the UI remains responsive, the preview reflects state, and
GPU failures stay contained.

## Hypothesis

If rendering boundaries are healthy, users can iterate visually without crashes, blank output, or
state divergence between graph, preview, and saved project.

## Pressure Test

Audit render loop ownership, GPU context lifetime, shader/operator loading, visual graph topology,
UI layout/draw/hit-test coupling, output preview behavior, and diagnostics.

## Scope

- Frame loop, GPU context, shader/operator loading, visual graph runtime, render targets, output
  preview, UI renderer, layout primitives, hit testing, diagnostics, and performance stats.
- GPU and UI failure reporting through both visible UI and control APIs.
- Interaction between visual graph state, persisted project state, and preview output.

Out of scope: aesthetic redesign, shader art direction, or non-release experimental visuals.

## Audit Procedure

1. Trace one frame from app tick through UI draw, visual graph render, GPU presentation, and
   diagnostics.
2. Trace shader/operator load success and failure, including resource lifetime and visible errors.
3. Compare layout source, drawing, and hit testing for each primary UI surface.
4. Exercise visual graph edits while playback or preview is active and inspect state consistency.
5. Review diagnostics for blank output, shader failure, GPU device loss, and frame-time spikes.

## Evidence To Collect

- Frame/render path trace with ownership notes.
- GPU resource lifetime notes for textures, render targets, shaders, and operators.
- UI geometry inventory: layout source, draw function, hit-test function, and drift risks.
- Screenshots or logs for shader failure and blank-output scenarios.

## Deliverables

- Rendering and UI runtime risk report.
- Layout/draw/hit-test mismatch list.
- GPU failure containment and diagnostics recommendations.

## Acceptance Criteria

- Draw and hit-test geometry share the same layout source.
- GPU resources have clear lifetimes and failure paths.
- Shader/operator errors surface in UI and control APIs.
- Visual graph state, preview output, and persisted project state agree.
- Performance diagnostics identify frame-time or GPU failure causes.

## Failure Modes

- Rendering errors become silent blank previews.
- UI layout and hit tests drift apart.
- GPU resource lifetime depends on incidental object order.
- Visual graph edits invalidate preview or persistence state differently.

## Evidence Log

Method: three source sweeps (frame/render trace + GPU lifetimes; shader/op failure
surfacing; UI layout/draw/hit-test parity). Paths relative to `app/src/`.

### A. Frame / render trace + GPU ownership

`run_frame_loop` (`app/frame.cpp:573`) builds a `tick`: `control.process_pending` → async
polls (plugin loads, plugin scan, hot-reload, shader reloads) → resize check → health
collect → bridge publish → **`gpu.begin_frame`** (`frame.cpp:714`, gates the whole render
block) → `clear_pass` → `vgraph.run_chain` (`visual_graph.cpp:300`) → UI draw + two
`Renderer2D` flush passes → `vgraph.present_to` (preview blit) → overlays + perf HUD →
**`gpu.end_frame`** (MSAA-resolve → submit → `wgpuSurfacePresent` → release per-frame
surface view/texture). Ownership: `GpuContext`/`VisualGraph`/`Renderer2D` are `main.cpp`
locals; `GpuContext` (non-copyable) owns device/queue/surface + the MSAA target with an
order-explicit `shutdown()`. `RenderTarget` uses manual `init()`/`release()` (no destructor)
but cleanup is explicit and correct — resize reallocates all RTs on grow *and* shrink
(`visual_graph.cpp:154-168`); no incidental-destruction-order hazard found. The instanced
bind-group per-frame pool + one-frame retire (the prior UAF fix) is present and live
(`app/operators/packages/vivid-3d/render_3d.cpp:1641-1647,1964`).

### B. GPU failure containment

- **Surface unavailable:** `begin_frame` returns false (releases the texture, warns once),
  the render block is skipped, loop keeps spinning until acquire succeeds
  (`gpu_context.cpp:393-412`). Contained.
- **Shader/pipeline compile failure:** `ShaderFileOp` keeps the **last-good** pipeline +
  stores `error_`, initial failure falls back to a black passthrough
  (`shader_file_op.cpp:164-233`); surfaced to the node badge via `VisualNode::error()` and
  to a toast/log (`frame.cpp:563-568`).
- **WGPU device loss:** a device-lost callback sets `device_lost_` (`gpu_context.cpp:124-134`);
  `begin_frame`/`end_frame` then early-out, so the loop runs but renders/presents nothing —
  contained (no crash/hang) but **terminal: nothing ever resets `device_lost_` or recreates
  the device/surface** (→ P2-01).
- **GPU validation/OOM:** uncaptured-error callback records `last_error_`/`error_count_`
  (`gpu_context.cpp:137-147`) → `HealthSnapshot` amber dot; process-wide, unattributed to a
  node (→ P3-02).
- **Visual-op crash:** `run_chain` wraps `process_gpu` in `CrashGuard(type_name)`
  (`visual_graph.cpp:463-466`) — attribution only; the process still dies, but on relaunch
  the crash is attributed and the op is **quarantined by type_name** (unlike hosted plugins,
  which quarantine can't yet catch — Phase 2 P0-01 / PR #190). This is the designed
  ADR-0018 model and works for operators.

### C. Failure-surfacing (ADR-0019) — inventory + gaps

Node error state has a single source (`VisualNode::error()`, `visual_graph.cpp:57-66`),
drawn as a red border + "!" chip + over-thumbnail note, and exposed by MCP `get_ops.broken_ops`
(names the op + error). Loadable-operator failures carry named codes
(`operator_loader.cpp`: `dlopen_failed`/`abi_mismatch`/…). Gaps:
- The **diagnostics panel** and **`get_health`** show missing-ops + GPU state but **omit
  shader-compile and compiled-op reload errors** (badge/`broken_ops`-only) (→ P3-01).
- **Startup** dylib-scan (`operator_scan.cpp:22-37`) and shader-scan (`main.cpp:170-174`)
  failures go to **stderr only** — a package failing to load at launch is silent in-app
  (its nodes still badge missing, but the *reason* never toasts/logs) (→ P2-02).
- **Blank-output vs no-output-by-design is not distinguished at render time or in
  `HealthSnapshot`** — a broken chain rendering black looks identical to an empty-by-design
  canvas; only a CPU heuristic (`is_blank`, `image_analysis_tools.cpp:91-97`) reachable via
  MCP polling tells them apart, and it flags legitimately-empty output as `fail` (→ P2-03).

### D. UI layout / draw / hit-test parity — CLEAN

All six primary surfaces route **draw and hit-test through one shared geometry source** —
the acceptance criterion holds everywhere: the DAW/session grid + transport via `ui/layout.h`
(`session_view.cpp` draw ↔ `input_clipgrid.cpp`/`input_transport.cpp` hit); the visual and
audio node graphs via `CardPorts`/`NodeView` (`ui/node_canvas.h`, ADR-0023); the clip editor
via its own `xb()/yp()/bw()/rh()` helpers; the mapping overview via `ov_geom`/`ov_row`; the
shader browser via `shader_view_geom/row`. Three surfaces document the shared-source rule
in-header. One micro-drift only (→ P3-03).

### E. Findings

#### P2-01: WGPU device loss has no recovery path

- Surface: `gpu/gpu_context.cpp`
- Impact: device loss (GPU reset, driver crash, sleep/wake) is *contained* — the loop keeps
  running without crashing — but nothing resets `device_lost_` or recreates the
  device/surface, so the window is **frozen (blank, unresponsive to render) until the app is
  restarted**. Unsaved work since the last 15 s autosave is at the user's mercy of a restart.
  Rare on macOS, but a hard dead-end when it happens.
- Evidence: `gpu_context.cpp:124-134` (callback sets `device_lost_`), `:390,461-463`
  (permanent early-out); only read elsewhere at `runtime_health_collect.cpp:21`.
- Smallest acceptable fix: on device loss, surface a clear modal/toast ("GPU device lost —
  please restart") so the frozen state is explained; a full device/surface recreate is a
  larger follow-up. Owner/status: Unassigned | P2.

#### P2-02: Startup operator/shader scan failures are stderr-only (silent in-app)

- Surface: `gpu/operator_scan.cpp`, `main.cpp:170-174`
- Impact: a package or shader that fails to load *at launch* logs only to stderr — never
  toasts, never enters the in-app log; the user sees nodes badged "missing" with no stated
  reason. Violates ADR-0019 "nothing fails silently" for the launch path.
- Smallest acceptable fix: route startup scan errors through `app.log` once the logger is up
  (they currently run before/outside the frame loop). Owner/status: Unassigned | P2.

#### P2-03: Blank-output vs no-output-by-design is indistinguishable in-app

- Surface: `gpu/visual_graph.cpp` (run_chain/present_to), `HealthSnapshot`
- Impact: a genuine render failure (broken chain → black) and a legitimately-empty graph both
  present as black; the runtime/health path can't tell them apart and there is no in-app
  signal. Only an agent polling `nonblank_visual_output`/`analyze_frame` gets a heuristic
  verdict — which also reports empty-by-design as `fail`. Answers this phase's Open Question
  #2 (the distinction is *not* made). Owner/status: Unassigned | P2.
- Smallest acceptable fix: a `HealthSnapshot`/diagnostics signal that distinguishes "Output
  has no feed" (structural) from "Output feed rendered but is blank" (heuristic), and treat
  empty-by-design as OK not `fail`.

#### P3-01: Diagnostics panel + `get_health` omit shader/reload errors

Shader-compile and compiled-op reload errors reach only the node badge + `get_ops.broken_ops`,
not the diagnostics panel or `get_health` (which shows GPU + a nameless missing-ops count).
Fix: fold `broken_ops` into both. Owner/status: Unassigned | P3.

#### P3-02: Non-crashing GPU-op misbehavior has no attributed failure surface

A GPU validation error is a process-wide nameless counter → amber dot, not attributed to the
offending node; a non-crashing op that emits garbage/black has no dedicated signal. Fix:
per-node GPU error attribution where feasible. Owner/status: Unassigned | P3.

#### P3-03: Clip-editor note-width draw/hit micro-drift

Draw clamps note width to a 2px minimum (`clip_editor.cpp:859,867`); the hit-test uses the
true width (`:265`), so a sub-2px note is clickable on a smaller area than it paints. Fix:
apply the same 2px floor in `hit_note`. Owner/status: Unassigned | P3.

#### P3-04: Mid-session output-resize shrink handling

The core `ui`/`frame`/`window` resize paths are correct on shrink (RTs reallocate on grow
*and* shrink; `present_to` is UV-based + clamped). Two residual spots: the video recorder
latches its dims and crops-top-left / drops frames when the output resolution changes
mid-record (`app/video_recorder.cpp:64-82`); and the likely live-canvas "grid clips to
top-left" symptom is an **external** feedback/history-texture operator that grows-only and
doesn't shrink (`visual_graph.cpp:166-167` documents the rebuild-on-size-change contract;
the ops live in runtime packages, not `app/src/`). Fix: video-recorder shrink handling +
audit operator history-texture rebuild. Owner/status: Unassigned | P3.

## Open Questions (answered)

- **What visual-output correctness checks are required for release examples?** Today only
  the MCP `nonblank_visual_output` heuristic exists, and it mis-reports empty-by-design as
  `fail`. Recommend release examples each assert a non-blank rendered frame via a corrected
  check that treats "no Output feed" and "empty by design" distinctly (ties to P2-03).
- **How should the app distinguish no-output-by-design from render failure?** It currently
  does **not** (P2-03) — both are black, with only a CPU heuristic via MCP. Recommend a
  `HealthSnapshot` field: "Output unfed" (structural, benign if intentional) vs "Output fed
  but blank" (candidate failure).
- **Which GPU errors trigger quarantine/toast/diagnostics/recovery?** Today: an operator
  *crash* → attribution + quarantine-by-type on relaunch (works for native ops; not hosted
  plugins — Phase 2 P0-01). A GPU *validation* error → amber health dot only (unattributed,
  P3-02). Device *loss* → contained but no recovery/notice (P2-01). Shader *compile* error →
  node badge + toast + last-good pipeline (good). Recommend: device-loss gets a user-facing
  notice; validation errors get per-node attribution.

## Follow-Up Plans

- P2 fixes as their own gated PRs: device-loss user notice (P2-01), route startup scan
  errors through `app.log` (P2-02), blank-vs-empty health signal (P2-03).
- P3 cleanups: fold `broken_ops` into diagnostics + `get_health` (P3-01), per-node GPU error
  attribution (P3-02), clip-editor hit-test 2px floor (P3-03), video-recorder shrink +
  operator history-texture rebuild audit (P3-04).
- Cross-refs: the visual-op CrashGuard model (attribution + quarantine-by-type) is the
  contrast that makes Phase 2's hosted-plugin P0-01 (PR #190) distinct; the layout parity
  result (§D) means UI geometry needs no release cleanup.
