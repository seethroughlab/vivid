# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Pre-Launch Verification

### Hot-Reload: Error Recovery

Two failure modes, both now handled:

**Compile failure** (cmake --build fails): old dylib stays loaded, nodes keep running.
Compiler stderr is now propagated to `ns.error_message` and exposed via MCP
`introspect_nodes` (`health.message`). UI tooltip requires a separate fix (tooltip gates
on `errored=true`; compile errors set `error_message` only).

**dlopen/symbol failure** (cmake succeeds but dylib is broken): previously killed the
node permanently. Fixed via atomic swap in `OperatorLoader::load()` — new dylib is
opened and validated before the old one is released; on failure the old dylib stays live.
`Scheduler::reload_operator()` now destroys instances while the old dylib is loaded
(safe), then on reload failure recreates instances from the still-valid old loader.

**Implementation (shipped):**
- `OperatorLoader::load()` — atomic swap: dlopen new → resolve symbols → on success
  dlclose old; on failure dlclose new, old dylib stays live (`operator_loader.cpp`)
- `Scheduler::reload_operator()` — destroy instances first (old dylib safe), reload,
  recreate from old loader on failure (`scheduler.cpp`)
- `poll_hot_reload()` — propagate `result.error_output` to `ns.error_message` for all
  nodes of the affected type on compile failure (`main.cpp`)

- [ ] Syntax error: edit .cpp to introduce a syntax error → hot-reload fires → node keeps
      producing output (old dylib running) → error visible in `health.message` via MCP →
      fix error → next reload clears error and updates behavior
- [ ] Missing include: same steps with `#include "nonexistent_header.h"` inserted
- [ ] dlopen failure: cmake succeeds but dylib is broken → node keeps running with old code
- [ ] Linked package reload from source: `link_package()` a local package → edit an
      operator's .cpp in the source tree → call `rebuild_package()` via MCP → node
      hot-reloads from the edited source

### LLM-Guided Workflow: End-to-End

Connect Claude to a running Vivid session via MCP and drive the full authoring loop.
Save MCP request/response artifacts to docs/archive/LLM-WORKFLOW-SESSION.md.

- [ ] Compose from existing operators: LLM calls `list_types`, builds a graph using only
      built-in operators, verifies live output with `inspect_graph`
- [ ] Scaffold a new operator: LLM calls `scaffold_operator`, source files appear on disk,
      CMakeLists.txt is patched, build succeeds, operator appears in `list_types`
- [ ] Implement and hot-reload: LLM edits the generated .cpp to produce identifiable
      behavior; saves file; hot-reload fires; `inspect_graph` confirms output changed
- [ ] Wire into graph and verify: LLM adds the new node, connects it, calls
      `run_diagnostics` or `run_checks` to confirm correct output
- [ ] Save session: LLM calls `save_variation` and `save_graph`; artifacts written to
      docs/archive/LLM-WORKFLOW-SESSION.md

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
