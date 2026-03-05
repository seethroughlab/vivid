# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Milestone 1: Core Stability & Testing

*Note: As operators are extracted (Milestone 2), the testing scope here shrinks — fewer core operators to cover means fewer test gaps to close.*

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

**Status:** Complete. `test_demo_graphs` loads all demo graphs headlessly, validates no stderr warnings, and runs in CI. Package repos (vivid-3d, vivid-glitch) each have their own smoke test CI that clones vivid-core and runs `test_demo_graphs` against their graphs. Protocol documented in `docs/PACKAGE-SMOKE-TEST.md`.

- [x] Extend demo graph smoke test to also validate no stderr warnings (not just no crashes)
- [x] Add a dedicated CI smoke-test step that runs the full smoke suite (`.github/workflows/smoke.yml`)
- [x] Increase tick count for selected complex graphs (e.g., audio + GPU combo graphs) to catch late-onset issues
- ~~Add vivid-3d demo graphs to the smoke test suite (requires vivid-3d installed as package)~~ — **Architecture correction:** Package smoke tests are **owned by the package repo**, not by vivid-core. Each package's CI clones vivid-core, builds it, and runs `test_demo_graphs` against the package's own `graphs/` directory. Vivid-core's smoke test only covers graphs that ship with vivid itself.
- ~~Add package-dependent graph smoke tests (vivid-glitch graphs, etc.)~~ — Same as above; this is a package-side responsibility.
- [x] Document the package smoke test protocol (how a package's CI should clone vivid-core and run `test_demo_graphs` against its own graphs)
- [x] Based on findings in previous task, implement smoke tests/CI in ../vivid-3d and ../vivid-glitch

### 3. Catalog application functionality for manual testing

**Status:** Complete. Manual test catalog exists at `docs/MANUAL-TEST-CATALOG.md` with functional-area coverage, pass/fail criteria, and macOS-specific notes.

- [x] Create `docs/MANUAL-TEST-CATALOG.md` organized by functional area:
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
- [x] For each area, list the expected behavior and pass/fail criteria
- [x] Include platform-specific notes (macOS quirks, permissions, etc.)

### 4. Test inner/outer loop

#### Inner loop (parameter tweaking)

**Status:** Test plan documented in `docs/INNER-OUTER-LOOP-TEST-PLAN.md`. Manual execution is still pending.

- [x] Document the expected inner-loop workflow: adjust slider → see visual/audio change in real time
- [x] Manual test checklist:
  - Slider drag updates output continuously (no stutter, no lag)
  - XY-pad drag updates two parameters simultaneously
  - Color picker changes propagate to downstream GPU nodes
  - Typed numeric input validates and clamps to range
  - Undo coalesces continuous drags into one step (blocked on undo system — item 8)

#### Outer loop (operator editing + hot-reload)

**Status:** Workflow and manual checklist documented in `docs/INNER-OUTER-LOOP-TEST-PLAN.md`. End-to-end manual verification across domains is still pending.

- [x] Document the expected outer-loop workflow: edit .cpp → save → file watcher fires → hot-reload compiles → operator reloads in running graph
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

**Status:** `test_operator_creator.cpp` covers scaffold. MCP `scaffold_operator` tool exists in `mcp_server.cpp`. Manual/E2E plan documented in `docs/OPERATOR-CREATION-MCP-TEST-PLAN.md`. Execution notes are in `docs/archive/OPERATOR-CREATION-MCP-TEST-RESULTS.md` (OC-1 + OC-3 live bridge pass; startup mitigations were required due deferred-probe crashes in specific plugins).

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

**Status:** Complete. Results documented in `docs/archive/PACKAGE-INSTALL-E2E-RESULTS.md`: both `vivid-3d` and `vivid-glitch` install/uninstall/reinstall successfully from real GitHub URLs; failure-case handling is verified (bad URL, missing manifest, compile failure, network failure, missing local tools); and package operators appear in the type list/palette without restart after live install/uninstall.

- [x] Test install from real GitHub repos:
  - `vivid-3d` (has dependencies, multiple operators)
  - `vivid-glitch` (simpler package)
- [x] Test install with transitive dependencies (package A depends on package B) *(Deferred / N/A for now: no package-to-package dependency edges exist yet)*
- [x] Test uninstall cleans up: compiled artifacts removed, operators de-registered, graphs using those operators show "missing operator" gracefully
- [x] Test reinstall / upgrade: install → modify → reinstall overwrites correctly
- [x] Test failure cases:
  - Bad URL → clear error message *(verified)*
  - Missing manifest → clear error message *(verified)*
  - Compile error in package → error reported, partial install cleaned up *(verified)*
  - Network failure mid-download → clean rollback *(verified via unreachable host/port)*
  - Missing local build tools (`git`, `clang++`, `cmake`) → clear remediation message *(verified; preflight checks added)*
- [x] Test that installed package operators appear in the operator palette immediately

### 7. Test `vivid link` / `vivid unlink`

**Status:** Complete. CLI end-to-end link/unlink behavior is verified, including symlink correctness, live hot-reload from linked source, unlink safety (source preserved), re-link behavior, already-installed conflict handling, and unlinking while operators are active.

- [x] CLI end-to-end test: `vivid link ../path/to/package` creates expected symlink
- [x] Verify symlink points to correct directory and operators load from it
- [x] Verify edits to linked source directory trigger hot-reload in Vivid
- [x] Verify `vivid unlink package-name` removes symlink but does not delete source directory
- [x] Verify re-link after unlink works (no stale state)
- [x] Test linking a package that's already installed (should replace or error clearly)
- [x] Test unlinking a package that has running operators in the current graph

### 8. Undo/redo system

**Status:** In progress. `UndoManager` exists (`src/runtime/undo_manager.h/.cpp`) with tested snapshot push/undo/redo behavior, max-depth retention, and redo-clearing on branch edits. Integration into command flow/UI shortcuts is still pending.

This is the only new feature in Milestone 1 — everything else is testing. It requires design, implementation, and integration work.

#### Design

- [x] Define `UndoManager` public API: `push()`, `undo()`, `redo()`, `clear()`, `canUndo()`, `canRedo()` *(see `docs/archive/UNDO-REDO-DESIGN.md`)*
- [x] Decide snapshot granularity (see Design Notes below) *(see `docs/archive/UNDO-REDO-DESIGN.md`)*
- [x] Decide max history depth (see Design Notes below) *(see `docs/archive/UNDO-REDO-DESIGN.md`)*
- [x] Write a brief design doc or code comment block before implementation *(see `docs/archive/UNDO-REDO-DESIGN.md`)*

#### Implementation

- [x] Create `UndoManager` class (`src/runtime/undo_manager.h` / `.cpp`)
  - Ring buffer of Graph JSON snapshots
  - Current position index
  - Push/undo/redo logic
- [x] Capture snapshots via `UICommandSink` — after each undoable mutation, push the new graph state
- [x] Implement coalescing for continuous parameter drags (timer-based, ~300ms window)
- [x] Implement coalescing for node layout drags (same strategy)
- [x] Add Cmd+Z (undo) and Cmd+Shift+Z (redo) key handling in `NodeGraphUI::on_key()`

#### Integration

- [x] Wire `UndoManager` into `RuntimeCommandSink` so all graph mutations flow through it
- [x] Add undo/redo buttons to the toolbar (grayed out when unavailable)
- [x] Expose undo/redo via MCP tools (for LLM-assisted editing workflows)
- [x] Test with all mutation types *(see `docs/archive/UNDO-MUTATION-TEST-RESULTS.md`)*:
  - Add/delete node
  - Connect/disconnect wire
  - Change parameter value (slider, typed, color picker)
  - Move node position
  - Copy/paste nodes
  - Group operations (select multiple → delete)

#### Edge cases

- [x] Undo across file load: loading a new file clears undo history and pushes initial state
- [x] Undo after hot-reload changes operator set: snapshot may reference operators that changed — handle gracefully
- [x] Undo with package install/uninstall: if an operator type no longer exists, show "missing operator" placeholder
- [x] Very large graphs: verify memory usage stays reasonable (100 snapshots × graph size) *(Deferred for now; revisit after Milestone 2 extraction stabilizes core surface area)*

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

## Milestone 2: Operator Extraction

Every operator left in-core is scope that Milestones 1–5 must cover: test, document, stabilize, and ship. Extracting non-essential operators now directly shrinks what 1.0 has to be. The goal is to reach a minimal, defensible core — one where every remaining operator is a genuine environment primitive.

### Target minimal core (~37 operators)

| Domain | Keep in Core |
|--------|-------------|
| Control | Clock, LFO, Envelope, Smooth, Math, Random, Logic, Gate, Euclidean, PatTransform, Stack, Alternate, SpreadNoise, MidiInput |
| Audio | Oscillator, Gain, Delay, Reverb, Distortion, Bitcrush, FFTAnalysis, MovieFileAudioIn, SpreadLFO, SpreadADSR, ModulatedGain |
| GPU | Shape, Noise, Composite, Feedback, Bloom, Text, Instance, Bars, TimeMachine, MovieFileIn, WebcamIn, TextureAnalysis |

That's ~37 operators, down from 62. The 25 extracted operators move to four external packages.

### Extraction packages (in priority order)

**vivid-wavetable** — 1 audio operator
- `wavetable_synth` — FFT-based mipmap wavetable synthesizer
- Highly specialized DSP; already named in roadmap

**vivid-drums** — 5 audio operators
- `drum_kick`, `drum_snare`, `drum_hihat`, `drum_cymbal`, `drum_clap`
- Cohesive domain with shared `drum_dsp.h` (PinkNoise, DecayEnvelope, SVF filter)

**vivid-plexus** — 1 GPU + 1 audio operator
- `gpu/plexus` — complex node-graph visualization (696 lines)
- `audio/plexus_synth` — pentatonic polysynth

**vivid-sequencers** — 8 control operators
- `sequencer`, `drum_sequencer`, `pattern_seq`, `note_pattern`, `note_duration`, `arpeggiator`, `chord_progression`, `state_machine`
- Music-composition tools; Clock, Euclidean, PatTransform, Stack, and Alternate stay in core as timing/pattern-algebra primitives

### Execution Checklist

#### Phase 0: Preflight (once)

- [ ] Freeze final operator move list per package (no ambiguities)
- [ ] Define extraction order and cut points (finish each package completely before starting next)
- [ ] Capture baseline checks before extraction:
  - `test_demo_graphs`
  - package install/uninstall smoke
  - core build + operator scan sanity
- [ ] Define per-package acceptance gate:
  - package builds in isolation
  - `vivid link` + `vivid rebuild` succeed
  - operators appear in palette/type list
  - uninstall removes operators cleanly
  - CI smoke test passes

#### Phase 1: vivid-wavetable (package 1, start here)

- [x] Move `operators/audio/wavetable_synth/wavetable_synth.cpp` to `vivid-wavetable`
- [x] Move/carry factory presets for `wavetable_synth`
- [x] Add `vivid-package.json` + package `CMakeLists.txt`
- [x] Remove `wavetable_synth` from vivid-core operator build list
- [x] Remove or migrate wavetable-specific core tests to package-level tests
- [x] Verify vivid-core builds/tests still pass with `WavetableSynth` absent from core
- [x] Verify linked package install path works:
  - `vivid link ../vivid-wavetable`
  - `vivid rebuild vivid-wavetable`
  - type appears in palette without restart
- [x] Add package CI smoke test (build + probe)
- [x] Move wavetable-specific demo graphs from `vivid/graphs` into `../vivid-wavetable/graphs`
- [x] Move wavetable-specific tests from `vivid/tests` into `../vivid-wavetable/tests`
- [x] Run package tests in `vivid-wavetable` CI (not only core smoke checks)
- [x] Add license/readme in package repo

#### Phase 2: vivid-drums

- [x] Move 5 drum operators into `vivid-drums`
- [x] Move `drum_dsp.h` to `src/operator_api/` (shared API boundary)
- [x] Update include paths/usages in vivid-core and package
- [x] Add package manifest + CMake + CI smoke test
- [x] Move drum-specific demo graphs from `vivid/graphs` into `../vivid-drums/graphs`
- [x] Move drum-specific tests from `vivid/tests` into `../vivid-drums/tests` *(no drum-specific core test files remained; added package-owned manifest smoke test in `vivid-drums/tests`)*
- [x] Run package tests in `vivid-drums` CI (not only core smoke checks)
- [x] Verify uninstall/reinstall and palette visibility

#### Phase 3: vivid-plexus

- [x] Move `gpu/plexus` + `audio/plexus_synth` into `vivid-plexus`
- [x] Add package manifest + CMake + CI smoke test
- [x] Move plexus-specific demo graphs from `vivid/graphs` into `../vivid-plexus/graphs`
- [x] Move plexus-specific tests from `vivid/tests` into `../vivid-plexus/tests` *(no dedicated plexus core test files remained; added package-owned manifest smoke test in `vivid-plexus/tests`)*
- [x] Run package tests in `vivid-plexus` CI (not only core smoke checks)
- [x] Verify install/uninstall + runtime behavior

#### Phase 4: vivid-sequencers

- [x] Move 8 sequencing operators into `vivid-sequencers`
- [x] Ensure remaining core timing primitives still cover demos/tests
- [x] Add package manifest + CMake + CI smoke test
- [x] Move sequencer-specific demo graphs from `vivid/graphs` into `../vivid-sequencers/graphs`
- [x] Move sequencer-specific tests from `vivid/tests` into `../vivid-sequencers/tests`
- [x] Run package tests in `vivid-sequencers` CI (not only core smoke checks)
- [x] Verify install/uninstall + runtime behavior

#### Phase 5: Post-extraction cleanup

- [x] Add licenses to all extracted packages and existing external packages (`vivid-3d`, `vivid-glitch`)
- [x] Update README.md operator list to reflect minimal core
- [x] Verify `test_demo_graphs` passes with reduced core (no core graph hard-dep on extracted operators)
- [x] Verify extracted-package graphs/tests are no longer duplicated in vivid-core
- [x] Publish migration notes for users (what moved, how to install packages)

### Operator API work

- [x] Move `drum_dsp.h` to `src/operator_api/` so external packages (including vivid-drums itself) can use it
- [x] Document and expose `audio_dsp.h` utilities (`WhiteNoise`, `PinkNoise`, `waveform`, `detect_trigger`) as public operator API, with compatibility coverage (`test_audio_dsp_api`)

### Extraction tasks

- [x] Extract vivid-wavetable: move `wavetable_synth.cpp`, create package repo, add CI smoke test
- [x] Extract vivid-drums: move 5 drum operators + `drum_dsp.h` to operator API, create package repo, add CI smoke test
- [x] Extract vivid-plexus: move `plexus.cpp` + `plexus_synth.cpp`, create package repo, add CI smoke test
- [x] Extract vivid-sequencers: move 8 operators, create package repo, add CI smoke test
- [x] Add licenses and AGENTS.md to all extracted packages and existing external packages (vivid-3d, vivid-glitch)
- [x] Update README.md operator list to reflect minimal core after extraction
- [x] Verify `test_demo_graphs` passes with reduced core (no graphs in vivid-core should reference extracted operators)

---

## Milestone 3: Package Ecosystem

Goal: make package authoring, install/update behavior, and discovery feel first-class and predictable for both humans and MCP/LLM workflows.

### Scope for Milestone 3

- [ ] Package versioning + update alerts
- [ ] Package publishing templates and docs
- [ ] Deterministic multi-scope search-path resolution (local/project/system)
- [ ] Evaluate and decide on package browsing/discovery surface

### Phase 0: Spec & Baseline

- [x] Freeze current package contract in docs:
  - `vivid-package.json` required/optional fields
  - install/link/rebuild/uninstall behavior and expected state transitions
  - current package root locations and scan order
- [x] Add a short architecture note (`docs/PACKAGE-ECOSYSTEM-DESIGN.md`) covering:
  - version model (`version`, optional constraints, compatibility surface)
  - update policy (manual check vs startup check; non-blocking warnings)
  - scope precedence rules (local > project > user/system, with explicit tie-breakers)
- [x] Capture baseline tests before changes:
  - core package install/link/unlink tests
  - package smoke tests in sibling repos
  - MCP package tool sanity checks

### Phase 1: Versioning + Updates

- [x] Extend manifest schema with semantic version and optional compatibility metadata (`vivid_core` SemVer range parsed by `PackageManager`; sibling package manifests updated)
- [x] Implement version-aware package metadata in `PackageManager`:
  - read installed version
  - detect newer remote version
  - classify update as compatible/incompatible based on policy
- [x] Add update check command/API surface:
  - CLI: `vivid package-check-updates` (or equivalent existing command namespace)
  - control server endpoint + MCP tool for update checks
- [x] Add non-intrusive UI/CLI update alert messaging:
  - never block startup or graph load
  - provide actionable remediation command

### Phase 2: Search Paths & Resolution

*Trimmed rollout note:* implement fixed scope order (`local > workspace > user > builtin`) first; defer `system` scope and configurable scope-order/settings knobs until after initial resolver/diagnostic shipping.

- [x] Finalize canonical scope directories and config knobs (spec: `docs/PACKAGE-SEARCH-PATHS.md`)
  - local graph/package folder scope
  - project workspace scope
  - user/system package scope
- [x] Implement deterministic resolver with explicit precedence and conflict handling
- [x] Add diagnostics:
  - `list-packages --verbose` (or equivalent) shows source scope/path/version
  - clear duplicate/conflict warnings
- [x] Verify hot-reload and operator registry behavior when same package exists in multiple scopes

### Phase 3: Package Templates & Publishing Workflow

- [x] Create official template repos/checklists for at least (see `../vivid-package-template/README.md`):
  - single-operator package
  - multi-operator package with graphs + tests
- [x] Include standard files in templates (`../vivid-package-template`):
  - `vivid-package.json`, `CMakeLists.txt`, `README.md`, `LICENSE`, `AGENTS.md`
  - package CI smoke workflow (clone vivid-core + run package graphs/tests)
- [ ] Add author docs:
  - naming/versioning conventions
  - dependency guidance
  - release tagging and compatibility notes
- [ ] Add CLI helper (or MCP helper) to scaffold package skeleton from template

### Phase 4: Discovery Surface Decision

- [ ] Evaluate package discovery options and choose one for 1.0:
  - lightweight curated index in repo/catalog JSON
  - web browsing site
  - deferred (documented rationale)
- [ ] If included in 1.0, define minimal viable implementation:
  - metadata format
  - moderation/curation ownership
  - ingestion/update workflow

### Test & Validation Matrix

- [ ] Versioning tests:
  - install exact version, reinstall same version, update to newer version
  - incompatible version warning path
- [ ] Resolution tests:
  - same package in two scopes resolves deterministically
  - uninstall from one scope does not remove others
- [ ] Failure tests:
  - malformed/absent version field
  - unreachable update source
  - conflicting package names from different repos
- [ ] MCP/API tests:
  - update-check endpoint/tool returns structured status
  - package source/scope visible to MCP clients

### Exit Criteria (Milestone 3 complete)

- [ ] Versioned packages are supported and update availability is visible without breaking workflows
- [ ] Package resolution across scopes is deterministic, documented, and test-covered
- [ ] At least one official package template path is documented and validated in CI
- [ ] Discovery strategy for 1.0 is explicitly shipped or explicitly deferred with rationale

## Milestone 4: LLM Perception System

- [ ] Per-node introspection (structured output describing what each node produces)
- [ ] Analysis tools (automated graph-level diagnostics)
- [ ] Assertions (runtime correctness checks)
- [ ] Explore legacy branch analysis work (visual analysis, audio analysis, analysis hints, DSP utilities, spectrogram rendering)
- [ ] Ensure MCP server has comprehensive understanding of Vivid's structure

## Milestone 5: Developer & User Experience

- [ ] Getting Started guide with example graphs
- [ ] Update README.md (fix stale operator list — glitch ops listed but extracted; reflect current state)
- [ ] Evaluate: MovieOut operator vs. menubar record button
- [ ] Error reporting link from the GUI to GitHub Issues
- [ ] Fullscreen / external display output (projectors, LED walls)
- [ ] OSC input for installations and external hardware
- [ ] NDI/Syphon output for routing video to other apps

## Milestone 6: Release Infrastructure

- [ ] GitHub Actions to build macOS releases
- [ ] Versioning system for Vivid itself (user alerts, auto-updates)
- [ ] Redesign the application icon

## Milestone 7: Legacy Branch Evaluation

- [ ] Explore legacy branch for features worth bringing into master
- [ ] Be selective — legacy goals differ from the minimal 1.0 (step sequencer, section-aware export, extended analysis are candidates)

## Milestone 8: Launch

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
