# Vivid 1.0 Roadmap

Vivid 1.0 is a polished, reliable creative coding environment for macOS. The goal is to ship the *environment* — the node graph, the package system, the hot-reload workflow, the MCP integration — not to maximize the operator count. Operators are user-extensible; the platform needs to be solid.

Windows and Linux support is deferred past 1.0.

---

## Milestone 1: Core Stability & Testing

**Status:** In progress — movie playback architecture and project-local operator ownership complete; remaining items are exit-gate verification and manual QA.

### Movie Playback Exit Gate

The `MovieLoaded` / `MovieVideoOut` / `MovieAudioOut` trio is shipped and the implementation phases are complete. The following acceptance criteria need a final sign-off before the exit gate closes.

- [ ] Architecture integrity: no media-specific runtime service; operator stack is package-portable
- [x] Sync stability: thresholds validated by `test_movie_long_loop_sync` and runtime instrumentation

  | Metric | Green (pass) | Yellow (warn) | Red (fail) |
  |--------|-------------|---------------|------------|
  | `audio_underrun_frames` per loop cycle | 0 | ≤512 (~10 ms) | >512 or progressive |
  | `sync_resync_applied` per 60 s steady state | 0–1 | 2–5 | >5 |
  | `video_payload_dropped` per loop cycle | 0 | 1–2 | >2 or progressive |
  | Loop boundary settle time | <0.5 s | 0.5–2 s | >2 s |

  Validated codecs: HAP, HAPQ, HAP-alpha, H.264, HEVC.

- [x] Threading correctness: RT-safe audio callback, crash-free teardown, stale-command rejection tested
- [ ] Cross-domain data correctness: `media_stream_v1` / `media_clock_v1` verified across control/audio/GPU domains
- [x] Codec/regression behavior: HAP BC path, AVF fallback, HAPQ YCoCg all functional
- [x] Migration outcome: legacy operators removed; canonical demos use `MovieLoaded` trio
- [ ] **Exit gate closed** (all six confirmed)

### Automated Coverage Gaps

- [ ] MIDI input tests *(hardware-dependent; likely deferred to post-1.0 virtual MIDI loopback suite)*
- [ ] CI proof: MovieLoaded + long-loop tests passing in default macOS CTest run

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

## Milestone 8: Port Domain Naming

Resolve the "GPU" label ambiguity before it gets baked deeper into serialization, documentation, and user muscle memory. Vertex buffers are as GPU-resident as textures; the current label is misleading.

### Decision

- [ ] Decide on replacement label (`Render`, `Visual`, `Video`, `Graphics`, or other) or explicitly defer with rationale
  - Criteria: accuracy (covers textures, shaders, geometry), distinctness from Audio/Control, brevity in UI, familiarity to creative-coding users

### Enum & API Surface

- [ ] Rename `VIVID_DOMAIN_GPU` in `src/operator_api/types.h`
- [ ] Rename `VIVID_PORT_GPU_TEXTURE` in `VividPortType` enum (same file)
- [ ] Update `domain_str()` in `src/runtime/control_server.cpp` — this is the canonical enum→string mapping used in JSON serialization
- [ ] Update `domain_subdir()` in `src/runtime/operator_creator.cpp` (directory name for scaffolded operators)
- [ ] Update domain validation in `src/runtime/main.cpp` CLI scaffold (`--domain` flag accepts new label)
- [ ] Update `is_gpu` flag and domain checks in `src/runtime/scheduler.h` / `scheduler.cpp`
- [ ] Update `video_out` builtin domain assignment in `src/runtime/builtin_operators.cpp`

### Serialization & Migration

- [ ] Define migration strategy for existing saved graphs that contain the `"gpu"` string
  - Option A: accept both `"gpu"` and the new label on load, always write the new label on save (rolling migration)
  - Option B: one-time migration pass on load, reject old label after a version cutoff
- [ ] Implement chosen migration in graph load path (`control_server.cpp` deserialization)
- [ ] Verify round-trip: load a pre-rename graph → save → reload → no data loss or domain misassignment

### UI & Styling

- [ ] Rename user-facing `domain_labels[]` in `src/ui/node_graph_draw.cpp`
- [ ] Rename `kGpuAccent` color constant in `src/ui/node_graph_constants.h` (and `domain_color()` helper)
- [ ] Verify the Create Operator popup, patch bay, and node headers all display the new label

### Scaffold Templates

- [ ] Update GPU template domain assignment in `src/runtime/operator_creator.cpp`
- [ ] Update CMake insertion markers that reference the GPU domain (same file)
- [ ] Verify `vivid scaffold-operator <name> --domain <new-label>` produces a compilable operator

### Documentation

- [ ] Update `docs/INTERFACE.md` — domain references, theme key names (`domain.gpu` → new key)
- [ ] Update `docs/PRD.md` — architecture and port-type sections
- [ ] Update any other docs that reference "GPU domain" or "GPU operators"

### Tests

- [ ] Update test operator descriptors in `tests/operators/` that use `VIVID_DOMAIN_GPU`
- [ ] Confirm all CTests pass after rename

---

## Milestone 9: Multiple Output Ports

Multiple scalar, spread, string, and texture outputs already work at the API and serialization
layers. Three gaps remain before the capability is fully general.

**ABI (version bump 4→5 shipped):**
- [x] Extend `VividProcessContext.output_data` from single `void*` to indexed `void**` array
      to support operators with more than one `VIVID_PORT_DATA` output
- [x] Update scheduler `NodeState` and tick dispatch accordingly (`gpu_data_outputs`,
      `data_output_port_indices`, `output_data_buf`)

**Runtime/GPU:**
- [x] Generalized `gpu_depth_texture` special case into a proper multi-texture output vector
      (`aux_texture_output_port_indices`, `aux_gpu_textures`, `aux_gpu_texture_views` in
      NodeState; `aux_output_texture_views`/`aux_output_texture_count` in VividGpuState; ABI 6)

**UI:**
- [ ] Replace the hard ≤3 output-port visibility threshold in `count_visible_output_ports()`
      (`src/ui/node_graph.cpp:110`) with a user-expandable affordance for operators with many outputs
- [ ] Update scaffold templates and `scaffold_operator` to support declaring multiple named
      outputs at creation time (coordinate with M11 operator creation modal)

---

## Milestone 10: Versioning Strategy

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

- [ ] Define `GRAPH_SCHEMA_VERSION 1` compile-time constant (in `graph.h` or near
  `VIVID_CORE_VERSION` in `main.cpp`)
- [ ] Add `schema_version` (int) and `vivid_version` (string) to the graph JSON root; write them
  at save time, read at load time; hard-reject if `schema_version > GRAPH_SCHEMA_VERSION`; treat
  absent `schema_version` as `1` for backward compat
- [ ] Add optional `"pkg": {"name": "...", "version": "..."}` sub-object to each node entry;
  omit for core operators, WGSL filters, and operators with no package manifest; populate at
  save time by querying `OperatorRegistry::package_for_type` + `PackageManager::list()`
- [ ] Extend `NodeDef` in `graph.h` with `pkg_name` / `pkg_version` fields; extend `Graph` with
  `schema_version` / `vivid_version` fields
- [ ] Update demo graphs in `graphs/` to include root-level `schema_version` and `vivid_version`
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

- [ ] Define `GraphLoadDiagnostic` struct (node_id, pkg_name, saved_version, installed_version,
  `PackageUpdateClass` classification) in `graph.h` or a companion header
- [ ] After `Graph::load`, iterate nodes with non-empty `pkg_name`; cross-reference against
  `PackageManager::list()`; classify via `assess_update()`; emit `IncompatibleUpdate` as a UI
  toast/inspector warning, `CompatibleUpdate` as stderr-only
- [ ] Reuse `parse_semver_triplet` / `compare_semver` / `assess_update` from
  `package_manager.cpp:135-222` — no new SemVer parsing code
- [ ] Expose diagnostics via MCP / control-server `get_diagnostics` so LLM-assisted workflows
  can surface version warnings

### Theme Versioning

- [ ] Add `"vivid_version": "0.1.0"` to all 8 embedded theme JSON constants in `theme_loader.cpp`
- [ ] Parse `"vivid_version"` in `parse_theme_root`; store it in `UIStyle`; if major component
  differs from `VIVID_CORE_VERSION`, emit a single stderr warning
- [ ] Stamp `VIVID_CORE_VERSION` into `"vivid_version"` when `ensure_default_themes` writes
  theme files to disk

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

Replace the minimal "create operator" flow with a modal that surfaces all scaffolding options upfront. Depends on M9 (output port API shape settled) and M8 (domain label finalized).

- [ ] Design full-options modal: domain selection, port types + count, param scaffolding (name/type/default), destination (project package vs. core)
- [ ] Include "create empty" path for advanced users who want a blank slate without the wizard
- [ ] Decide whether the modal is the canonical entry point for both GUI-triggered and MCP-triggered scaffold, or whether the two paths diverge intentionally

---

## Milestone 12: Solo Mode

"Solo" in a multi-domain graph has non-obvious cross-domain interactions. Design must be decided before implementation.

- [ ] Define GPU solo semantics: bypass upstream compositing and render only that operator to preview? What happens to downstream operators?
- [ ] Define audio solo semantics: mute all other audio outputs, or route only that operator to the audio device?
- [ ] Define cross-domain behavior: if a GPU operator is soloed but depends on an audio operator for modulation, does the audio chain still run?
- [ ] Decide scope: session-only UI affordance vs. graph-level concept (serialized with the graph)
- [ ] Implement

---

## Milestone 13: Semantic Parameter Tags Rollout

Implementation largely complete; remaining work is adoption breadth and CI gate.

- [ ] Finish broader seed-tag adoption (core + sibling operators not fully tagged)
- [ ] Close final CI rollout gate: no graph/load/runtime regressions

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
