# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Milestone 1: Core Stability & Testing

**Status:** Exit gate closed — architecture integrity, cross-domain correctness, and CI proof confirmed. Remaining items are manual QA (hot-reload, operator creation + MCP E2E).

### Movie Playback Exit Gate

The `MovieLoaded` / `MovieVideoOut` / `MovieAudioOut` trio is shipped and the implementation phases are complete. The following acceptance criteria need a final sign-off before the exit gate closes.

- [x] Architecture integrity: no media-specific runtime service; operator stack is package-portable
- [x] Sync stability: thresholds validated by `test_movie_long_loop_sync` and runtime instrumentation

  | Metric | Green (pass) | Yellow (warn) | Red (fail) |
  |--------|-------------|---------------|------------|
  | `audio_underrun_frames` per loop cycle | 0 | ≤512 (~10 ms) | >512 or progressive |
  | `sync_resync_applied` per 60 s steady state | 0–1 | 2–5 | >5 |
  | `video_payload_dropped` per loop cycle | 0 | 1–2 | >2 or progressive |
  | Loop boundary settle time | <0.5 s | 0.5–2 s | >2 s |

  Validated codecs: HAP, HAPQ, HAP-alpha, H.264, HEVC.

- [x] Threading correctness: RT-safe audio callback, crash-free teardown, stale-command rejection tested
- [x] Cross-domain data correctness: `media_stream_v1` / `media_clock_v1` verified across control/audio/GPU domains
- [x] Codec/regression behavior: HAP BC path, AVF fallback, HAPQ YCoCg all functional
- [x] Migration outcome: legacy operators removed; canonical demos use `MovieLoaded` trio
- [x] **Exit gate closed** (all six confirmed)

### Automated Coverage Gaps

- [ ] MIDI input tests *(hardware-dependent; likely deferred to post-1.0 virtual MIDI loopback suite)*
- [x] CI proof: MovieLoaded + long-loop tests passing in default macOS CTest run

### Manual Verification: Hot-Reload

- [ ] Hot-reload verified for each domain: Control, Audio, GPU
- [ ] Error cases: syntax error keeps last good version; missing include same; editing linked package operator reloads from source
- [ ] State preservation across reload: params, wires, node positions

### Manual Verification: Operator Creation + MCP E2E

- [ ] End-to-end: scaffold → edit → hot-reload → use in graph → verify output
- [ ] All domain variants: Control, Audio, GPU, Composite
- [ ] MCP `scaffold_operator` via control-server request path
- [ ] LLM-guided workflow end-to-end
- [ ] Scaffold into existing package directory (not just top-level `operators/`)
- [ ] Generated code compiles without warnings on first build

---

## Milestones 2–7: Complete

- **M2 Operator Extraction:** Core operator surface reduced to 1.0 set; domain packages extracted (`vivid-wavetable`, `vivid-drums`, `vivid-plexus`, `vivid-sequencers`).
- **M3 Package Ecosystem:** Version-aware metadata, deterministic multi-scope resolver, scaffold workflow, and discovery baseline shipped.
- **M4 LLM Perception System:** Introspection, diagnostics, and checks shipped through control server and MCP.
- **M5 Developer & User Experience:** Docs/onboarding refresh, runtime UX improvements, and external I/O baseline (OSC + Syphon) shipped.
- **M6 Release Infrastructure:** macOS release pipeline, notarization, update system, and app icon shipped.
- **M7 Legacy Branch Evaluation:** Evaluation complete; highest-value items executed; remainder deferred/rejected with rationale in `docs/internal/LEGACY-EVALUATION-M7.md`.

---

## Milestone 8: First-Class GPU Port Types

**Status:** Complete.

- **New ABI-stable structs** (`src/operator_api/gpu_types.h`): `VividGpuBuffer`, `VividComputeBuffer`, `VividVertexAttribute`, `VividMesh` — C-compatible, include-safe, no `data_type` string matching.
- **Three new `VividPortType` enum values**: `VIVID_PORT_HANDLE` now covers GPU buffer, mesh, and compute types (type-safe via `handle_type_id`).
- **`VividGpuState` extended** (`src/operator_api/gpu_operator.h`): six new pointer/count pairs for buffer, mesh, and compute I/O — operators access typed inputs and write typed outputs through the same `ctx->gpu` handle they already use.
- **Scheduler wiring** (`src/runtime/scheduler.h` / `scheduler.cpp`): `NodeState` carries per-type port-index vectors and resolved-input vectors; `Wire` carries three new flags (`is_buffer_wire`, `is_mesh_wire`, `is_compute_wire`); `build()` classifies wires with enum-level mismatch rejection; `tick()` resolves inputs from upstream `NodeState` and commits outputs after `process()`.
- **UI compatibility** (`src/ui/node_graph_util.h`): `port_type_compatible()` enforces exact-match for all three new types — cross-type connections are rejected at drag time.
- **Control server** (`src/runtime/control_server.cpp`): `port_type_str()` maps the three new enum values to `"gpu_buffer"`, `"gpu_mesh"`, `"gpu_compute"` for JSON serialization.
- **No visual changes needed**: all three types use the existing `kGpuAccent` cyan via `domain_color(VIVID_DOMAIN_GPU)`; no `type_suffix` suffix is shown (correct behavior).

---

## Milestone 9: Multiple Output Ports

Multiple scalar, spread, string, and texture outputs already work at the API and serialization
layers. Three gaps remain before the capability is fully general.

**ABI (version bump 4→5 shipped):**
- [x] Extend `VividProcessContext.output_data` from single `void*` to indexed `void**` array
      to support operators with more than one `VIVID_PORT_HANDLE` output
- [x] Update scheduler `NodeState` and tick dispatch accordingly (`gpu_data_outputs`,
      `data_output_port_indices`, `output_data_buf`)

**Runtime/GPU:**
- [x] Generalized `gpu_depth_texture` special case into a proper multi-texture output vector
      (`aux_texture_output_port_indices`, `aux_gpu_textures`, `aux_gpu_texture_views` in
      NodeState; `aux_output_texture_views`/`aux_output_texture_count` in VividGpuState; ABI 6)

**UI:**
- [x] Replace the hard ≤3 output-port visibility threshold in `count_visible_output_ports()`
      (`src/ui/node_graph.cpp:110`) with a user-expandable affordance for operators with many outputs.
      Nodes with >3 outputs show connected ports + a `"▸ N more…"` row; click to expand/collapse.
- [x] Update scaffold templates and `scaffold_operator` to support declaring multiple named
      outputs at creation time via `--outputs "name:type,..."` flag (coordinate with M11 operator
      creation modal)

---

## Milestone 10: Versioning Strategy ✓

ABI versioning exists but does not cover the full compatibility surface across graphs, themes, and
operators. Define the scheme before 1.0 locks the serialization format.

### Decisions

- **Version granularity: per-package, not per-operator.** Operators are grouped in packages;
  `vivid-package.json` already carries a SemVer `version` field. Per-operator tracking would bloat
  graph files with no independent update mechanism to justify it.

- **Version metadata lives in the manifest only; graphs record a package snapshot at save time.**
  No operator-header field needed — `OperatorRegistry` already resolves type name → package name
  at runtime, and `PackageManager` can supply the installed version.

- **Graph negotiation policy: hard reject on schema version bump, best-effort + diagnostics on
  package version mismatch.** A future `schema_version > GRAPH_SCHEMA_VERSION` means the engine
  cannot safely interpret the file — fail fast with a clear error. A stale `pkg_version` on a node
  means the user's package has changed since they saved — warn, don't reject, since forcing a
  hard reject on a working graph is too destructive pre-1.0. Use `IncompatibleUpdate` vs.
  `CompatibleUpdate` classification (already in `PackageUpdateAssessment`) to grade the warning.

- **Theme negotiation policy: best-effort load, never a hard reject.** Themes are pure presentation
  data; missing keys fall back to `default_style()`. A major-version skew emits a single stderr
  warning.

### Graph Serialization

- [x] Define `GRAPH_SCHEMA_VERSION 1` compile-time constant (in `graph.h`)
- [x] Add `schema_version` (int) and `vivid_version` (string) to the graph JSON root; write them
  at save time, read at load time; hard-reject if `schema_version > GRAPH_SCHEMA_VERSION`; treat
  absent `schema_version` as `1` for backward compat
- [x] Add optional `"pkg": {"name": "...", "version": "..."}` sub-object to each node entry;
  omit for core operators, WGSL filters, and operators with no package manifest; populate at
  save time by querying `OperatorRegistry::package_for_type` + `PackageManager::list()`
- [x] Extend `NodeDef` in `graph.h` with `pkg_name` / `pkg_version` fields; extend `Graph` with
  `schema_version` / `vivid_version` fields
- [x] Update demo graphs in `graphs/` to include root-level `schema_version` and `vivid_version`
  (node `"pkg"` fields omit — demos use core operators only)

**Graph JSON shape (additions only):**
```json
{
  "schema_version": 1,
  "vivid_version": "0.1.0",
  "nodes": {
    "drum_kick1": {
      "type": "audio/drum_kick",
      "pkg": { "name": "vivid-drums", "version": "0.1.0" },
      "params": {}, "layout": { "x": 120, "y": 80 }
    }
  }
}
```

### Package Version Mismatch Diagnostics

- [x] Define `Graph::LoadDiagnostic` struct (node_id, pkg_name, saved_version, installed_version,
  classification string) in `graph.h` as a nested struct; stored in `graph.load_diagnostics`
- [x] After `Graph::load`, iterate nodes with non-empty `pkg_name`; cross-reference against
  `PackageManager::list()`; classify via `classify_version_delta()`; emit `IncompatibleUpdate` as
  stderr warning, `CompatibleUpdate` as stderr-only; done in `run_graph_package_diagnostics` lambda
- [x] Added `PackageManager::classify_version_delta(saved, installed)` static helper using
  existing `parse_semver_triplet` / `compare_semver` — no duplicate SemVer parsing
- [x] Expose diagnostics via MCP `get_graph_load_diagnostics` control-server endpoint

### Theme Versioning

- [x] Add `"vivid_version": "0.1.0"` to all 8 embedded theme JSON constants in `theme_loader.cpp`
- [x] Parse `"vivid_version"` in `parse_theme_root`; store it in `UIStyle`; if major component
  differs from `VIVID_CORE_VERSION`, emit a single stderr warning
- [x] Stamp embedded via constants (already contains vivid_version); `ensure_default_themes`
  writes them verbatim — no extra write code needed

**Theme JSON shape (addition only):**
```json
{ "vivid_version": "0.1.0", "name": "Dark Steel", "corner_radius": 0, ... }
```

### Critical Files

- `src/runtime/graph.h` — extend `NodeDef`, `Graph`; define `GraphLoadDiagnostic`
- `src/runtime/graph.cpp` — `parse_doc` + `build_graph_json_doc` (read/write new fields)
- `src/runtime/main.cpp` — load-time diagnostics; define `GRAPH_SCHEMA_VERSION`
- `src/ui/theme_loader.cpp` — embedded constants + `parse_theme_root` + `ensure_default_themes`
- `src/runtime/package_manager.cpp` — reuse point only (no new code)

---

## Milestone 11: Operator Creation Modal

**Status:** Complete — full-options modal shipped with domain selection, dynamic port/param rows, composite variant, destination chooser, and "Create Empty" path.

- [x] Design full-options modal: domain selection, port types + count, param scaffolding (name/type/default), destination (project package vs. core)
- [x] Include "create empty" path for advanced users who want a blank slate without the wizard
- [x] GUI and MCP scaffold paths intentionally diverge: GUI uses the interactive modal, MCP uses the `scaffold_operator` JSON API directly — both call `OperatorCreator::create()` on the backend

---

## Milestone 12: Solo Mode

**Status:** Complete — solo mode implemented across scheduler, audio engine, GPU sink override, and UI.

- [x] Define GPU solo semantics: soloed node becomes the effective GPU sink; its output texture feeds the display. Downstream nodes are skipped. If the soloed node has no texture output, GPU rendering continues normally.
- [x] Define audio solo semantics: mute all audio nodes NOT in the solo upstream set. The audio_out sink still runs but receives silence from non-solo paths. If a non-audio node is soloed, audio continues normally.
- [x] Define cross-domain behavior: compute the transitive upstream closure of the soloed node across ALL domains. If a GPU node depends on a control LFO, that LFO still runs. If it transitively depends on an audio analysis node, the audio chain up to that point still runs.
- [x] Decide scope: session-only UI affordance. Not serialized with the graph. Cleared on graph reload.
- [x] Implement: scheduler `set_solo()` with BFS upstream closure, tick() skip logic, `find_effective_gpu_sink()`, audio engine solo bridging via `ParamSnapshot`, RuntimeAPI `set_solo()`/`solo_node_id()`, context menu Solo/Unsolo item, 'S' keyboard shortcut, gold border + dimmed visual indicators, `set_solo`/`get_solo` control server endpoints

---

## Milestone 13: Semantic Parameter Tags Rollout

**Status:** Complete — semantic tags applied across all domains; validation script integrated into CTest and CI.

- [x] Finish broader seed-tag adoption (core + sibling operators not fully tagged)
- [x] Close final CI rollout gate: no graph/load/runtime regressions

---

## Milestone 14: Launch

- [ ] YouTube video
- [ ] Finalize and proof all documentation
- [ ] Update ROADMAP.md to reflect completed/deferred items

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
