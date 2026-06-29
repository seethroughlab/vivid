# PoC → Product: Readiness Assessment & Roadmap

Companion to [ADR-0011](../decisions/ADR-0011-poc-to-product-architecture.md). Target (ratified): an
**extensible, cross-platform-capable platform**. Trunk decision (A/B/C) pending — see ADR-0011;
recommendation is **B (port the product layer onto vivid-classic's runtime)**.

Date: 2026-06-29 · Scope: `app/` (branch `poc-cpp-prototype`), benchmarked against the `vivid-classic`
branch.

---

## 1. Assessment

### 1a. The PoC today (audit)

`app/` is ~8,100 LOC across 43 files in clean module dirs (`audio` 1.4k, `gpu` 1.0k, `ui` 1.3k, `cli`
0.4k, `platform`, `midi`, root 1.8k). What's good and what blocks productization:

| Area | State | Evidence |
|---|---|---|
| Module separation | OK | `audio`/`gpu`/`ui`/`cli`/`platform`/`midi`; subsystem APIs (`vst3_host.h`, `node_graph.h`, `mapping.h`, `persist`) are decent seams |
| **`main.cpp` god file** | **Critical** | **1,213 LOC, 15+ responsibilities** (globals, layout consts, audio callback, all `draw_*`, all GLFW input callbacks, render loop, MCP wiring, persistence keybinds); 30+ globals; draw fns touch them directly |
| **Tests / CI** | **Critical (none)** | 0 test files, no framework, no `.github/workflows`, no sanitizers. Untested: thread-safe edit patterns, mapping math, persistence round-trip, control-server dispatch |
| **Error handling** | **High** | Silent failures (video open, effect add); MCP `control_server` handlers pass **unvalidated** track/scene/device indices to the C API; crash vectors: null-session-after-failed-create, plugin-window double-free, video nullptr |
| Concurrency | Good | generation-counter + mutex + try_lock for clips/FX; lock-free SPSC param queue + transport atomics |
| Conventions | Mixed | consistent intra-module namespaces but `vivid` vs `vivid_poc` across the repo; `#pragma once` everywhere; header-only mega-files (`vst3_host_common.h` 1.1k) |
| **Docs (in `app/`)** | **Medium (none)** | no README/ARCHITECTURE/BUILD, no thread-safety guide, ~2% comments in `main.cpp`; header doc-comments are good where present |
| Deps | Mostly pinned | all FetchContent pinned except **`glfw3webgpu` tracks `main`**; no lockfile |
| **Platform lock** | **Medium** | ~20% macOS-locked: `platform/macos_frame_timer.cpp` (CFRunLoop), `gpu/video_player.mm` (AVFoundation), `audio/vst3_plugin_window.mm` (Cocoa), Metal-only wgpu path, `MACOSX_BUNDLE`; **no conditional compilation / abstraction** |

### 1b. vivid-classic's discipline (the benchmark)

Classic is ~4,500 tracked files with the engineering scaffolding the PoC lacks — and most of it exists
*to enable extensibility + cross-platform*, which is exactly the chosen target:

- **Enforced layering**: `operator_api` (public C ABI) → `common` → `runtime{core,graph,operators,
  packages,control,audio,gpu,platform}` → `ui`; no circular deps; cross-cadence only via an explicit
  `AudioFrameBridge`.
- **Plugin/operator contract**: operators are dylibs behind an `extern "C"` ABI (`VIVID_OPERATOR_ABI_VERSION
  = 10`), with **descriptor validation by named error codes** and stale-dylib rejection.
- **Codegen + hot-reload + packages**: `tools/operator_codegen` (source → registration/uniform headers
  via tree-sitter), file-watch hot-reload with param-state preservation + crash guard, source-only
  package system (manifest, install/build/rebuild, lockfile).
- **Tests partitioned by cost**: 314 test files in tiers 10–50 (headless core everywhere; GPU/audio/UI/
  package tiers on demand) on a headless `vivid_runtime_testlib`; opt-in benchmarks with regression gates.
- **CI + production gate**: `.github/workflows` (smoke, PR gate, release/notarize, version-guard, pages,
  MCP evals); tiered `production_gate_{core,gui,env,soak}` emitting JUnit/JSON; **ASan/UBSan/TSan** opt-in.
- **Docs culture**: 180+ md — `PRD.md`, `ARCHITECTURE.md`, `ARCHITECTURE-GUARDRAILS.md`, per-subsystem
  `docs/runtime/*.md`, a testing strategy, and **`CLAUDE.md` at every directory**; plans are archived
  (never deleted) as decision history.
- **Conventions as architecture**: named error codes (not stringly/silent), data-driven `ui/style`
  theme, semantic param metadata for LLM/MCP, ABI version checks — consistency enforced by structure, not
  lint.

### 1c. Verdict

Architecture and product are sound; the **engineering base is a prototype's**. The god file, the absence
of tests/CI/sanitizers, the unvalidated control surface, and the thin docs are the disqualifiers — and
the extensibility/cross-platform machinery the target requires is entirely absent. Because that machinery
*is* classic's, the roadmap is organized around the trunk decision in ADR-0011.

---

## 2. Carry-forward vs. rework

| Carries forward (validated) | Reworked / replaced |
|---|---|
| Mapping registry model (source→shape→dest, curve/range/invert) | `main.cpp` (decompose into App + subsystems) |
| Master transport + SPSC atomics | Fixed `VisualGraph` (becomes operators under B) |
| Thread-safe edit discipline (gen-counter / try_lock / SPSC) | macOS-locked `.mm` + CFRunLoop (behind platform seams) |
| MCP control-server *shape* (queue → main-thread dispatch) | Ad-hoc error handling (→ named codes + validation) |
| `ui_style` palette + widgets; the two-surface + bridge UX | No-ABI, fixed feature set (→ operator/ABI contract) |

---

## 3. Roadmap (phased)

Horizon is multi-month / rebuild-scale. Phrasing assumes trunk **B/C** ("port/reuse from classic"); under
**A** each item means "build from classic's design."

### P0 — Engineering hygiene (trunk-agnostic; can start now)
- **Decompose `main.cpp`** into an `App` struct + subsystems: `AppShell` (window/GPU/lifecycle),
  `InputRouter` (the GLFW callbacks + hit dispatch), `SessionUI`/`VisualsUI` controllers, `FrameLoop`
  (the tick). Eliminate the 30+ globals (own them in `App`). Target `main.cpp` < ~150 LOC.
- **Test harness + CI**: a headless test lib (no window/GPU/audio), `ctest`, GitHub Actions. First tests:
  mapping-registry math, `session_to_json`/`from_json` round-trip, control-server dispatch + **index
  validation**, the clip/FX generation-counter edit path.
- **Sanitizers**: `-DVIVID_SANITIZE` (ASan+UBSan) and `-DVIVID_SANITIZE_THREAD` (TSan) build options —
  highest leverage on the audio-thread code.
- **Robustness**: introduce **named error codes**; bounds-check every index in the `session_*` C API and
  `control_server` handlers (return a code, never crash); fix the known vectors (null session, plugin
  window double-free, video nullptr).
- **Docs**: `app/ARCHITECTURE.md` (thread model + two-surface/bridge), `README`/`BUILD`, per-dir
  `CLAUDE.md`, an audio-thread-safety guide. Pin `glfw3webgpu`.

### P1 — Trunk + layering + operator/ABI contract  *(gated on A/B/C)*
- Ratify A/B/C. **Spike first**: prove the PoC's product layer maps onto classic's runtime (a Plasma op +
  the bridge + a clip launching) before committing.
- Establish the enforced layering and the `extern "C"` operator ABI + descriptor validation (named codes).
  Under B: adopt classic's `operator_api` + graph compiler; express the PoC's visuals ops and audio nodes
  as operators.

### P2 — Codegen + hot-reload + packages
- `operator_codegen` (source → registration/uniform headers), file-watch hot-reload (param-state
  preservation + crash guard), source-only package system (manifest, install/build/rebuild, lockfile).
  Port from classic under B.

### P3 — Cross-platform seams
- Abstract the macOS-locked surface behind interfaces: frame loop, audio device, plugin-window hosting,
  video, GPU/Metal interop. Validate with a second-platform stub even if only macOS ships first. (Classic
  already abstracts much of this under B.)

### P4 — Release engineering + production gate
- Tiered `production_gate_{core,gui,env,soak}` + CI PR gate; signed/notarized bundle + auto-update +
  version guard; runtime health sampling; semantic-metadata schema + MCP eval suite.

---

## 4. Immediate next step

Ratify the **trunk decision (A/B/C)** in ADR-0011 (recommendation: B). P0 hygiene is trunk-agnostic and
can begin in parallel. The two audit transcripts that back §1 live in this session's history; their
findings are distilled above.
