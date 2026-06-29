# PoC → Product: Readiness Assessment & Roadmap

Companion to [ADR-0011](../decisions/ADR-0011-poc-to-product-architecture.md). Target (ratified): an
**extensible, cross-platform-capable platform**. Trunk strategy (recommended, evidence-based): **keep our
codebase (`app/`) as the trunk and adopt vivid-classic's platform by *selective lift*** — not a
whole-trunk swap to classic. See §1d for the entanglement audit that decided this.

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
the extensibility/cross-platform machinery the target requires is largely absent. The question is whether
to swap to classic's trunk to get that machinery (Option B) or lift it onto ours — answered in §1d.

### 1d. Entanglement audit — can classic's machinery be lifted cleanly?

Three read-only probes of vivid-classic traced actual `#include`/type dependencies to test whether its
valuable subsystems are cleanly separable or welded to its runtime. They are **separable** — classic's
strict dependency direction pays off here. Per-subsystem disposition:

| Subsystem | Verdict | Evidence / cost |
|---|---|---|
| `operator_api/` (operator ABI + type-erased draw table) | **LIFT-CLEAN** | 37 headers; `operator_api/**`+stdlib+WebGPU only; **zero** `runtime/` includes (seed ops math/lfo/bloom confirm). The `void* opaque` draw table keeps **our `Renderer2D` as the host impl**. ~<1hr. |
| Operator loader + hot-reload (loader, `HotReloader`, `FileWatcher`) | **LIFT-CLEAN** | pure dlopen/ABI wrapper + build queue + file-event pump; graph compiler only *late-bound* at a recompile decision. ~15 files / 3k LoC. |
| Package system — core (manager/compiler/manifest/scan) | **LIFT-CLEAN** | depends only on the operator registry. ~25 files / 8k LoC. |
| `tools/operator_codegen` (tree-sitter) | **LIFT-ADAPT** | self-contained; generated code references `operator_api` only. Retarget the descriptor-emit template. ~4–6hr. |
| `AudioFrameBridge` (cross-cadence snapshot) | **LIFT-ADAPT** | lock-free double-buffer; reads `CompiledGraph` fields only. Refactor `build()` to a lean layout struct. ~1.2k LoC. |
| Test tiers / production-gate / sanitizers / CI / `test_helpers` | **ADOPT-PATTERN** | re-author for our targets; sanitizer flags copy verbatim. |
| Graph model / **7-pass lane compiler** | **BUILD-FRESH (right-sized)** | the no-cruft call — more than this product needs, and **nothing above forces it in**. Our `VisualGraph` is the seed. |
| Package lockfile (Phase 6a strict-mode) | **BUILD-FRESH / defer** | the one piece wedged into compiled-graph state. |
| `Renderer2D`, `ui_style`/theme, `vivid_runtime_testlib` | **KEEP OURS** | classic's are full WGPU-coupled *replacements*; adopting them discards our drawing library / pulls the whole runtime. |

**Conclusion:** keeping our trunk and selectively lifting is strictly better than B — we get classic's
proven ABI/loader/packages *and* keep our low-level work *and* avoid inheriting irrelevant breadth. This
is the strategy ADR-0011 recommends.

---

## 2. Carry-forward vs. lift vs. build-fresh

| Keep (ours, validated) | Lift from classic | Build fresh |
|---|---|---|
| `Renderer2D` + `ui_style`; the two-surface + bridge UX | `operator_api` (ABI + draw table) — LIFT-CLEAN | Right-sized **graph model** (seed: `VisualGraph`) |
| Mapping registry (source→shape→dest, curve/range/invert) | Operator **loader + hot-reload** — LIFT-CLEAN | Named error codes + control-server validation |
| Master transport + SPSC; thread-safe edit discipline | Package system **core** — LIFT-CLEAN | App decomposition (`main.cpp` → App + subsystems) |
| VST3 host + session + sampler; GPU stack | `operator_codegen`; `AudioFrameBridge` — LIFT-ADAPT | Cross-platform seams; package **lockfile** (defer) |
| MCP control-server shape (queue → main-thread) | Test/CI/sanitizer structure — ADOPT-PATTERN | Per-dir docs / `CLAUDE.md` |

---

## 3. Roadmap (phased)

Horizon is multi-month. Trunk = ours; each platform phase **lifts** the cleanly-separable classic
subsystem (per §1d) onto it, builds the right-sized graph model fresh, and adopts practices as patterns.

### P0 — Engineering hygiene (start now; real non-throwaway work on the trunk)
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

### P1 — Operator/ABI contract + a right-sized graph model (the keystone)
- **Lift `operator_api/` (LIFT-CLEAN)** into our trunk; keep our `Renderer2D` as the host behind its
  type-erased draw table. Add descriptor validation with named error codes.
- **Build a right-sized graph model fresh** (seed: our `VisualGraph` executor) and express the PoC's
  visuals ops + audio nodes as operators against the lifted ABI.
- **Front-load the seam spike**: the lifted `operator_api` descriptor model ↔ our new graph model is the
  main integration risk — prove one operator end-to-end (Plasma op + the bridge + a clip launching)
  before scaling out.

### P2 — Codegen + hot-reload + packages (lift)
- **Lift the operator loader + hot-reload + `FileWatcher` (LIFT-CLEAN)** and the **package system core
  (LIFT-CLEAN)**. **Lift-adapt `operator_codegen`** (retarget the descriptor-emit template) and
  **`AudioFrameBridge`** (lean layout struct). Defer/rebuild the lockfile strict-mode (BUILD-FRESH).

### P3 — Cross-platform seams (build)
- Abstract the macOS-locked surface behind interfaces: frame loop, audio device, plugin-window hosting,
  video, GPU/Metal interop. Validate with a second-platform stub even if only macOS ships first.

### P4 — Release engineering + production gate
- Tiered `production_gate_{core,gui,env,soak}` + CI PR gate; signed/notarized bundle + auto-update +
  version guard; runtime health sampling; semantic-metadata schema + MCP eval suite.

---

## 4. Immediate next step

Ratify the strategy in ADR-0011 (**keep our trunk + selective lift**) to flip it to accepted. Because the
trunk is ours, **P0 hygiene is real, non-throwaway work and starts first**; P1 (lift `operator_api` +
build the right-sized graph model, with the ABI↔graph seam spike) is the first platform step. The audit
transcripts backing §1/§1d live in this session's history; their findings are distilled above.
