# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Pre-Launch Verification

### Hot-Reload

- [ ] Hot-reload verified for each domain: Control, Audio, GPU
- [ ] Error cases: syntax error keeps last good version; missing include same; editing linked package operator reloads from source
- [ ] State preservation across reload: params, wires, node positions

### Operator Creation + MCP E2E

- [ ] End-to-end: scaffold → edit → hot-reload → use in graph → verify output
- [ ] All domain variants: Control, Audio, GPU, Composite
- [ ] MCP `scaffold_operator` via control-server request path
- [ ] LLM-guided workflow end-to-end
- [ ] Scaffold into existing package directory (not just top-level `operators/`)
- [ ] Generated code compiles without warnings on first build

---

## Port Type Registry

One-shot ABI break replacing `VIVID_PORT_HANDLE` with extensible `VividPortType` + explicit `VividPortTransport`. See [PORT-TYPE-REGISTRY.md](PORT-TYPE-REGISTRY.md) for the full spec.

Develop in a dedicated branch and merge to master only when complete and tested.

---

## Operator Loading Consolidation

Ships in the same ABI break as the port type registry.

Develop in a dedicated branch and merge to master only when complete and tested.

### Core operators — built into the runtime

- Core operators register via `register_builtin()`, no dylibs
- No hot-reload for core operators (by design — they are part of the runtime)

### Package operators — one dylib per package

- Each package produces a single shared library (not one per operator)
- New multi-operator ABI entry points:
  - `vivid_operator_count()` — returns number of operators in the package
  - Indexed `vivid_operator_descriptor(i)`, `vivid_operator_create(i)`, `vivid_operator_destroy(i)`, `vivid_operator_process(i)` — access operators by index
- `VIVID_REGISTER` convenience macro for single-operator packages (wraps the indexed ABI with count=1)
- Hot-reload applies only to package/custom operators, at package granularity

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
