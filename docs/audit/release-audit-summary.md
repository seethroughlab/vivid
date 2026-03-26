# Release Audit Summary

## Status

- `Current phase`: Phase 6 complete
- `Next phase`: Audit complete
- `Overall release status`: The active audit is complete. Phases 0-5 established a healthy baseline, runtime core, graph correctness, domain/media behavior, focused UI/editor behavior, and core operator/package contract surface. Phase 6 initially found a bundled `particles.dylib` probe failure and appcast-path doc drift, and the follow-up fix cleared both. The remaining work is now the expected release-prep set: clean tagging state, clean-machine/manual validation, GPU-available validation, and any intentional sibling-package follow-up.
- `Architecture under audit`: owned embedded composition for host-local behavior, ordinary ports for graph transport, explicit outputs for graph-visible sharing

## Open Blockers

- None established by the current automated audit state.

## Required Before Release

- Commit the intended release changes and return the repo to a deliberate tagging state with a clean `git status --short`.
- Decide whether sibling packages such as `vivid-wavetable` are part of the current release story; if they are, rebuild and revalidate them against the active core before publish.
- Execute the remaining release-machine/manual validation flows required by:
  - [RELEASE-CHECKLIST.md](/Users/jeff/Developer/vivid/docs/release/RELEASE-CHECKLIST.md)
  - [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md)

## Fixed Issues

- The initial Phase 0 screenshot-smoke failures were triaged and resolved on the current branch:
  - obsolete `scale_lfo` GUI-smoke expectations removed after the move to owned embedded composition
  - graph-drop reload expectations updated to match the current `instanced_shapes_demo.json` graph
  - GUI-env smoke now forwards package search paths so sibling package operators resolve during spawned sessions
- The Phase 6 bundled-operator and release-doc blockers were also resolved on the current branch:
  - `particles.dylib` now links cleanly against composable `Envelope` support and no longer fails registry probe during `check-core-updates --force`
  - release docs now reference `site/appcast.xml`, matching the publish workflow and repo layout

## Deferred Issues

- Direct demo smoke on this machine is partially constrained by environment:
  - `./build/test_demo_graphs ./build/graphs` reported `19` passes, `0` failures, and `65` skips because GPU-only graphs were skipped in a no-GPU headless lane.
- Direct standalone media smoke on this machine is also partially constrained by environment:
  - `./build/test_media_headless ./build/graphs` reported `0` passes, `0` failures, and `3` skips, then exited non-zero because the standalone harness requires at least one passing curated media graph.
  - the focused `ctest` lane for `test_media_headless` still passed, so this remains a GPU-available validation gap rather than a Phase 3 product failure.
- Sibling package compatibility still needs one release-facing follow-up pass:
  - `vivid-sequencers` smoke was clean and `vivid-drums` smoke was clean within expected no-GPU skips.
  - `vivid-wavetable` smoke reported `8` passes and `1` failure, with a local MIDI-initialization error and unresolved package operator loads against the current core build.
- Clean-machine publish validation is still outstanding:
  - notarized download / Gatekeeper acceptance
  - in-app update flow on a clean machine
  - fullscreen / external-display behavior
  - theme switching
  - audio / MIDI / device-dependent validation

## Completed Phases

### Phase 0

- Document: `phase-0-baseline.md`
- Status: `pass with defer`
- Evidence summary:
  - repo state at audit start: clean `master`
  - automated inventory: `86` discovered tests
  - non-screenshot baseline: `83/83` passed
  - focused release-facing lane: `7/7` passed
  - screenshot-smoke follow-up: `test_ui_screenshot_smoke`, `test_ui_screenshot_smoke_env`, and `test_ui_screenshot_smoke_harness` all passed after triage fixes

### Phase 1

- Document: `phase-1-runtime-core.md`
- Status: `pass`
- Evidence summary:
  - runtime-core focused bundle: `17/17` passed
  - stress-biased rerun: `4/4` passed
  - direct runtime logs showed successful graph load, control-server startup, file-watcher/hot-reload activation, and clean shutdown

### Phase 2

- Document: `phase-2-graph-correctness.md`
- Status: `pass`
- Evidence summary:
  - graph-correctness focused bundle: `15/15` passed
  - mutation/snapshot rerun: `6/6` passed
  - current docs, implementation notes, and tests aligned on transactional apply/reload, embedded-slot persistence, undo/redo snapshot restoration, and graph-truth-preserving snapshots

### Phase 3

- Document: `phase-3-domain-pipelines.md`
- Status: `pass with defer`
- Evidence summary:
  - domain/media focused bundle: `16/16` passed
  - timing/media-heavy rerun: `6/6` passed
  - direct demo smoke: `19` passed, `0` failed, `65` skipped in the no-GPU headless lane
  - current docs and focused tests aligned on control-at-the-center routing, control→audio snapshots, cross-domain spread behavior, media decode/sync behavior, and the movie out/export path

### Phase 4

- Document: `phase-4-ui-and-interaction.md`
- Status: `pass`
- Evidence summary:
  - focused UI/editor bundle: `10/10` passed
  - high-signal UI rerun: `7/7` passed
  - deterministic retained-mode editor/widget/overlay tests, GUI screenshot-smoke lanes, and runtime-facing capture/state seams all remained aligned with the current inspector-first UI model

### Phase 5

- Document: `phase-5-operator-package-ecosystem.md`
- Status: `pass with defer`
- Evidence summary:
  - focused operator/package bundle: `19/19` passed
  - lifecycle/authoring rerun: `7/7` passed
  - sibling package smoke: `vivid-sequencers` clean, `vivid-drums` clean within no-GPU skips, `vivid-wavetable` requires follow-up for package compatibility and MIDI-environment coverage

### Phase 6

- Document: `phase-6-release-readiness.md`
- Status: `pass with defer`
- Evidence summary:
  - release-facing focused bundle: `10/10` passed
  - high-signal release rerun: `5/5` passed
  - local appcast generation + `check-core-updates --force` passed and reported `Up to date (0.1.0)`
  - required GitHub workflows and release secrets were present
  - live appcast URL returned `HTTP/2 200`
  - follow-up verification cleared the earlier Phase 6 blockers:
    - `particles.dylib` now probes successfully with no unresolved `Envelope` symbols
    - release docs now align with `site/appcast.xml`
