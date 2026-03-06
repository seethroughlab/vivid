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

**Status:** Complete. `test_demo_graphs` loads all demo graphs headlessly, validates no stderr warnings, and runs in CI. Package repos (vivid-3d, vivid-glitch) each have their own smoke test CI that clones vivid-core and runs `test_demo_graphs` against their graphs. Protocol documented in `docs/testing/PACKAGE-SMOKE-TEST.md`.

- [x] Extend demo graph smoke test to also validate no stderr warnings (not just no crashes)
- [x] Add a dedicated CI smoke-test step that runs the full smoke suite (`.github/workflows/smoke.yml`)
- [x] Increase tick count for selected complex graphs (e.g., audio + GPU combo graphs) to catch late-onset issues
- ~~Add vivid-3d demo graphs to the smoke test suite (requires vivid-3d installed as package)~~ — **Architecture correction:** Package smoke tests are **owned by the package repo**, not by vivid-core. Each package's CI clones vivid-core, builds it, and runs `test_demo_graphs` against the package's own `graphs/` directory. Vivid-core's smoke test only covers graphs that ship with vivid itself.
- ~~Add package-dependent graph smoke tests (vivid-glitch graphs, etc.)~~ — Same as above; this is a package-side responsibility.
- [x] Document the package smoke test protocol (how a package's CI should clone vivid-core and run `test_demo_graphs` against its own graphs)
- [x] Based on findings in previous task, implement smoke tests/CI in ../vivid-3d and ../vivid-glitch

### 3. Catalog application functionality for manual testing

**Status:** Complete. Manual test catalog exists at `docs/testing/MANUAL-TEST-CATALOG.md` with functional-area coverage, pass/fail criteria, and macOS-specific notes.

- [x] Create `docs/testing/MANUAL-TEST-CATALOG.md` organized by functional area:
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

**Status:** Test plan documented in `docs/testing/INNER-OUTER-LOOP-TEST-PLAN.md`. Manual execution is still pending.

- [x] Document the expected inner-loop workflow: adjust slider → see visual/audio change in real time
- [x] Manual test checklist:
  - Slider drag updates output continuously (no stutter, no lag)
  - XY-pad drag updates two parameters simultaneously
  - Color picker changes propagate to downstream GPU nodes
  - Typed numeric input validates and clamps to range
  - Undo coalesces continuous drags into one step (blocked on undo system — item 8)

#### Outer loop (operator editing + hot-reload)

**Status:** Workflow and manual checklist documented in `docs/testing/INNER-OUTER-LOOP-TEST-PLAN.md`. End-to-end manual verification across domains is still pending.

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

**Status:** `test_operator_creator.cpp` covers scaffold. MCP `scaffold_operator` tool exists in `mcp_server.cpp`. Manual/E2E plan documented in `docs/testing/OPERATOR-CREATION-MCP-TEST-PLAN.md`. Execution notes are in `docs/archive/OPERATOR-CREATION-MCP-TEST-RESULTS.md` (OC-1 + OC-3 live bridge pass; startup mitigations were required due deferred-probe crashes in specific plugins).

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
- [x] Add a short architecture note (`docs/internal/PACKAGE-ECOSYSTEM-DESIGN.md`) covering:
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

- [x] Finalize canonical scope directories and config knobs (spec: `docs/internal/PACKAGE-SEARCH-PATHS.md`)
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
- [x] Add author docs (`../vivid-package-template/AUTHORING.md`):
  - naming/versioning conventions
  - dependency guidance
  - release tagging and compatibility notes
- [x] Add CLI helper (or MCP helper) to scaffold package skeleton from template (`vivid scaffold-package <name> --template single|multi`)

### Phase 4: Discovery Surface Decision

*Execution note:* 1.0 discovery follows the GitHub-hosted hybrid plan in `docs/internal/PACKAGE-DISCOVERY-SPEC.md`: external website + runtime install bridge, with **no CEF in core**.
Initial artifact shipped: `catalog/packages.json` with current sibling package metadata and preview-image URL placeholders.

- [x] Evaluate package discovery options and choose one for 1.0 (see `docs/internal/PACKAGE-DISCOVERY-SPEC.md`):
  - lightweight curated index in repo/catalog JSON
  - web browsing site
  - deferred (documented rationale)
- [x] If included in 1.0, define minimal viable implementation (see `docs/internal/PACKAGE-DISCOVERY-SPEC.md`):
  - metadata format
  - moderation/curation ownership
  - ingestion/update workflow

### Test & Validation Matrix

- [x] Versioning tests:
  - install exact version, reinstall same version, update to newer version
  - incompatible version warning path
- [x] Resolution tests:
  - same package in two scopes resolves deterministically
  - uninstall from one scope does not remove others
- [x] Failure tests:
  - malformed/absent version field
  - unreachable update source
  - conflicting package names from different repos
- [x] MCP/API tests:
  - update-check endpoint/tool returns structured status
  - package source/scope visible to MCP clients

### Exit Criteria (Milestone 3 complete)

- [x] Versioned packages are supported and update availability is visible without breaking workflows
- [x] Package resolution across scopes is deterministic, documented, and test-covered
- [x] At least one official package template path is documented and validated in CI
- [x] Discovery strategy for 1.0 is explicitly shipped or explicitly deferred with rationale

## Milestone 4: LLM Perception System

Goal: give MCP/LLM workflows a reliable perception loop: inspect current graph state, run automated diagnostics, run checks, and iterate safely.

Legacy references consulted for fit checks (patterns only, no verbatim porting):
- `legacy:docs/ANALYSIS-TOOLS.md`
- `legacy:docs/ASSERTIONS-REFERENCE.md`
- `legacy:src/cli/analysis_hints.cpp`
- `legacy:src/cli/assertion.cpp`
- `legacy:src/cli/include/vivid/analysis_hints.h`
- `legacy:src/cli/include/vivid/assertion.h`
- `legacy:src/cli/mcp_server.cpp`
- `legacy:src/cli/runtime_api.cpp`

### Phase 0: Spec + API Contract

- [x] Define the canonical perception payload schema in docs (see `docs/internal/PERCEPTION-API-SPEC.md`):
  - per-node introspection object
  - graph-level diagnostics object
  - check definition/result object
  - severity model (`critical|warning|info`)
- [x] Decide ownership boundaries (runtime core vs control server vs MCP bridge):
  - what is produced by runtime core vs MCP bridge
  - what is cached vs computed on-demand
- [x] Add a compatibility note for schema versioning (`schema_version` field)

### Phase 1: Per-Node Introspection (Runtime Core)

- [x] Add per-node introspection builder in runtime:
  - node identity (`id`, `type`, `domain`)
  - param snapshot (current values only; metadata references optional)
  - output snapshot (scalar + spread summary)
  - error/health state (errored flag + message when present)
- [x] Include domain-specific extras:
  - audio: RMS/peak/wave summary where available
  - GPU: texture dimensions/format where available
  - control: spread length/type summary
- [x] Expose introspection via control server endpoint (initial `introspect_nodes` payload, MCP-consumable JSON)

### Phase 2: Analysis Tools (Graph-Level Diagnostics)

- [x] Implement first-pass diagnostics over introspection output:
  - disconnected critical sinks/sources
  - missing operator placeholders / unresolved types
  - obvious value anomalies (NaN/Inf/clipping-like flags where detectable)
  - stale/erroring nodes with propagation hints
- [x] Add hint generation pass:
  - prioritized hints list with severity + suggested remediation
  - cap + dedupe strategy (avoid noisy outputs)
- [x] Add deterministic ordering for diagnostics (stable for CI and MCP diffs)

### Phase 3: Checks System

- [x] Define and document storage policy before implementation:
  - checks live in external profile files (for example `checks/dev.json`, `checks/ci.json`)
  - graph stores optional check profile reference only (no embedded full checks by default)
  - control server can accept ad-hoc ephemeral checks in request bodies for one-off runs

- [x] Define checks file format for current graph/runtime model:
  - path-based checks (`exists`, `==`, `!=`, comparison, `between`)
  - optional guards (`when`, `after_frame`/time window equivalent)
- [x] Implement check evaluator over introspection/diagnostics payload
- [x] Add control server endpoint(s):
  - validate checks payload
  - run checks and return structured report
- [x] Ensure check report supports CI consumption:
  - `all_passed`
  - per-check pass/fail/skip + message + observed value

### Phase 4: MCP Surface + Tooling

- [x] Add MCP tools mapped to control-server perception endpoints:
  - get node introspection snapshot
  - run diagnostics
  - run checks
- [x] Keep MCP outputs compact and stable:
  - short summaries + optional detailed payload
  - explicit error envelope for runtime-side failures
- [x] Add MCP docs updates (`docs/LLM-INTEGRATION.md` + tool docs section)

### Phase 5: Legacy Pattern Adoption Pass (Selective)

- [x] Compare implemented payloads to legacy hint/check ergonomics:
  - carry over only ideas that fit current JSON-graph + control-server architecture
- [x] Evaluate advanced metrics backlog (defer unless clearly low-risk/high-value):
  - color harmony / symmetry
  - audio loudness/spectral detail
  - temporal reactivity scoring
- [x] Record accepted vs deferred legacy ideas in roadmap notes

Legacy adoption disposition (Milestone 4):
- **Accepted now:** deterministic severity-ranked diagnostics with stable finding IDs; structured checks with CI-friendly summary/results; compact MCP summaries with optional full payload passthrough.
- **Deferred:** advanced visual/audio perception metrics (color harmony, symmetry, LUFS/spectral scoring) and temporal/cross-domain reactivity scoring windows. These remain post-Milestone-4 backlog items.

### Test & Validation Matrix

- [x] Introspection tests:
  - per-domain node snapshots are structured and stable
  - errored nodes include deterministic health info
- [x] Diagnostics tests:
  - known broken fixture graph yields expected warnings/errors
  - healthy fixture graph yields empty/minimal findings
- [x] Checks tests:
  - passing/failing/guarded checks behave correctly
  - invalid checks schema yields clear validation errors
- [x] MCP/API tests:
  - endpoints return structured JSON envelopes
  - MCP tools preserve key fields and severity semantics
- [x] Regression tests:
  - perception endpoints do not mutate graph/runtime state
  - deterministic output ordering across repeated runs

### Exit Criteria (Milestone 4 complete)

- [x] Per-node introspection is available via runtime API/control server and consumed by MCP
- [x] Graph-level diagnostics produce actionable, severity-ranked findings on fixture graphs
- [x] Checks can be validated and executed headlessly with CI-friendly reports
- [x] Legacy analysis/check patterns have been reviewed and either adopted or explicitly deferred
- [x] Documentation reflects the shipped perception tool surface and schemas

## Milestone 5: Developer & User Experience

Goal: improve first-run onboarding, day-to-day usability, and installation/show workflows without expanding core architecture risk late in 1.0.

### Phase 0: Scope + Baseline

- [x] Freeze Milestone 5 success criteria and non-goals:
  - docs/readme accuracy
  - onboarding quality (Getting Started + graph organization)
  - GUI affordances for error reporting + output workflows
  - external integration priorities (OSC, NDI/Syphon) scoped as implementation-ready or explicitly deferred
- [x] Capture baseline UX pain points from current app/docs:
  - setup friction
  - package/operator discoverability
  - capture/export confusion
  - fullscreen/external display reliability

Phase 0 decisions (2026-03-05):
- **In scope for Milestone 5:** documentation and onboarding quality, graph browse organization, capture/output UX decision, GitHub issue-reporting affordance, fullscreen/external display hardening, and explicit OSC/NDI/Syphon ship-or-defer decisions.
- **Out of scope for Milestone 5:** major architecture changes, new package ecosystem features, non-macOS platform parity, and deep perception-system expansion (handled in other milestones).

Baseline findings (from current repo/docs state):
- `README.md` still has stale/high-level claims (for example MCP tool count wording) and needs tighter alignment with current core/package split and current Milestone 4 checks/perception surface.
- Graph discovery needed stronger in-app navigation than docs-only folder READMEs.
- Getting Started path exists but lacks a curated “first 10 minutes” flow linking graph examples, package libraries, and hot-reload loop.
- Capture/output guidance is split across runtime behavior/docs and does not yet present one clearly designated primary 1.0 workflow.
- GUI issue-reporting affordance to GitHub is not surfaced as a first-class path in current UX docs.
- Fullscreen/external display behavior has checklist coverage but not yet a Milestone-5 hardening pass with explicit acceptance scenarios.

### Phase 1: Onboarding + Documentation

- [x] Publish a practical Getting Started path with example graphs:
  - first 10-minute workflow (load graph, tweak params, hot-reload operator, save variation)
  - curated “starter graph set” with consistent naming/tags by domain/use-case
  - graph directory organization for easier browsing (intro/performance/demo/package examples)
- [x] Update `README.md` to current operator/package reality:
  - remove stale in-core references from extracted packages
  - clearly list core vs sibling package libraries
  - reflect current MCP/perception/checks tool surface
- [x] Add doc cross-links:
  - `README.md` ↔ `docs/ARCHITECTURE.md` / `docs/LLM-INTEGRATION.md` / package template docs
  - Getting Started ↔ demo graph index

### Phase 2: Runtime UX Affordances

- [x] Evaluate and decide output capture UX:
  - `MovieOut` operator path vs menubar record path
  - select one primary 1.0 workflow, document rationale, defer the alternative if needed

Capture UX decision (2026-03-05):
- **Primary 1.0 workflow:** keep the existing top-bar/menubar recording path, backed by core capture/export services and MCP/control-server capture endpoints.
- **Rationale:** synchronized AV export remains in core (required for reliable LLM outer-loop demo generation) without introducing graph-level export node complexity in 1.0.
- **Deferred alternative:** `MovieOut` operator is deferred post-1.0 unless graph-embedded export orchestration becomes a clear user requirement.
- [x] Add error reporting affordance in GUI:
  - GitHub Issues deep-link entry point
  - include useful context payload (version, platform, basic runtime diagnostics)
  - ensure this is non-blocking and does not interrupt render/audio loops

Error-reporting affordance status (2026-03-05):
- Added macOS GUI entry point: `Help -> Report an Issue...`
- Opens GitHub new-issue URL with prefilled diagnostics payload (core version, platform, build mode, graph path, operator/package counts, audio/GPU flags)
- Uses existing non-blocking URL dispatch (`open_url`) so render/audio loop remains uninterrupted

### Phase 3: Display + Installation Workflows

- [x] Fullscreen + external display output hardening:
  - reliable enter/exit fullscreen
  - stable output on external display attach/detach
  - deterministic behavior when display configuration changes mid-session

Display workflow hardening status (2026-03-05):
- Added sink-level controls in `video_out` (`fullscreen`, `display_target`) so output routing/fullscreen behavior is controlled from the render sink itself.
- Implemented stable macOS fullscreen as borderless windowed fullscreen (not monitor-mode switch), sized to full monitor bounds (`glfwGetMonitorPos` + `glfwGetVideoMode`) with menu bar/dock hidden while active.
- Added runtime monitor-topology reconciliation (GLFW monitor callback + frame-loop reconciliation) to retarget fullscreen window or reposition window safely after attach/detach and reconfiguration events.
- Hardened frame/surface transitions around display changes (surface reconfigure on transitions, settle-frame suppression, suboptimal-surface acquire guard, discard-path for invalid in-flight frames) to avoid WebGPU submit-time crashes.
- Added macOS `View -> Toggle Fullscreen` fallback affordance (Cmd+Ctrl+F) for manual control when needed.

### Phase 4: External I/O Expansion

- [x] OSC input/output for installations/hardware:
  - defined minimal 1.0 mapping model via `OscIn`/`OscOut` control operators
  - implemented runtime send/receive path using vendored `oscpack` (core, non-feature-gated)
  - added audio+video demo graphs: `graphs/io/osc_av_in_demo.json`, `graphs/io/osc_av_loopback_demo.json`
- [x] NDI/Syphon output evaluation + implementation plan:
  - Decision (2026-03-05):
    - ship **Syphon output in core** (macOS-first)
    - align **Spout support** with future Windows port (same conceptual output surface)
    - keep **NDI** out of core and implement as a separate package (e.g. `vivid-texshare`)
  - implementation scope for Syphon:
    - implemented as dedicated GPU operators (`SyphonOut`, `SyphonIn`) rather than `video_out` embedding
    - runtime sender naming + receiver server selection
    - demo graphs + runtime validation checklist completed
  - package scope for NDI:
    - prototype/output operator in package repo
    - dependency/licensing isolation from core runtime

External I/O status (2026-03-05):
- OSC is shipped in core via `OscIn`/`OscOut` and validated with demo-graph coverage.
- Syphon output is shipped in core via `SyphonOut` (with GPU-path publish; no CPU readback in publish flow) and validated live with TouchDesigner.
- Syphon input is shipped in core via `SyphonIn` with event-latched frame updates.
- NDI/Syphon decision is set: Syphon is in core, NDI remains a package target, Spout deferred until Windows port.

### Test & Validation Matrix

- [x] Docs/onboarding validation:
  - clean-machine walkthrough completes without hidden steps
  - Getting Started links and example graph paths are valid
  - README claims match actual shipped behavior/operators/packages
- [x] Runtime UX validation:
  - chosen capture/export workflow is discoverable and reliable
  - error-reporting link works and includes expected metadata
- [x] Display validation:
  - fullscreen/external display scenarios pass on macOS test matrix
- [x] External I/O validation:
  - OSC mapping smoke tests (live parameter updates)
  - NDI/Syphon smoke tests (Syphon implemented and validated; NDI deferred by design)

### Exit Criteria (Milestone 5 complete)

- [x] New users can get from launch to meaningful output quickly using Getting Started + curated examples
- [x] Public docs (`README` + key guides) are accurate to current core/package architecture
- [x] Primary capture/output workflow for 1.0 is clear and validated
- [x] GUI includes a practical issue-reporting path to GitHub
- [x] Fullscreen/external display behavior is stable for live use
- [x] OSC and NDI/Syphon are either shipped with baseline coverage or explicitly deferred with rationale

Milestone 5 closure note (2026-03-05):
- Milestone 5 is complete. Core UX goals are shipped: onboarding/docs refresh, display hardening, issue-reporting affordance, and external I/O baseline (OSC + Syphon in core, NDI deferred to package scope with rationale).

## Milestone 6: Release Infrastructure

- [x] GitHub Actions to build macOS releases
  - Added `.github/workflows/release-macos.yml` (tag/manual releases, version guard, codesign/notarize/staple, release assets, appcast generation/publish).
  - Added `scripts/release/generate_appcast.py` and Pages-served `catalog/appcast.xml`.
  - Added `.github/workflows/version-guard.yml` to enforce CMake/runtime version surface consistency.
- [x] Versioning system for Vivid itself (user alerts, auto-updates)
  - Added runtime `AppUpdateManager` appcast fetch/check path with non-blocking startup behavior.
  - Added CLI `vivid check-core-updates`.
  - Added control-server endpoint `check_core_updates` + MCP tool `check_core_updates`.
  - Added settings persistence for core update policy/last-check/skipped version.
  - Added macOS menu actions: `Check for Updates...` and `Automatically Check for Updates`.
  - Added non-intrusive in-app update notice with `Install`, `Skip`, `Later`.
- [x] Redesign the application icon

Milestone 6 follow-up notes:
- Sparkle runtime bridge is integrated via Objective-C runtime lookup; release builds should embed Sparkle framework and set `VIVID_SPARKLE_PUBLIC_KEY`.
- Release operations and secrets are documented in `docs/release/RELEASE-OPS.md`.

## Milestone 7: Legacy Branch Evaluation

- [x] Explore legacy branch for features worth bringing into core and sibling repos
- [x] Apply conservative selection gate (high-fit, low-risk only) and explicitly defer/reject the rest
- [x] Publish decision artifact with ranked shortlist + explicit deferrals/rejections (`docs/internal/LEGACY-EVALUATION-M7.md`)

Milestone 7 closure note (2026-03-06):
- Completed as a planning/evaluation milestone with no runtime/API mutations.
- Package-first pass completed in this order: `vivid-3d`, `vivid-drums`, `vivid-glitch`, `vivid-sequencers`, `vivid-wavetable`, then core.
- `docs/internal/LEGACY-EVALUATION-M7.md` is the source-of-truth for adopt/defer/reject decisions and prerequisites.

Milestone 7 Next Execution Queue (from ranked `adopt_next` shortlist):
1. `M7-SQ-01` (`vivid-sequencers`) - step probability + ratchet support
2. `M7-GL-01` (`vivid-glitch`) - tempo-locked rate helpers
3. `M7-GL-02` (`vivid-glitch`) - anti-click reverse transitions
4. `M7-3D-01` (`vivid-3d`) - GLTF diagnostics/fallback handling
5. `M7-WV-01` (`vivid-wavetable`) - wavetable morph quality improvements

Milestone 7 execution progress:
- [x] `M7-SQ-01` complete in `../vivid-sequencers` (per-step `prob_*` + `ratchet_*` in `Sequencer`, plus regression tests)
- [x] `M7-GL-01` complete in `../vivid-glitch` (shared tempo-locked rate helpers + integration into beat-synced glitch operators, with helper tests)
- [x] `M7-GL-02` complete in `../vivid-glitch` (Reverse wet-path transition ramping for cleaner entry/exit, preserving existing defaults)
- [x] `M7-3D-01` complete in `../vivid-3d` (`MeshImport` diagnostics/fallback improvements, external glTF texture URI support, and fallback marker mesh on load failure)
- [x] `M7-WV-01` complete in `../vivid-wavetable` (cubic periodic wavetable sampling + smoother frame/mip morph blending, with interpolation regression tests)
- [x] `M7-SQ-03` complete in `../vivid-sequencers` (new arp modes `RandomNoRepeat` + `OrderDown`, step-stable random selection per step, and pattern regression tests)
- [x] `M7-3D-02` complete in `../vivid-3d` (Environment3D IBL rotation support via `rotation_y` with correct cache invalidation + environment regression coverage)
- [x] `M7-DR-01` complete in `../vivid-drums` (drum-stack macro demo graphs + package factory presets for layered kit starting points, with package asset regression coverage)
- [x] `M7-CORE-01` complete in core (`HotReloader` queue robustness: in-flight coalescing + single deferred retry for edits during compile, with queue regression test)
- [x] `M7-CORE-02` complete in core (WGSL include preprocessor diagnostics with include-chain/cycle reporting, integrated into `WgslFilter` hot-reload path, plus regression tests)

## Milestone 8: Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation
- [ ] Update ROADMAP.md to reflect completed/deferred items

### Team Workflow Follow-Up: Project-Local Operator Ownership

Goal: make clone/scaffold behavior conducive to team repository workflows by default (project/package-local, not core-repo-local).

Current behavior (2026-03-06):
- `Clone` writes to core `operators/<domain>/...` and appends targets to core `CMakeLists.txt`.
- This is convenient for solo core-dev work but poor for team/project ownership boundaries.

Target behavior:
- Clone/scaffold destination defaults to active project package (or configured project operator root).
- Core repo stays engine-only unless user explicitly chooses "clone into core".
- Graphs persist stable operator type refs independent of physical source location.

Execution checklist (concrete):
1. **Destination policy + config contract**
- [x] Add settings keys:
  - `operator_clone_destination_mode`: `project_default|core_explicit`
  - `project_operator_root`: absolute path (optional)
  - `project_package_name`: package target for scaffold/clone (optional)
- [x] Define resolution order:
  1) explicit user choice in action
  2) graph/workspace-local project package
  3) configured `project_package_name` / `project_operator_root`
  4) fallback core path with warning

2. **Runtime/UI/CLI surface**
- [x] Update node clone flow to choose/write destination via policy (not hardcoded core path).
- [x] Add explicit UI option: `Clone Into -> Project Package | Core`.
- [~] Extend scaffold API/CLI with optional destination:
  - [x] control-server/MCP: `scaffold_operator(..., destination=...)`
  - [x] CLI: `vivid scaffold-operator <name> --domain ... --dest <path|package>`

3. **Project/package integration**
- [x] If destination is package: patch that package's `CMakeLists.txt`/manifest rather than core.
- [x] Ensure hot-reload watcher auto-registers cloned/scaffolded files in project/package roots.
- [x] Ensure install/link workflows preserve expected ownership boundaries.

4. **Migration safety**
- [ ] Keep current core-destination path available behind explicit opt-in during transition.
- [ ] Add non-destructive migration helper:
  - detect cloned operators in core with no upstream usage
  - offer move/copy into project package with patch suggestions (no automatic delete).

5. **Validation matrix**
- [x] Unit tests for destination resolution precedence.
- [x] End-to-end tests:
  - clone into project package -> build/hot-reload works
  - scaffold into project package via MCP -> files + CMake edits correct
  - fallback-to-core path emits clear warning and remains functional
- [ ] Team workflow regression: two repos (core + project package) with clean git diff boundaries.

Minimal first slice (recommended next implementation step):
- Implement destination resolution + new destination parameters in clone/scaffold APIs.
- Support one project destination mode: linked package (`vivid link ...`) as default target.
- Keep core fallback; no automatic migration yet.

Minimal first slice status (2026-03-06):
- `scaffold_operator` now accepts optional `destination` (`core`, `package:<name>`, or absolute path) and defaults to first linked package when available.
- UI create-operator flow now defaults to first linked package destination when available, with fallback to core source tree.
- Package destination scaffolding writes to `src/<name>.cpp` (and `.wgsl` for GPU) and patches package `CMakeLists.txt` ops list.
- Project-default destination now only selects linked packages in `local/workspace` scopes (not user/global installs), preserving ownership boundaries for installed dependencies.
- Destination policy contract is now centralized and shared across runtime clone/create, CLI scaffold, and control-server scaffold via settings-backed precedence resolution.
- E2E trio coverage is now automated and passing:
  - `test_undo_mutation_types`: clone into linked project package writes `src/`, patches package CMake ops, and queues package hot-reload target.
  - `test_control_server`: MCP `scaffold_operator` writes to package destination and validates fallback-to-core warning path.

---

## Open Questions

### Semantic Parameter Tags Plan (Large Feature, post-1.0)

Goal: add machine-readable parameter semantics (for LLM hints, safer auto-wiring, and better defaults) without breaking existing operators or graphs.

Status: planning only; implementation deferred past 1.0.

Phase 0: Spec + taxonomy baseline
- [x] Define initial controlled vocabulary (`frequency`, `amplitude`, `gate`, `phase`, `time_seconds`, `bpm`, `color_rgba`, `position_xy`, `seed`, etc.).
- [x] Define value-shape metadata (`scalar`, `vec2`, `vec3`, `color`, `enum`, `pattern`, `event`).
- [x] Define compatibility rules: unknown tags ignored; untagged params remain valid.
- [x] Publish canonical contract doc (`docs/SEMANTIC-PARAM-TAGS.md`) with examples and anti-patterns.

Phase 1: Operator API + runtime surface
- [x] Extend `ParamBase` metadata to carry semantic tag(s) and optional units/range intent.
- [x] Keep source compatibility: existing operators compile unchanged.
- [x] Expose tags through existing introspection/control surfaces (Runtime API, control server, MCP).
- [x] Ensure serialization boundaries are explicit: tags live in operator code/metadata, not required in graph JSON values.

Phase 2: Seed operator adoption (core + sibling packages)
- [ ] Tag core seed operators first (audio/control/gpu high-traffic params).
- [ ] Tag sibling package operators (`../vivid-3d`, `../vivid-drums`, `../vivid-glitch`, `../vivid-sequencers`, `../vivid-wavetable`, `../vivid-cef`) using same vocabulary.
- [x] Add lint/check script to detect invalid or unknown tags in operator metadata.
- [ ] Keep rollout incremental: partial coverage is acceptable while contract stabilizes.

Phase 2 status (2026-03-06, core wave 1):
- Added semantic tags to high-traffic core operators:
  - Control: `LFO`, `Clock`, `Envelope`
  - Audio: `Oscillator`, `Gain`
  - GPU: `Shape`, `Noise`, `Bars`
- Regression check: `test_control_server` passes with semantic metadata API surfaces enabled.

Phase 2 status (2026-03-06, sibling wave 1):
- Added semantic tags in sibling repos:
  - `../vivid-3d`: `Transform3D`
  - `../vivid-drums`: `DrumKick`, `DrumSnare`, `DrumHiHat`, `DrumClap`, `DrumCymbal`, `DrumTom`
  - `../vivid-glitch`: `Reverse`
  - `../vivid-sequencers`: `Sequencer`
  - `../vivid-wavetable`: `WavetableSynth` (core envelope/filter semantics)
  - `../vivid-cef`: `BrowserOp`
- Added validator script: `scripts/validate_semantic_tags.sh` (core + sibling paths supported).
- Validation results:
  - `vivid-3d`: build + 16/16 tests pass
  - `vivid-drums`: build + tests pass
  - `vivid-glitch`: build + tests pass
  - `vivid-sequencers`: build + tests pass (`test_pattern_algebra` passes after core plugin ABI rebuild)
  - `vivid-wavetable`: build + tests pass
  - `vivid-cef`: rebuild succeeds (`ctest`: no tests defined)

Phase 2 status (2026-03-06, core wave 2):
- Added semantic tags to additional core operators:
  - Control: `Random`, `Gate`, `ModulatedGain`
  - Audio: `Delay`, `Reverb`, `Bitcrush`, `Distortion`
  - GPU: `Composite`, `Feedback`, `Bloom`
- Validation results:
  - Semantic tag validator passes: `scripts/validate_semantic_tags.sh operators src tests`
  - Targeted builds pass for all updated operators.
  - Control-server semantic metadata regression passes from build dir: `./test_control_server .`

Phase 2 status (2026-03-06, core wave 3):
- Added semantic tags to core media/I/O operators:
  - GPU: `MovieFileIn`, `WebcamIn`
  - Audio: `MovieFileAudioIn`
  - Control: `OscIn`, `OscOut`
- Validation results:
  - Semantic tag validator still passes.
  - Targeted builds pass for updated operators.
  - Control-server regression passes from build dir: `./test_control_server .`

Phase 3: Tooling + UX integration
- [x] UI: show semantic hint text in parameter inspector (non-intrusive).
- [x] MCP/control: include semantic tags in parameter schema responses used by LLM tools.
- [x] Scaffold/clone templates: include optional semantic-tag examples so generated operators start with best practices.
- [x] Add docs section in operator-authoring guides for when to tag and when not to tag.

Phase 3 status (2026-03-06, scaffold template semantics):
- `OperatorCreator` templates now seed semantic metadata examples in generated operators:
  - control/audio/gpu default templates include `semantic_tag` + `semantic_shape` on starter params
  - composite control template includes semantic examples for modulation/time params (`lfo_rate`, `smooth_time`, etc.)
- `test_operator_creator` now asserts semantic metadata snippets are present in scaffolded sources.

Phase 4: Behavior improvements guarded by tags
- [x] LLM-assisted operator creation: prefer semantically compatible defaults when connecting nodes.
- [x] Auto-wiring suggestions: rank candidate ports by semantic compatibility before simple type match ties.
- [x] Safe coercion rules (example: `time_ms` -> `time_seconds`) only when explicitly defined in contract.
- [x] No hidden mutations: all inferred mappings must be inspectable/overridable by user.

Phase 4 status (2026-03-06, step 1):
- Control-server `connect` now accepts optional `semantic_defaults` (bool).
- MCP `connect` enables `semantic_defaults` by default for LLM workflows.
- RuntimeAPI connect applies tag-guarded default remap only for parameter-to-parameter connections:
  - same-tag range mapping (when non-identity)
  - explicit conversion pairs: `time_milliseconds` <-> `time_seconds`, `rotation_degrees` <-> `rotation_radians`
- Regression coverage added in `test_runtime_api` with deterministic semantic test operators.

Phase 4 status (2026-03-06, step 2):
- Insert-on-wire auto wiring now ranks candidate input/output ports by semantic-tag compatibility
  before falling back to plain type compatibility.
- Destination param picker now ranks compatible inputs/params by semantic-tag match against the
  source endpoint tag, preserving existing order when semantic tags are absent.
- Descriptor lookup now works for both deferred and already-loaded operators during semantic
  auto-wiring (`OperatorRegistry::probe_descriptor` includes loaded operators).
- Regression validation passes:
  - `./build/test_ui_overlay_interactions`
  - `./build/test_control_server .`

Phase 4 status (2026-03-06, step 3):
- Semantic coercion is now contract-gated:
  - runtime only applies different-tag remap for explicit rule table pairs
  - non-listed tag pairs are left unchanged (no hidden conversion)
- Coercion contract documented in `docs/SEMANTIC-PARAM-TAGS.md` under
  "Explicit Coercion Rules (Behavior Contract)".
- Regression coverage added in `test_control_server`:
  - explicit pair applies remap (`time_milliseconds` -> `time_seconds`)
  - non-listed pair does not apply remap (`frequency_hz` -> `time_seconds`)

Phase 4 status (2026-03-06, step 4):
- Control-server `connect` now makes inferred remaps explicit in response payload when semantic defaults are enabled:
  - `inferred_remap_applied` (bool)
  - `inferred_remap` object (`from_min`, `from_max`, `to_min`, `to_max`, `clamp`) when applied
- Inferred remaps remain inspectable through `inspect_graph` wire fields.
- Inferred remaps are user-overridable via `set_connection_remap`; regression coverage verifies
  override values replace inferred defaults and persist in `inspect_graph`.

Phase 5: Validation matrix + rollout gate
- [x] Unit tests: taxonomy parser/validator, metadata propagation, unknown-tag tolerance.
- [x] Integration tests: introspection/control/MCP parity for tagged vs untagged params.
- [x] Regression tests: untagged legacy graphs/operators behave exactly as before.
- [ ] Exit gate for activation of tag-driven behaviors:
  - [x] stable taxonomy version
  - [x] core seed coverage target met
  - [x] sibling package baseline coverage met
  - [ ] no graph/load/runtime regressions in CI.

Phase 5 status (2026-03-06):
- Unit/contract validation:
  - semantic taxonomy validator passes for core + sibling repos:
    - `./scripts/validate_semantic_tags.sh operators src tests ...<sibling roots>...`
  - control-server matrix validates semantic metadata propagation and unknown-tag tolerance:
    - `./build/test_control_server .`
    - includes extension-tag operator (`x_test_unknown_scalar`) and untagged operator parity checks
- Integration parity:
  - tagged/untagged metadata parity covered through `list_types`, `inspect_graph`, and `introspect_nodes`
    in `test_control_server`.
  - MCP regression baseline remains green:
    - `python3 mcp/test_vivid_mcp_perception.py`
- Legacy regression coverage:
  - demo graph smoke suite remains green (31 pass / 0 fail / 4 external-I/O skips):
    - `./build/test_demo_graphs ./build/graphs`
- Rollout gate:
  - taxonomy/core/sibling readiness checks are complete.
  - CI-wide regression confirmation remains open until GitHub CI run is green.

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



## Extra Findings

[x] Developer & User Experience -- graph discovery now uses recursive folder scan + per-graph `meta` and in-app **Open Example...** search/filter UI.
[x] Team workflow issue documented and converted into concrete launch follow-up plan: **Project-Local Operator Ownership** (see Milestone 8 section above).
[ ] Check if PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
[ ] Clean up docs directory. There should really only be user-facing documents in there. The rest should be either in subfolders (eg: archive), and/or some should be gitignored because they're just for me.
[ ] Clean up the ROADMAP - remove what has been done, but be conservative in case some unfinished items could benefit from the context of finished items. 
