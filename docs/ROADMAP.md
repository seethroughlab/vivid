# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Milestone 1: Core Stability & Testing

**Status:** Mostly complete.

Conservative completed summary retained for context:
- Core test matrix, smoke coverage, package install/link/unlink flows, manual test catalog, and undo/redo coverage were implemented and validated.
- Package smoke-test ownership was correctly moved to package repos (core smoke covers core-shipped graphs only).
- Milestone 1 testing/docs artifacts live under `docs/testing/` and `docs/archive/` as needed.

Remaining active test items:

### Automated Coverage Gaps

- [ ] Add MIDI input tests: note on/off mapping, CC mapping, channel filtering *(hardware-dependent; best handled via virtual MIDI loopback integration suite)*.

### Inner/Outer Loop Manual Verification

- [ ] Verify hot-reload for each domain:
  - Control operators
  - Audio operators
  - GPU operators
- [ ] Test outer-loop error cases:
  - Syntax error in `.cpp` keeps last good version and surfaces clear error
  - Missing include has same graceful behavior
  - Editing linked package operator triggers hot-reload from linked source
- [ ] Verify state preservation across reload:
  - parameter values retained
  - wire connections retained
  - node positions retained

### Operator Creation + MCP E2E

- [ ] End-to-end test: scaffold -> edit implementation -> hot-reload -> use in graph -> verify output
- [ ] Verify all domain variants:
  - Control operator
  - Audio operator
  - GPU operator
  - Composite operator (multi-domain)
- [ ] Verify MCP `scaffold_operator` via control-server request path
- [ ] Verify LLM-guided workflow end-to-end (scaffold/edit/reload/validate)
- [ ] Verify scaffold into existing package directory (not just top-level operators path)
- [ ] Verify generated code compiles without warnings on first build

---

## Milestone 2: Operator Extraction

**Status:** Complete.

Conservative summary retained for context:
- Core operator surface was reduced to the minimal 1.0 set.
- Domain packages extracted and operational: `vivid-wavetable`, `vivid-drums`, `vivid-plexus`, `vivid-sequencers`.
- Shared operator API updates shipped (`drum_dsp.h`, `audio_dsp.h` exposure), with package CI/smoke coverage and install/uninstall validation.

## Milestone 3: Package Ecosystem

**Status:** Complete.

Conservative summary retained for context:
- Version-aware package metadata + update-check surfaces are shipped (CLI/control/MCP).
- Deterministic multi-scope resolver and diagnostics are shipped.
- Package template/scaffold workflow is shipped.
- Discovery decision and baseline implementation are shipped (`catalog/packages.json` + GitHub-hosted catalog path).

## Milestone 4: LLM Perception System

**Status:** Complete.

Conservative summary retained for context:
- Introspection, diagnostics, and checks are shipped and exposed through control server + MCP.
- Deterministic ordering and CI-friendly check reports are in place.
- Legacy patterns were reviewed with explicit adopt/defer outcomes.

## Milestone 5: Developer & User Experience

**Status:** Complete.

Conservative summary retained for context:
- Docs/onboarding refresh shipped (README + Getting Started + example browser flow).
- Runtime UX improvements shipped (error-reporting affordance, fullscreen/display hardening).
- External I/O baseline shipped (OSC + Syphon in core; NDI deferred to package scope).

## Milestone 6: Release Infrastructure

**Status:** Complete.

Conservative summary retained for context:
- macOS release pipeline + notarization path shipped.
- Core update system shipped (appcast, runtime check surface, CLI/control/MCP integration).
- App icon refresh shipped.

## Milestone 7: Legacy Branch Evaluation

**Status:** Complete.

Conservative summary retained for context:
- Evaluation artifact lives in `docs/internal/LEGACY-EVALUATION-M7.md`.
- Highest-value shortlist items were executed across core + sibling repos.
- Remaining legacy candidates are explicitly deferred/rejected with rationale in the artifact.

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

**Current status:** Implementation is largely complete; rollout is in stabilization.

Conservative completed context retained:
- Spec + taxonomy baseline is defined in `docs/SEMANTIC-PARAM-TAGS.md`.
- Runtime/control/MCP semantic metadata surfaces are shipped.
- Tag-driven behavior improvements are shipped:
  - semantic default remap on connect
  - semantic-ranked auto-wiring suggestions
  - explicit coercion-contract enforcement
  - inspectable + user-overridable inferred remaps
- Validation matrix is green locally (`validate_semantic_tags`, `test_control_server`, MCP + demo smoke checks).

Remaining active items:
- [ ] Finish broader seed-tag adoption coverage (core + sibling operators still not fully tagged).
- [ ] Keep incremental rollout discipline while taxonomy stabilizes (avoid over-tagging low-confidence params).
- [ ] Close final rollout gate:
  - [ ] no graph/load/runtime regressions in CI.

---

## Deferred Past 1.0

These are acknowledged but explicitly out of scope for the initial release:

- Subpatches
- Simulation zones (frame-to-frame feedback)
- Multi-window / multi-monitor
- Windows / Linux
- Bundled compiler
- WebSocket API — external process integration over WebSocket; enables non-MCP clients to control Vivid programmatically
- Project file format (single JSON vs. directory with assets)
- Library version pinning
- Accessibility



## Extra Findings

- [ ] Check if PR was accepted, switch back to main repo: https://github.com/gfx-rs/wgpu-native/pull/557
