# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Milestone 1: Core Stability & Testing

### 1. Expand automated test coverage

**Status:** 45 tests. Settings persistence, WGSL filter loading, variation/preset round-trips, graph validation edge cases, operator loader unload-during-use, and MIDI state isolation are fully covered. Remaining gap: MIDI note on/off and hardware-level channel filtering require a virtual MIDI loopback and belong in a separate integration suite.

- [x] Audit uncovered modules — identify operators and subsystems with zero test coverage
- [x] Add operator_registry edge cases: duplicate registration, missing dependencies, unload during use
- [x] Add graph validation edge cases: cycles, dangling wires, mismatched port types, empty graphs
- [x] Add capture_coordinator tests: start/stop capture, frame count, output file creation
- [x] Add settings persistence round-trip: write settings → reload → verify values preserved
- [x] Add WGSL filter loading tests: valid shader, syntax error, missing uniforms
- [ ] Add MIDI input tests: note on/off mapping, CC mapping, channel filtering *(hardware-dependent — deferred to integration suite)*
- [x] Add variation/preset tests: save variation, recall variation, preset file round-trip

### 2. Smoke tests

**Status:** `test_demo_graphs.cpp` loads all 47 demo graphs headlessly and ticks 5 frames. This catches crashes but not warnings or logic errors.

- [x] Extend demo graph smoke test to also validate no stderr warnings (not just no crashes)
- [x] Add a dedicated CI smoke-test step that runs the full smoke suite (`.github/workflows/smoke.yml`)
- [x] Increase tick count for selected complex graphs (e.g., audio + GPU combo graphs) to catch late-onset issues
- ~~Add vivid-3d demo graphs to the smoke test suite (requires vivid-3d installed as package)~~ — **Architecture correction:** Package smoke tests are **owned by the package repo**, not by vivid-core. Each package's CI clones vivid-core, builds it, and runs `test_demo_graphs` against the package's own `graphs/` directory. Vivid-core's smoke test only covers graphs that ship with vivid itself.
- ~~Add package-dependent graph smoke tests (vivid-glitch graphs, etc.)~~ — Same as above; this is a package-side responsibility.
- [ ] Document the package smoke test protocol (how a package's CI should clone vivid-core and run `test_demo_graphs` against its own graphs)

### 3. Catalog application functionality for manual testing

**Status:** No catalog exists. Manual testing is ad-hoc.

- [ ] Create `docs/MANUAL-TEST-CATALOG.md` organized by functional area:
  - **Graph editing:** add/delete nodes, connect/disconnect wires, copy/paste, drag to reorder, group selection
  - **Parameter UI:** sliders, xy-pads, color pickers, typed input, dropdown menus, toggle buttons
  - **Audio:** audio operators produce sound, AudioOut routing, sample rate handling, buffer sizes
  - **GPU:** GPU operators render, texture passing between nodes, shader compilation errors shown
  - **Packages:** install/uninstall from UI, package operators appear in palette
  - **File I/O:** save graph, load graph, recent files, file association
  - **MIDI:** MIDI input mapping, learn mode, device hot-plug
  - **Variations & presets:** save/recall variations, preset files, interpolation
  - **Capture:** screenshot, video capture start/stop, output file correctness
  - **Themes:** theme switching, custom theme loading
  - **Fullscreen:** enter/exit fullscreen, external display output
- [ ] For each area, list the expected behavior and pass/fail criteria
- [ ] Include platform-specific notes (macOS quirks, permissions, etc.)

### 4. Test inner/outer loop

#### Inner loop (parameter tweaking)

**Status:** No structured test plan. Works in practice but untested systematically.

- [ ] Document the expected inner-loop workflow: adjust slider → see visual/audio change in real time
- [ ] Manual test checklist:
  - Slider drag updates output continuously (no stutter, no lag)
  - XY-pad drag updates two parameters simultaneously
  - Color picker changes propagate to downstream GPU nodes
  - Typed numeric input validates and clamps to range
  - Undo coalesces continuous drags into one step (blocked on undo system — item 8)

#### Outer loop (operator editing + hot-reload)

**Status:** `test_hot_reload.cpp` covers file-watcher → recompilation → reload. No end-to-end manual verification across all domains.

- [ ] Document the expected outer-loop workflow: edit .cpp → save → file watcher fires → hot-reload compiles → operator reloads in running graph
- [ ] Verify hot-reload for each domain:
  - Control operators
  - Audio operators
  - GPU operators
- [ ] Test error cases:
  - Syntax error in .cpp → error shown in UI, operator stays at last good version
  - Missing include → same graceful handling
  - Editing a linked package operator → hot-reload fires from the linked source directory
- [ ] Verify state preservation across reload: parameter values, wire connections, node positions retained

### 5. Test operator creation + MCP-assisted development

**Status:** `test_operator_creator.cpp` covers scaffold. MCP `scaffold_operator` tool exists in `mcp_server.cpp`.

- [ ] End-to-end test: scaffold → edit implementation → hot-reload → use in a graph → verify output
- [ ] Test all domain variants:
  - Control operator
  - Audio operator
  - GPU operator
  - Composite operator (multiple domains)
- [ ] Test MCP scaffold_operator tool via the control server (send JSON-RPC, verify files created)
- [ ] Test LLM-guided workflow: scaffold → Claude edits code via MCP → reload → verify operator works
- [ ] Test scaffold into an existing package directory (not just top-level operators/)
- [ ] Verify generated code compiles without warnings on first build

### 6. Test package install/uninstall from GitHub

**Status:** `test_package_manager.cpp` uses temp dirs with local paths. No tests against real remote repos.

- [ ] Test install from real GitHub repos:
  - `vivid-3d` (has dependencies, multiple operators)
  - `vivid-glitch` (simpler package)
- [ ] Test install with transitive dependencies (package A depends on package B)
- [ ] Test uninstall cleans up: compiled artifacts removed, operators de-registered, graphs using those operators show "missing operator" gracefully
- [ ] Test reinstall / upgrade: install → modify → reinstall overwrites correctly
- [ ] Test failure cases:
  - Bad URL → clear error message
  - Missing manifest → clear error message
  - Compile error in package → error reported, partial install cleaned up
  - Network failure mid-download → clean rollback
- [ ] Test that installed package operators appear in the operator palette immediately

### 7. Test `vivid link` / `vivid unlink`

**Status:** Covered in `test_package_manager.cpp` programmatically. No CLI end-to-end tests.

- [ ] CLI end-to-end test: `vivid link ../path/to/package` creates expected symlink
- [ ] Verify symlink points to correct directory and operators load from it
- [ ] Verify edits to linked source directory trigger hot-reload in Vivid
- [ ] Verify `vivid unlink package-name` removes symlink but does not delete source directory
- [ ] Verify re-link after unlink works (no stale state)
- [ ] Test linking a package that's already installed (should replace or error clearly)
- [ ] Test unlinking a package that has running operators in the current graph

### 8. Undo/redo system

**Status:** No undo infrastructure exists. No undo stack, no command history, no Cmd+Z handler.

This is the only new feature in Milestone 1 — everything else is testing. It requires design, implementation, and integration work.

#### Design

- [ ] Define `UndoManager` public API: `push()`, `undo()`, `redo()`, `clear()`, `canUndo()`, `canRedo()`
- [ ] Decide snapshot granularity (see Design Notes below)
- [ ] Decide max history depth (see Design Notes below)
- [ ] Write a brief design doc or code comment block before implementation

#### Implementation

- [ ] Create `UndoManager` class (likely `src/core/undo_manager.h` / `.cpp`)
  - Ring buffer of Graph JSON snapshots
  - Current position index
  - Push/undo/redo logic
- [ ] Capture snapshots via `UICommandSink` — after each undoable mutation, push the new graph state
- [ ] Implement coalescing for continuous parameter drags (timer-based, ~300ms window)
- [ ] Implement coalescing for node layout drags (same strategy)
- [ ] Add Cmd+Z (undo) and Cmd+Shift+Z (redo) key handling in `NodeGraphUI::on_key()`

#### Integration

- [ ] Wire `UndoManager` into `RuntimeCommandSink` so all graph mutations flow through it
- [ ] Add undo/redo buttons to the toolbar (grayed out when unavailable)
- [ ] Expose undo/redo via MCP tools (for LLM-assisted editing workflows)
- [ ] Test with all mutation types:
  - Add/delete node
  - Connect/disconnect wire
  - Change parameter value (slider, typed, color picker)
  - Move node position
  - Copy/paste nodes
  - Group operations (select multiple → delete)

#### Edge cases

- [ ] Undo across file load: loading a new file clears undo history and pushes initial state
- [ ] Undo after hot-reload changes operator set: snapshot may reference operators that changed — handle gracefully
- [ ] Undo with package install/uninstall: if an operator type no longer exists, show "missing operator" placeholder
- [ ] Very large graphs: verify memory usage stays reasonable (100 snapshots × graph size)

#### Design Notes

**Why snapshot-based over command pattern:**
Graph already serializes to/from JSON via `Graph::save()` / `Graph::load()`. A command pattern requires writing an inverse for every mutation type in `UICommandSink` (30+ methods) — far more code for marginal memory savings. Snapshot-based undo is simple, correct by construction, and easy to debug (each snapshot is a valid, inspectable JSON document).

**Coalescing strategy:**
Continuous parameter drags (sliders, xy-pads) and node layout drags should coalesce into one undo step. Implementation: if the same parameter (or node position) changes again within ~300ms of the last snapshot, replace the top snapshot instead of pushing a new one. Topology changes (add/remove node, connect/disconnect wire) always push immediately.

**Max history:**
100 snapshots in a ring buffer. At ~100KB per snapshot for a typical graph, this is ~10MB — well within reason. Make the depth configurable in settings. When the ring buffer is full, the oldest snapshot is silently dropped.

**Redo stack behavior:**
Standard: any new mutation clears the redo stack. This is the universally expected behavior.

**On file load:**
Clear the entire undo/redo history. Push the loaded graph as the initial (non-undoable) state.

**Key files to modify:**
- `src/ui/ui_command_sink.h` — `UICommandSink` interface (mutation callbacks)
- `src/runtime/runtime_command_sink.h/.cpp` — `RuntimeCommandSink` (concrete implementation, wire in UndoManager)
- `src/core/graph.h/.cpp` — `Graph::save()` / `Graph::load()` (snapshot serialization)
- `src/ui/node_graph_ui.h/.cpp` — `NodeGraphUI::on_key()` (keyboard shortcuts)

---

## Milestone 2: Package Ecosystem

Infrastructure exists (package manifest, search paths, `operators/packages/` directory), but the full vision is incomplete:

- [ ] Extract vivid-wavetable as external package; evaluate other extraction candidates (drum kit, plexus)
- [ ] Evaluate which operator internals should become shared helper methods (drum DSP, envelope, LFO, smooth)
- [ ] Add licenses to all external packages (vivid-3d, vivid-glitch)
- [ ] Package versioning system with update alerts
- [ ] Template repos for publishing operator packages
- [ ] Search path resolution across local, project, and system scopes
- [ ] Evaluate: package browsing site (a la Ableton Packs)

## Milestone 3: LLM Perception System

- [ ] Per-node introspection (structured output describing what each node produces)
- [ ] Analysis tools (automated graph-level diagnostics)
- [ ] Assertions (runtime correctness checks)
- [ ] Explore legacy branch analysis work (visual analysis, audio analysis, analysis hints, DSP utilities, spectrogram rendering)
- [ ] Ensure MCP server has comprehensive understanding of Vivid's structure

## Milestone 4: Developer & User Experience

- [ ] Getting Started guide with example graphs
- [ ] Update README.md (fix stale operator list — glitch ops listed but extracted; reflect current state)
- [ ] Evaluate: MovieOut operator vs. menubar record button
- [ ] Error reporting link from the GUI to GitHub Issues
- [ ] Fullscreen / external display output (projectors, LED walls)
- [ ] OSC input for installations and external hardware
- [ ] NDI/Syphon output for routing video to other apps

## Milestone 5: Release Infrastructure

- [ ] GitHub Actions to build macOS releases
- [ ] Versioning system for Vivid itself (user alerts, auto-updates)
- [ ] Redesign the application icon

## Milestone 6: Legacy Branch Evaluation

- [ ] Explore legacy branch for features worth bringing into master
- [ ] Be selective — legacy goals differ from the minimal 1.0 (step sequencer, section-aware export, extended analysis are candidates)

## Milestone 7: Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation
- [ ] Update ROADMAP.md to reflect completed/deferred items

---

## Open Questions

- **Semantic parameter tags:** Tags like `"frequency"` or `"amplitude"` on `ParamBase` could enable LLM hinting and smart defaults, but no tagging system exists yet. Worth designing before or alongside Milestone 3.

---

## Deferred Past 1.0

These are acknowledged but explicitly out of scope for the initial release:

- Subpatches
- Simulation zones (frame-to-frame feedback)
- Multi-window / multi-monitor
- Windows / Linux
- Bundled compiler
- WebSocket API — external process integration over WebSocket; enables non-MCP clients to control Vivid programmatically
- Semantic parameter tags
- Project file format (single JSON vs. directory with assets)
- Library version pinning
- Accessibility
