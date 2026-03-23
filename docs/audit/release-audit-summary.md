# Release Audit Summary

## Status

- `Current phase`: Phase 6 completed
- `Next phase`: release-candidate prep and operational release checklist
- `Overall release status`: audit sequence complete; no audit-established blockers or release-required code fixes remain open

## Open Blockers

- None currently established by completed audit phases.

## Required Before Release

- None currently established by completed audit phases.

## Fixed Issues

### 1. `test_demo_graphs` is trustworthy again under `ctest`

- Fixed in: `Phase 1`
- Current evidence:
  - `ctest --test-dir build --output-on-failure -R "test_demo_graphs"` passes
  - direct `./build/test_demo_graphs ./build/graphs` also passes
  - the harness now provides per-graph lifecycle checkpoints and narrow repro filtering
  - plugin/preset discovery no longer depends on caller cwd

### 2. Built-in `modulated_gain.dylib` probe failure is resolved

- Fixed in: `Phase 1`
- Current evidence:
  - `./build/vivid list-packages` probes `ModulatedGain` cleanly
  - direct demo smoke also probes `ModulatedGain` cleanly
  - no unresolved `Smooth` vtable/probe error appears in the current registry scan paths

### 3. Control-server mutation parity now includes role bindings

- Fixed in: `Phase 2`
- Current evidence:
  - `control_server.cpp` exposes `set_role_binding` and `clear_role_binding`
  - `test_control_server` covers remote bind/clear plus undo/redo
  - `inspect_graph` now exposes per-node role-binding state for readback

### 4. Graph snapshot contract now covers role-binding truth

- Fixed in: `Phase 2`
- Current evidence:
  - `test_graph_snapshot_contract` now covers:
    - `role_binding_snapshots`
    - `referenced_by`

### 5. Semantic-tag metadata now matches the accepted v1 taxonomy

- Fixed in: `Phase 3`
- Current evidence:
  - `test_semantic_tags` now passes cleanly
  - the invalid tag drift in:
    - `Quantizer`
    - `SampleHold`
    - `AudioAnalysis`
    - `FmSynth`
    - `MidiFilePlayer`
    - `Compressor`
    - `Limiter`
    - `StereoPanWidth`
    - `LutApply`
    - `MovieLoaded`
    has been normalized or removed

### 6. Operator-loader runtime docs now match the current ABI version

- Fixed in: `Phase 5`
- Current evidence:
  - `docs/runtime/operator_loader.md` now matches `VIVID_OPERATOR_ABI_VERSION 13`
  - the previous stale ABI value was documentation drift only, not a runtime defect

### 7. Release checklist local preflight now matches the strongest release-surface evidence

- Fixed in: `Phase 6`
- Current evidence:
  - `docs/release/RELEASE-CHECKLIST.md` now includes:
    - `test_export_pipeline`
    - `test_demo_graphs`
    - `test_media_headless`
    - `test_capture_coordinator`
  - the checklist also now calls out the current fresh-export dependency assumption for WebGPU setup

### 8. Inspector readability cleanup and manual visual signoff are complete

- Fixed in: `Phase 4`
- Current evidence:
  - the inspector planner now rejects broken compact pairings and collapses them to readable full rows
  - role bindings and `Referenced By` render as stacked cards instead of dense inline rows
  - role-binding headers no longer show runtime-scope wording like `Per-Voice` / `Shared` in the visual inspector surface
  - deterministic whole-window screenshots now exist for:
    - `graphs/gpu/instanced_shapes_demo.json` → `shapes`
    - `graphs/gpu/instanced_shapes_demo.json` → `scale_lfo`
    - `graphs/gpu/particle_envelope_demo.json` → `env`
  - the current signoff evidence came from the live running-instance workflow:
    - `ensure_runtime(...)`
    - `load_graph(...)`
    - `capture_image(mode="interface", ...)`
  - those screenshots show the previously open overlap issue resolved strongly enough to close the release item
  - the same screenshot pass also confirmed that removing the runtime-scope label made the role-binding UI clearer in visual contexts without changing any runtime behavior

### 9. `rich_text_demo.json` has been reclassified as a verification-gap issue, not a general GPU/runtime defect

- Fixed in: `deferred follow-up after Phase 6`
- Current evidence:
  - filtered `./build/test_demo_graphs ./build/graphs rich_text_demo` still skips on this machine because there is no usable headless GPU adapter
  - the windowed runtime path succeeds cleanly via:
    - `./build/vivid graphs/gpu/rich_text_demo.json --screenshot /tmp/rich_text_demo_windowed.png --screenshot-delay 20`
  - that run initialized the GPU context, loaded the graph, and wrote a screenshot without aborting
- Current read:
  - this no longer supports the earlier fear of a broad GPU/runtime crash
  - the remaining gap is automated GPU-capable verification coverage, not a currently active runtime blocker

### 10. `wavetable_dream_keys_demo.json` needs two separate readings: sequencer shader fix landed, but the optional live `cp1` sanity case exposed a missing-package runtime path

- Fixed in: `deferred follow-up after Phase 6`
- Current evidence:
  - the original crashing graph still includes `cp1: ChordProgression`, and the invalid WGSL uniform field name `meta` in the sequencer thumbnail family was fixed
  - that earlier issue was real and operator-local to `vivid-sequencers`
  - however, the later live MCP review did **not** produce a fully clean end-to-end result:
    - required inspector signoff cases passed:
      - `graphs/gpu/instanced_shapes_demo.json` → `shapes`
      - `graphs/gpu/instanced_shapes_demo.json` → `scale_lfo`
      - `graphs/gpu/particle_envelope_demo.json` → `env`
    - the optional `../vivid-wavetable/graphs/extended/wavetable_dream_keys_demo.json` → `cp1` sanity case crashed in a separate run where `WavetableSynth` was missing at load time
  - fresh classification work showed:
    - with `vivid-wavetable` missing, the graph degraded to a missing-operator placeholder path and could still reach a fatal main-thread GPU submit
    - with `vivid-wavetable` present and rebuilt, the same graph loaded and `capture_image(mode="interface", node_id="cp1", ...)` succeeded
  - the runtime now fail-closes custom thumbnail GPU work whenever any node is unresolved, and the thumbnail overlay pass only composites GPU nodes or explicitly active custom-thumbnail nodes
- Current read:
  - the old `ChordProgression` WGSL bug was fixed
  - the remaining follow-up was a separate unresolved-graph / missing-package stability issue, not evidence that `cp1` or `WavetableSynth` are still crashing in the package-present path

## Deferred Issues

### 1. Headless/demo coverage intentionally defers some graph classes

- Status: `known at audit start`
- Current deferred classes:
  - movie/media graphs defer to `test_media_headless`
  - external I/O graphs are skipped in the headless smoke lane
  - GPU-only demo graphs currently skip when the environment has no usable headless GPU adapter

### 2. GPU-only demo verification is still environment-limited in headless mode

- Status: `known at audit start`
- Context:
  - the current machine still reports no usable headless GPU adapter for filtered repro
  - `rich_text_demo.json` now passes the normal windowed screenshot path, so the remaining issue is verification coverage rather than a classified crash
- Planned follow-up:
  - add deeper GPU-capable automation alongside the existing windowed screenshot path

### 3. Fresh standalone export is not offline-hermetic

- Status: `known from Phase 6`
- Context:
  - `./build/vivid export --graph ./build/graphs/intro/audio_demo.json --output vivid_phase6_audio_demo --output-dir /tmp/vivid_phase6_export --headless` resolved operators and generated export artifacts successfully
  - the generated standalone configure now prefers pre-fetched local source trees from the originating Vivid build for `WebGPU-distribution` and `IXWebSocket`
  - it still falls back to `FetchContent` if those local trees are unavailable
- Current read:
  - this is a real export-surface limitation, but not currently classified as a release blocker
  - fully fresh standalone export should currently be treated as requiring network access or pre-populated dependency state

### 4. Final publication validation remains an operational release task

- Status: `known from Phase 6`
- Context:
  - GitHub secrets, Pages/appcast publication, notarization, staple/verify, and clean-machine Gatekeeper/update checks are tracked in `docs/release/RELEASE-CHECKLIST.md`
  - these were not verifiable from the local repo audit environment
- Current read:
  - these remain mandatory release operations tasks, but are not code regressions established by the audit

## Baseline Environment Notes

- The worktree was not clean at audit start.
- Current unstaged changes now include release-audit follow-up work in:
  - `CMakeLists.txt`
  - semantic-tag cleanup across affected operators
  - `operators/control/smooth/smooth_composable.cpp`
  - `operators/gpu/rich_text/rich_text.cpp`
  - `tests/test_demo_graphs.cpp`
  - `docs/audit/`
  - `docs/release/RELEASE-CHECKLIST.md`

This is not a finding by itself, but it matters when interpreting later regressions.

## Completed Phases

### Phase 0

- Document: `phase-0-baseline.md`
- Status: `pass with defer`

### Phase 1

- Document: `phase-1-runtime-core.md`
- Status: `pass with defer`

### Phase 2

- Document: `phase-2-graph-correctness.md`
- Status: `pass with defer`

### Phase 3

- Document: `phase-3-domain-pipelines.md`
- Status: `pass with defer`

### Phase 4

- Document: `phase-4-ui-and-interaction.md`
- Status: `pass`

### Phase 5

- Document: `phase-5-operator-package-ecosystem.md`
- Status: `pass`

### Phase 6

- Document: `phase-6-release-readiness.md`
- Status: `pass with defer`

## Next Workstreams

1. Add deeper GPU-capable automation on top of the current windowed screenshot repro path.
2. Continue the broader `NodeGraphUI` modularity cleanup while the inspector refactor is still fresh.
3. Tighten standalone export hermeticity beyond the new pre-fetched-source fallback if fully offline export matters.
4. Execute the operational release checklist when the codebase is actually ready to cut a release.

---

**Note (March 2026):** Role bindings were an intermediate design that has since been removed. The codebase now uses owned embedded composition for host-local modulation, ordinary ports for graph transport, and explicit outputs for cross-domain sharing. See `docs/EMBEDDED-OPERATOR-SLOTS.md` for the current architecture.
