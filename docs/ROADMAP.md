# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Shipped

Major capabilities delivered for 1.0:

- **Three-domain graph engine** — audio, GPU, and control operators in a single unified graph with cross-domain bridging via control-as-hub topology
- **71 operators** across audio, GPU, and control domains (seed set + packages)
- **Hot reload** — parameter/routing changes same-frame, operator code recompile in 1–3 seconds with rollback-first safety
- **Package ecosystem** — install, link, unlink, rebuild, scaffold, test, browse, publish; project-local operator ownership
- **Python MCP bridge** — 57 tools exposing graph manipulation, introspection, packages, variations, checks, diagnostics, and more
- **HTTP control server** — 61 endpoints covering full Runtime API surface
- **Output analyzer** — audio metrics (RMS, peak, spectral centroid, brightness, flatness), visual metrics (brightness, contrast, motion), AV reactivity correlation, structured comparison with semantic labels
- **Capture and recording** — frame and audio capture for analysis and export
- **Variations and presets** — save, recall, rename, duplicate, reorder, queue, quantized switching, branching; operator-level presets with factory preset support
- **MIDI and OSC** — MIDI input/mapping/learn, OSC input/output
- **Undo/redo** — full graph state undo/redo
- **Export pipeline** — standalone binary export with tree-shaking
- **Introspection and diagnostics** — `introspect_nodes`, `run_diagnostics`, `get_registry_diagnostics`, `get_graph_load_diagnostics`, `get_graph_errors`
- **Checks/assertions** — `validate_checks`, `run_checks` for durable quality gates
- **Data-driven WGSL filter framework** — pure-WGSL filters with no C++ code
- **First-class GPU port types** — buffer, mesh, compute dispatch, texture readback
- **Spreads** — implicit vectorization with broadcasting semantics
- **Movie playback** — MovieLoaded operator trio
- **North Star validation** — end-to-end AV workflow validated

---

## Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation
- [x] North Star validation

Project-local operator ownership (clone/scaffold destination policy, package CMake patching, team workflow regression tests) shipped as part of launch prep.

---

## Deferred Past 1.0

- Subpatches
- Simulation zones (frame-to-frame feedback)
- Multi-window / multi-monitor
- Windows / Linux
- WebSocket API — external process integration over WebSocket; enables non-MCP clients to control Vivid programmatically
- Project file format (single JSON vs. directory with assets)
- Library version pinning
- Accessibility
- MIDI input tests (virtual MIDI loopback integration suite)
- Live REPL — integrated text-based graph mutation interface (PRD §3.2)
- Parameter space explorer — spatial parameter manipulation with MIDI mapping and preset interpolation (PRD §3.2)
- Pattern algebra interface — composable time-varying pattern expressions (PRD §3.2)
- State machine interface — macro-level named states with transition conditions (PRD §3.2)
- Built-in chat panel — in-app LLM chat; the Python MCP bridge is the 1.0 path
- Advanced analysis — color harmony, symmetry, spatial balance, pitch detection, stereo imaging, A/B parameter sweeps (PRD §9.2)

### Instrument Coherence Follow-Ons

- Instrument coherence Step 1 follow-up:
  - Embedded subgraph definitions inside regular graph JSON
  - Peek-inside visualization for selected module instances
  - Nested module editing workflows
  - Recursive module flattening after single-level semantics are stable
- Instrument coherence Step 2 follow-up:
  - Richer curve and scaling options
  - Destination bundles with author-defined weighting
  - Graph overlays for assignment visibility
  - Assignment gestures such as select-source-click-control
  - Broader lane-aware authoring helpers
  - Embedded-graph authoring support if Step 1 `v2` lands first
- Instrument coherence Step 3 follow-up:
  - Stereo-aware or mid/side variants
  - Stage tap outputs for advanced graph composition
  - Richer crossover types and routing morphing
  - Visual routing affordances or custom inspector UX
  - Broader filter-flavor expansion if package use shows a real gap
- Instrument coherence Step 4 follow-up:
  - Asset picker UI integrated into module and file-param controls
  - Broader asset kinds such as samples, impulse responses, or LUTs
  - Optional graph persistence by `asset_id`
  - Batch import, duplicate detection, and rename/delete workflows
  - Author-defined tags, favorites, and richer preview metadata
  - Audition and preview actions for sound-design workflows
- Instrument coherence Step 5 follow-up:
  - Richer performance-page widgets or gestures
  - Performance-control and local-modulation UI integration
  - Stronger package and browser affordances for performance-ready modules
  - Broader controller-learn workflows for performance surfaces
  - Deeper MPE configuration if real package usage proves the need
- Instrument coherence Step 6 follow-up:
  - Richer browser grouping and instrument-library visual affordances
  - Authoring UI for selecting `preview_controls`
  - Favorites, ratings, and library-curation fields
  - Per-variation or exposed-control snapshot browsing if usage proves it worthwhile
  - Step 5 performance-page and browser-preview integration

---

## Extra Findings

- [ ] Check if wgpu-native PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
