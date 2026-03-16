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
- **MCP server** — 57 tools exposing graph manipulation, introspection, packages, variations, checks, diagnostics, and more
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
- **North Star validation** — end-to-end AV workflow validated (see `docs/internal/NORTH-STAR-VALIDATION.md`)

---

## Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation
- [x] North Star validation — documented in `docs/internal/NORTH-STAR-VALIDATION.md`

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
- Built-in chat panel — in-app LLM chat; MCP server is the 1.0 path
- Advanced analysis — color harmony, symmetry, spatial balance, pitch detection, stereo imaging, A/B parameter sweeps (PRD §9.2)

---

## Extra Findings

- [ ] Check if wgpu-native PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
