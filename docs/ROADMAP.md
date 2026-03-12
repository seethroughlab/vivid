# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Pre-Launch Verification

### Hot-Reload

- [x] Hot-reload verified for each domain: Control, Audio, GPU
- [ ] Error cases: syntax error keeps last good version; missing include same; editing linked package operator reloads from source
- [x] State preservation across reload: params, wires, node positions

### Operator Creation + MCP E2E

- [x] End-to-end: scaffold → edit → hot-reload → use in graph → verify output
- [x] All domain variants: Control, Audio, GPU, Composite
- [x] MCP `scaffold_operator` via control-server request path
- [ ] LLM-guided workflow end-to-end
- [x] Scaffold into existing package directory (not just top-level `operators/`)
- [x] Generated code compiles without warnings on first build

---

## Port Type Registry

One-shot ABI break replacing `VIVID_PORT_HANDLE` with extensible `VividPortType` + explicit `VividPortTransport`. See [PORT-TYPE-REGISTRY.md](PORT-TYPE-REGISTRY.md) for the full spec.

Develop in a dedicated branch and merge to master only when complete and tested.

---

## Operator Loading Consolidation

Ships in the same ABI break as the port type registry.

Develop in a dedicated branch and merge to master only when complete and tested.

### Unified indexed ABI

All dylibs (package and custom) export the same indexed entry points:

- `vivid_operator_count()` — number of operators in this dylib
- `vivid_operator_descriptor(i)`, `vivid_operator_create(i)`, `vivid_operator_destroy(i)`, `vivid_operator_process(i)` — access operators by index

Two registration macros:

- `VIVID_REGISTER(ClassName)` — single-operator dylib (count=1). Used by custom per-project operators and Vivid's built-in operator dylibs. Same macro name as today; operator source files are untouched.
- `VIVID_REGISTER_PACKAGE(Op1, Op2, ...)` — multi-operator dylib. Used by packages to register all operators in one shared library.

The registry doesn't distinguish tiers at the loader level — it opens any dylib, reads `vivid_operator_count()`, and creates one loader entry per operator.

### Tier 1: Core operators — built into the runtime

- Register via `register_builtin()`, no dylibs
- No hot-reload (by design — they are part of the runtime)

### Tier 2: Package operators — one dylib per package

- Each package produces a single shared library (not one per operator)
- `PackageCompiler` auto-generates a registration source that includes all operator headers and invokes `VIVID_REGISTER_PACKAGE(...)`
- Hot-reload at package granularity: reloading the package dylib swaps all operators from that package atomically

### Tier 3: Custom per-project operators — one dylib per operator

- Individual operators compiled as standalone dylibs using `VIVID_REGISTER` (count=1)
- Hot-reload at individual operator granularity (the current behavior)
- Live in the project's `operators/` directory
- Scaffolded via `vivid scaffold` or MCP — the primary LLM authoring workflow

---

## Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation

Project-local operator ownership (clone/scaffold destination policy, package CMake patching, team workflow regression tests) shipped as part of launch prep.

---

## Deferred Past 1.0

- Subpatches
- Simulation zones (frame-to-frame feedback)
- Multi-window / multi-monitor
- Windows / Linux
- Bundled compiler
- WebSocket API — external process integration over WebSocket; enables non-MCP clients to control Vivid programmatically
- Project file format (single JSON vs. directory with assets)
- Library version pinning
- Accessibility
- MIDI input tests (virtual MIDI loopback integration suite)

---

## Extra Findings

- [ ] Check if wgpu-native PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
