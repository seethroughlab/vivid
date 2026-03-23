# Phase 6 — Export, Release Surfaces, And Final Readiness

## Scope Reviewed

Primary release-facing surfaces reviewed in this phase:

- standalone export pipeline and generated standalone build contract
- core app update surfaces and local appcast override behavior
- demo/example graph smoke coverage as a shipped release surface
- media-headless and capture release-path coverage
- release operations docs in `docs/release/`

This phase focused on whether Vivid's outward-facing release surfaces are trustworthy enough to finish the audit sequence, not on deeper runtime or UI implementation work already covered in earlier phases.

## Evidence Gathered

### Automated Phase 6 evidence bundle

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_export_pipeline|test_app_update_manager|test_demo_graphs|test_media_headless|test_capture_coordinator"
```

Observed result:

- 5 of 5 matched tests passed

Passing lanes:

- `test_export_pipeline`
- `test_app_update_manager`
- `test_demo_graphs`
- `test_media_headless`
- `test_capture_coordinator`

### Direct release-surface spot checks

Ran:

```bash
./build/test_demo_graphs ./build/graphs
VIVID_APPCAST_URL=file:///tmp/vivid_phase6_appcast.xml ./build/vivid check-core-updates --force
./build/vivid export --graph ./build/graphs/intro/audio_demo.json --output vivid_phase6_audio_demo --output-dir /tmp/vivid_phase6_export --headless
```

Observed results:

- direct demo smoke passed
  - `19 passed, 0 failed, 65 skipped`
- the CLI core-update path worked with a local appcast override and reported the mocked update correctly
- the export CLI generated export artifacts successfully, but the fresh standalone configure failed when `standalone.cmake.in` attempted to fetch `WebGPU-distribution` from GitHub in a network-restricted environment

### Focused source-backed review

Reviewed:

- `docs/release/RELEASE-CHECKLIST.md`
- `docs/release/README.md`
- `src/export/export_pipeline.cpp`
- `src/export/standalone.cmake.in`
- `tests/test_export_pipeline.cpp`
- `tests/test_app_update_manager.cpp`

Observed release-doc/runtime-contract note:

- the release checklist's local preflight section did not include export-pipeline, demo-smoke, or capture-coordinator coverage even though they are now part of the strongest release-surface evidence
- this checklist gap has been corrected in the same pass

## Findings

### 1. The core release-surface test bundle is healthy in the audited paths

- Severity: `note`
- Workstreams:
  - `export contract`
  - `demo/example surface`
  - `update surface`
  - `capture/media release paths`
- Evidence:
  - all 5 targeted Phase 6 tests passed
  - direct `test_demo_graphs` smoke also passed cleanly in the current environment
  - `test_media_headless` remains the explicit home for deferred movie/media demo coverage
  - `test_capture_coordinator` keeps the release recording path under contract-level coverage
- Current read:
  - no Phase 6 evidence indicates a release blocker in the currently tested export/update/demo/capture surfaces

### 2. The CLI core-update path works with the local appcast override flow used by release ops

- Severity: `note`
- Workstreams:
  - `core app updates`
  - `release checklist sanity`
- Evidence:
  - `./build/vivid check-core-updates --force` succeeded with `VIVID_APPCAST_URL=file:///tmp/vivid_phase6_appcast.xml`
  - the command reported the mocked update version, title, download URL, and release-notes link correctly
  - this matches the operational release checklist's local appcast sanity flow
- Current read:
  - the update surface is not just unit-tested; the CLI path also behaves correctly in a local no-network sanity setup

### 3. The release checklist previously under-specified the strongest local release preflight signals, and is now corrected

- Severity: `fixed`
- Workstreams:
  - `release docs`
- Evidence:
  - `docs/release/RELEASE-CHECKLIST.md` previously called out update/control-server preflight only
  - Phase 6 evidence shows export-pipeline, demo-smoke, media-headless, and capture-coordinator coverage are also part of the strongest release-surface signal set
  - the checklist now includes those local preflight expectations and commands
- Current read:
  - this was release-doc drift, not a code defect
  - it is now aligned with the actual audit evidence used for release confidence

### 4. Fresh standalone export is still not offline-hermetic

- Severity: `defer`
- Workstreams:
  - `export pipeline`
  - `release-surface environment assumptions`
- Evidence:
  - the export CLI resolved operators and generated export artifacts successfully for `audio_demo.json`
  - the generated standalone configure now prefers pre-fetched local source trees from the originating Vivid build for `WebGPU-distribution` and `IXWebSocket`
  - it still falls back to `FetchContent` when those local sources are unavailable
  - in this environment, that failed with DNS/network resolution errors against GitHub
- Current read:
  - this is still a real release-surface limitation, but the export contract is now more principled: reuse local/pre-fetched deps first, fetch only as fallback
  - the current docs and checklist should treat fully fresh standalone export as requiring either local/pre-populated dependency sources or network access unless a later pass makes export fully hermetic

### 5. Final publication checks remain operational, not repo-local, and were not reclassified by this phase

- Severity: `defer`
- Workstreams:
  - `release ops`
  - `notarization/pages/appcast publication`
- Evidence:
  - this phase validated repo-local code and docs surfaces only
  - GitHub secrets, Pages publication, notarization, and release workflow runs are tracked in `docs/release/RELEASE-CHECKLIST.md` but are not verifiable from this local environment audit
- Current read:
  - these are still required release operations tasks
  - they are not new code blockers or regressions established by this audit pass

## Required Fixes For Release

### Immediate release blockers

- None established by this phase.

### Required before release, but not currently classified as standalone blockers

- None added by Phase 6.
- The former Phase 4 inspector-readability item has now been completed in the post-audit follow-up pass.

## Deferred Follow-Ups

Still explicitly deferred after this phase:

- deeper GPU-capable automation for GPU-only demo verification
- future work to make standalone export more hermetic/offline-friendly if that becomes a release requirement or product goal
- operational release workflow validation from `docs/release/RELEASE-CHECKLIST.md`:
  - GitHub secrets
  - Pages/appcast publication
  - notarize/staple/verify
  - post-release Gatekeeper/update checks on a clean machine

## Signoff Status

- `pass with defer`

Reason:

- the targeted Phase 6 release-surface evidence bundle is green
- the local CLI update path behaves correctly with the documented appcast override flow
- release docs are now better aligned with the actual preflight evidence
- the remaining issues are environment/operations limitations rather than new Phase 6 code blockers
