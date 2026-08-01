# Phase 1: Architecture And Ownership Boundaries

Status: done (audited 2026-07-31)

## Verdict

**PASS with follow-ups — no P0/P1 architecture blockers for a first (macOS-only)
release.** The release-critical invariants hold: the `App`/`Window` split is real; the
three canonical models have single live owners; every mutation (UI *and* MCP) funnels
through one main-thread `process_pending` path and the `EditGateway` command sink
(ADR-0025 rule #2 verified clean); the operator ABI is a versioned C surface with no
host-type leakage; platform-specific code is macOS-first, acceptable for a macOS
release. The findings below are **1×P2 + 5×P3** — structural debt and ADR candidates,
none release-blocking.

## Purpose

Verify that core subsystems have clear ownership, dependency direction, and extension points before
the first release hardens them into public precedent.

## User Task

Maintain or extend one representative feature without crossing unclear boundaries between app,
audio, GPU, UI, persistence, packages, CLI, and platform layers.

## Hypothesis

If architecture boundaries are healthy, release fixes can be made locally without destabilizing
unrelated creative workflows.

## Pressure Test

Review module boundaries, `CLAUDE.md` guidance, build targets, public headers, and cross-subsystem
call paths for representative features.

## Scope

- `app/src/app`, `audio`, `gpu`, `ui`, `packages`, `operator_api`, `cli`, `platform`, `midi`, and
  persistence files.
- Module-level `CLAUDE.md` guidance and architecture docs.
- Public headers, operator-facing APIs, and cross-subsystem data models.
- Build targets and scripts that define release-supported components.

Out of scope: broad refactors and style-only cleanup unless they hide ownership or release risk.

## Audit Procedure

1. Draw a subsystem dependency map from source directories, public headers, and build files.
2. Pick three representative flows: project load/playback, visual graph edit/render, and
   package/operator load. Trace ownership through UI, runtime, persistence, and control APIs.
3. Identify duplicated state, bidirectional dependencies, platform leakage, and internals exposed
   through public headers.
4. Compare actual boundaries with `docs/decisions/ADR-0025-cpp17-organization-and-patterns.md`
   and relevant module guidance.
5. Mark each boundary concern as release blocker, follow-up cleanup, or ADR candidate.

## Evidence To Collect

- Dependency map or notes listing allowed and suspicious dependency directions.
- File references for cross-boundary state mutation or public API leaks.
- Three flow traces with entry points and ownership handoffs.
- ADR candidates for decisions that should become durable release policy.

## Deliverables

- Architecture boundary report.
- P0/P1 ownership risks with smallest acceptable fixes.
- ADR/follow-up list for non-blocking structural debt.

## Acceptance Criteria

- Each subsystem has a clear owner role and reason to change.
- Shared data models do not require duplicated truth across UI, persistence, and runtime.
- Platform-specific code is isolated behind platform seams.
- Public APIs and operator-facing types are stable enough for first-release documentation.
- Architectural exceptions are documented as intentional debt or ADR candidates.

## Failure Modes

- UI, runtime, and persistence each maintain incompatible state.
- Platform-specific behavior leaks into portable logic.
- A small feature requires edits across many unrelated modules.
- Public headers expose internals that cannot be supported after release.

## Evidence Log

Method: three parallel source sweeps (dependency map, project-load/visual-graph traces,
operator-load/control-threading), then direct re-read of every cited `file:line` before
it entered this report (shared audit rule: repro/code-ref required). Paths are relative
to `app/src/` unless noted. Build trees (`app/build*/`) excluded.

### A. Subsystem dependency map

**Module inventory.** `app/ audio/ gpu/ ui/ cli/ platform/ midi/ packages/
operator_api/`, plus root `main.cpp persist.* persist_undo.* mapping.h transport.h
signal_shape.h`. Sizes (LOC): audio 15520 · ui 8103 · app 6426 · cli 5910 ·
operator_api 5171 · gpu 4521 · platform 1467 · packages 809 · midi 440.

**Layering (who sits where).**

| Layer | Modules | Note |
|-------|---------|------|
| Leaves (self-only includes) | `operator_api/`, `midi/` | Foundation; safe to depend on |
| Downward-only | `platform/` | No `#include` of any app module; cleanly isolated |
| Mutually-recursive core | `app/ ↔ ui/ ↔ gpu/` | Header-level cycles in both directions |
| Cycle back into core | `audio/ → app/`, `packages/ → app/+gpu/`, `gpu/ ↔ packages/` | |
| Second hub | `cli/` control server | Reaches into audio/gpu/ui/app internals |
| Composition roots | `main.cpp`, `persist.cpp` | Fan out across all modules |

**Cross-module edges of note** (includer → includee, verified `#include` lines):

- `app/ ↔ ui/`: `app/frame.cpp:21-32` (app→ui) vs `ui/node_graph.cpp:2`
  `#include "app/edit_gateway.h"`, `ui/toasts.h:2` `app/log.h`,
  `ui/diagnostics_panel.h:3` `app/runtime_health.h` (ui→app).
- `app/ ↔ gpu/`: `app/app.h:9-12` (app→gpu) vs `gpu/visual_graph.cpp:2`
  `#include "app/crash_guard.h"` (gpu→app).
- `audio/ → app/` (upward): `audio/audio_callback.cpp:3` `#include "app/app.h"` — the RT
  callback pulls the whole `App`. **Intentional**: `device->pUserData` is an `App*`
  (`audio_callback.cpp:16-18`), the documented "audio thread sees only App, never
  Window" seam. `audio/audio_op_runtime.cpp:2` includes `app/crash_guard.h`.
- `gpu/ → ui/`: `gpu/splash.cpp:4` `#include "ui/renderer_2d.h"`.
- `gpu/ ↔ packages/` **header-level cycle**: `gpu/shader_library.h:10`
  `#include "packages/file_watcher.h"` (gpu→packages, in a header so it propagates to
  every `shader_library.h` includer — ui, app, cli) vs `packages/hot_reload_manager.cpp:5-7`
  (packages→gpu).
- `cli/` second hub: `cli/control_handlers_internal.h:8` `#include "audio/vst3_host.h"`
  — the base header for *all* handlers pulls the audio session; handlers reach into
  gpu/ui/app directly.

**Build enforcement.** Single monolithic target `add_executable(vivid ...)`
(`app/CMakeLists.txt:198`, ~125 sources across every module); include dirs wide-open
(`target_include_directories(vivid PRIVATE src src/gpu src/audio ...)`, `:389`). Module
boundaries are **directory convention only — no CMake/link-level enforcement**. The only
truly separately-linked artifacts are the loadable operator MODULEs
(`app/operators/CMakeLists.txt:20`), which link `operator_api` headers, not the app.

**Platform leakage outside `platform/`.** Concentrated in `audio/`: `#ifdef __APPLE__` +
CoreFoundation (`CFStringRef`, `CFErrorCopyDescription`) in `audio/vst3_host_common.h`
(:909 et al.) and `audio/clap_host.h` (:9,120,231,248,324); `audio/plugin_probe.cpp:227`.
None found in gpu/ui/packages/midi/operator_api (comment-only `CFRunLoop` mentions in
`app/frame.cpp:588`, `main.cpp:403`, `cli/control_server.h:48`).

### B. Ownership-flow traces

`App` (`app/app.h`) is a hub of *non-owning* raw pointers; the three canonical models
are `main.cpp` locals wired into `App` at startup.

**B1 — Project load / playback.** Entry `load_session()` (`persist.cpp:865`) →
`session_from_json_scoped(Session*, NodeGraph&)` (`persist.cpp:518`), the *single* loader
that writes both subsystems. Callers: crash-recovery (`main.cpp:441`), Open Project
(`app/project_io.cpp:90`), MCP `load_session` (`cli/control_handlers_project.cpp:394`).
Audio side rebuilt via `session_*` free functions (`rebuild_tracks_from_doc`
`persist.cpp:362`, per-track audio graph `:653-714`); visual side via
`g.chain_load_*` → writes through to `VisualGraph` (`node_graph.cpp:590-594`); mappings
via `g.add_mapping` → `reg_.connect()`. Startup handoffs: `main.cpp:225-227` (VisualGraph
→ App), `:257-260` (VisualGraph → NodeGraph → App), `:282` (`cfg.pUserData=&app`, audio
thread sees only `App*`), `:302` (`session_create`), `:415-416` (`EditGateway` wired).
Transport (`transport.h:13-22`) defaults `playing{true}`; sole clock writer is the audio
thread (`audio_callback.cpp:81 transport->advance()`); mutators are UI
(`app/input_transport.cpp:23,46`) and MCP (`cli/control_handlers_audio.cpp:29-40`,
non-undoable per `edit_methods.cpp:91-92`).

**B2 — Visual graph edit / render.** `VisualGraph` (`gpu/visual_graph.h:93`) owns node
identity/params/edges. UI edits (`ui/node_graph.cpp on_down/move/up`) mutate the live
graph directly and record undo via `note_edit_()` → `EditGateway`. MCP edits
(`cli/control_handlers_visuals.cpp:41 add_node`, `:66 connect_nodes`,
`:109 set_node_param`) mutate the *same* live objects; undo is captured **centrally** at
the dispatch chokepoint (`control_server.cpp:85-87` via the `edit_methods.cpp` table), not
per-handler. Render (per frame, `app/frame.cpp`): `publish_bridge_sources` (:706) →
`apply_audio_param_mappings` (:707) → `graph.apply_params()` (:378) →
`vgraph.run_chain(encoder, tsec)` (:721) → present.

**B3 — Package / operator load.** ABI `VIVID_OPERATOR_ABI_VERSION 14u`
(`operator_api/types.h:27`), floor `MIN_LOADABLE 11u` (`:37`); loadable range
`[11,14]` enforced at `gpu/operator_loader.cpp:145`. `OperatorLoader`
(`dlopen RTLD_NOW|RTLD_LOCAL`, `:124`; symbol+ABI+descriptor check `:122-221`) →
`operator_scan.cpp load_and_register_operator_ex()` → `OpRegistry` (`gpu/op_runtime.h:60`,
factory returning `std::unique_ptr<OperatorBase>`). Startup scan `main.cpp:154-163`;
runtime install via `packages/package_manager.cpp install_package()` (compiles with
`clang++` directly, `package_compiler.*`) → MCP handler
`cli/control_handlers_packages.cpp:232 reload_operator_package` → registers into
`App::op_registry` (`app.h:46`) + `file_drops.rebuild()`. `VisualGraph` holds
`&app.op_registry` (`main.cpp:225`), so a newly-registered op is immediately spawnable.

### C. Duplicated-state analysis

- **Index-coupled (structural risk):** `NodeGraph::op_pos_` (visual-node layout) is a
  `std::vector` documented "parallel to `vg_->nodes()`" (`node_graph.h:217-218`), aligned
  to `VisualGraph::nodes_` **by array index**, kept in step by `sync_op_pos()`
  (`node_graph.cpp:145-155`) called after every add/remove/load/drag. Correctness depends
  on the two never diverging in order/length; no invariant assert/test guards it. →
  Finding P2-01.
- **Regenerated each frame (low risk):** mapping `sources_` (`mapping.h:143`, refilled by
  `publish_bridge_sources` every frame); `Window::preview.*` (`window.h:79-83`, reconciled
  at `frame.cpp:722,728` — "params are the truth, buttons just write to them").
- **Not duplicated:** control-server view shares live pointers (`ControlCtx`,
  `control_server.h:20-30`) — no shadow model; audio-node editor rebuilt each frame from
  `Session` (`window.h:153-155`); window geometry is a single live owner + a persisted
  projection (deliberately stripped from the undo document, `edit_gateway.cpp:21`).

### D. Control-mutation threading (ADR-0025 rule #2) — PASS

Producer (HTTP thread) queues `Pending{method,body,promise}` under `mtx_`
(`control_server.cpp:41`) and blocks on the future; consumer drains on the main thread
(`process_pending`, `:71-90`) from `app/frame.cpp:619`. A grep for
`std::thread`/`std::async`/`detach` across `cli/*.cpp` returns exactly one hit — the
server's own listen thread (`control_server.cpp:49`). No handler mutates off-main; even
registry-mutating handlers run inside `process_pending`. Clean.

### E. Findings

#### P2-01: Visual-node model and layout are two live owners coupled by array index

- Surface: code — `ui/NodeGraph` ↔ `gpu/VisualGraph`
- Impact: `op_pos_` (positions) and `vg_->nodes()` (identity/params/edges) are aligned
  only by parallel-array index. A future edit path that reorders/removes on one side
  without calling `sync_op_pos()` would silently mis-associate every node's position with
  the wrong node — a hidden-broken-state class bug, and it feeds persistence
  (`get_op()` reads x/y from `op_pos_`, id/params from `VisualGraph`, `node_graph.cpp:441`).
- Evidence: `app/src/ui/node_graph.h:217-218`; `app/src/ui/node_graph.cpp:145-155`
  (`sync_op_pos`), called at `:168,348,778,931,950,1004`.
- Smallest acceptable fix: add a debug-build invariant asserting
  `op_pos_.size()==vg_->nodes().size()` (and, cheaply, id-parity) at the end of
  `sync_op_pos()`, plus a headless test that adds/removes/reorders and checks alignment.
  No structural change required for release.
- Owner/status: Unassigned | not a release blocker (guarded in practice today)

#### P3-01: Module dependency cycles with no build-level layering enforcement

- Surface: code — `app/audio/gpu/ui/cli/packages` include graph + `app/CMakeLists.txt`
- Impact: `app↔ui↔gpu` mutual header cycles, `gpu↔packages` header cycle, and a single
  monolithic `vivid` target mean a small change can pull in unrelated modules and there is
  no compiler-enforced layering — the "small feature edits many modules" failure mode.
  Maintainability, not correctness.
- Evidence: cycles listed in §A; `app/src/gpu/shader_library.h:10`;
  `app/CMakeLists.txt:198,389` (one target, wide-open includes).
- Smallest acceptable fix: none for release. Codify allowed edges + optional CMake
  object-library split as **ADR-0043** (drafted). Break specific cycles opportunistically
  when a file is next touched (per ADR-0025 Decision #7).
- Owner/status: Unassigned | ADR-0043 (proposed)

#### P3-02: macOS/CoreFoundation code leaks into portable `audio/` headers

- Surface: code — `audio/vst3_host_common.h`, `audio/clap_host.h`
- Impact: violates the "platform-specific code isolated behind platform seams" criterion.
  Zero user impact for a macOS-first release; becomes real work at first cross-platform
  port.
- Evidence: `app/src/audio/vst3_host_common.h:909` (`CFStringRef`/`CFErrorCopyDescription`)
  + `:35,511,620,881,1156,1210`; `app/src/audio/clap_host.h:9,120,231,248,324`.
- Smallest acceptable fix: **waive for first release** with this documented rationale;
  isolate behind a platform seam when cross-platform work begins.
- Owner/status: Unassigned | waived (macOS-first release)

#### P3-03: Operator ABI is pinned to the webgpu-native handle layout

- Surface: code — `operator_api/gpu_operator.h`
- Impact: `VividGpuContext` embeds `WGPUDevice/Queue/CommandEncoder/Texture(View)/Buffer`
  by value. These are the public `<webgpu/webgpu.h>` C API (not Vivid types, so no
  internal-type leak), but a future migration off webgpu-native — or a WGPU header ABI
  break — would be a **non-additive** operator-ABI break, orphaning installed operators.
  An external-dependency coupling the first release quietly commits to.
- Evidence: `app/src/operator_api/gpu_operator.h:5,35-72`.
- Smallest acceptable fix: document as intentional external coupling + first-release
  breakage policy in **ADR-0044** (drafted). No code change.
- Owner/status: Unassigned | ADR-0044 (proposed)

#### P3-04: The one genuinely public module (`operator_api/`) has no module guide

- Surface: docs — `app/src/operator_api/`
- Impact: every other module has a `CLAUDE.md`; the operator ABI — the only surface
  third-party authors compile against — does not. The additive-only doctrine lives only
  inline (`types.h:11-37`) + `docs/operator-api/abi-changelog.md`, easy for a contributor
  to miss and break the ABI.
- Evidence: no `app/src/operator_api/CLAUDE.md` (confirmed absent); doctrine at
  `app/src/operator_api/types.h:11-37`.
- Smallest acceptable fix: add a short `operator_api/CLAUDE.md` pointing at the inline
  contract, the changelog, and the append-only rule. (Done in this pass.)
- Owner/status: Fixed (this pass)

#### P3-05: Architecture doc drift

- Surface: docs — `app/ARCHITECTURE.md`
- Impact: `ARCHITECTURE.md` §3 diagram labels `main.cpp` "~150 LOC (init + wiring +
  teardown only)"; it is actually 479 LOC — a newcomer trusts a stale figure. (ADR-0025's
  "4069 LOC" for `vst3_host.cpp` is an explicitly *dated* 2026-07-25 snapshot, not current
  drift; current is 4102 — left as historical record, noted here only.)
- Evidence: `app/ARCHITECTURE.md:51`; `wc -l app/src/main.cpp` = 479.
- Smallest acceptable fix: correct the number. (Done in this pass.)
- Owner/status: Fixed (this pass)

## Open Questions (answered)

- **Which headers are public operator API versus internal app API?** *Public, versioned:*
  `operator_api/operator.h` + `operator_api/types.h` (the C ABI, v14/floor v11) — the only
  surface third parties compile against; clean, no host-type leakage (one external caveat,
  P3-03). *Internal-only:* `audio/vst3_host.h` (returns raw `void*` VST3 SDK controllers,
  `:190-191`) and `cli/control_server.h` `ControlCtx` (live-app pointer bag, `:20-30`) —
  leaky, but acceptable **because they are not documented public surfaces**. First-release
  docs should cover only `operator_api/`.
- **Which subsystem owns the canonical project model at runtime?** Three single-owner live
  models, all `main.cpp` locals reached through non-owning `App` pointers: audio =
  `vivid::session::Session*` (opaque C API); visuals = `gpu/VisualGraph`; bridge + visual
  layout = `ui/NodeGraph` (`MappingRegistry` + `op_pos_`). `persist.cpp` is the single
  place that writes both audio and visual truth. There is no competing shadow model.
- **What architectural debt is acceptable for first release (not yet public precedent)?**
  The module cycles + monolithic build target (P3-01) and the macOS leakage in `audio/`
  (P3-02) are acceptable: they are internal structure with no user-visible or public-API
  effect for a macOS-first release, and both graduate to tracked follow-ups (ADR-0043 /
  waiver). The one surface that *is* public precedent — the operator ABI — is clean and
  now documented (P3-03 ADR-0044, P3-04 CLAUDE.md).

## Follow-Up Plans

- **ADR-0043 — Module layering & dependency direction** (proposed):
  `docs/decisions/ADR-0043-module-layering-and-dependency-direction.md`. Codifies allowed
  edges, the app/ui/gpu core, downward-only leaves, and whether to enforce via CMake
  object libraries. Covers P3-01.
- **ADR-0044 — Operator ABI is pinned to webgpu-native handles** (proposed):
  `docs/decisions/ADR-0044-operator-abi-webgpu-native-coupling.md`. Records the external
  coupling + first-release breakage policy. Covers P3-03.
- **P2-01** (`op_pos_`/`nodes_` invariant assert + test) — lands as its own gated PR when
  `ui/node_graph.cpp` is next touched.
- Handoff to later phases: Phase 2 inherits the audio-thread seam
  (`device->pUserData == App*`, single-writer transport); Phase 4 inherits the
  `persist.cpp` single-loader + the undo canonical-projection boundary; Phase 5 inherits
  the operator ABI surface (v14/floor v11) and the `clang++`-direct package compiler.
