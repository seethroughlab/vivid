# Vivid Friend Beta Readiness Plan

## Summary

Prepare a high-confidence macOS beta for synth-savvy non-programmer beginners who may need CMake installed but should not need to understand the codebase. Treat `Vivid.app` plus first-run docs and examples as the primary product surface, with source/build steps only as clear setup prerequisites.

Use the existing repo gates as the foundation: `test_demo_graphs`, UI/GUI smoke tests, output analyzer/capture tooling, movie playback go/no-go, inner/outer loop tests, and stability stress tests. Add three human-centered review lanes: all sample graphs for audio/video appropriateness, every registered operator inspector for usability, and all beginner-facing docs from a non-programmer point of view.

Beta assumptions:

- Platform is macOS first.
- Primary flow is app/user-facing, not developer-facing.
- Setup docs may include Homebrew/CMake prerequisites where needed.
- High-confidence gate means blocking user-facing failures are fixed before sharing; only cosmetic polish and clearly documented limitations may remain.
- The target user is synth-savvy but not a programmer or creative technologist. Do not name private reference people in docs.

## Phase Index

- [Phase 1: Inventory and Gates](phase-1-inventory-and-gates.md)
- [Phase 2: Automated Release Baseline](phase-2-automated-release-baseline.md)
- [Phase 3: Sample Graph A/V Review](phase-3-sample-graph-av-review.md)
- [Phase 4: Operator Inspector Review](phase-4-operator-inspector-review.md)
- [Phase 5: Beginner Docs Review](phase-5-beginner-docs-review.md)
- [Phase 6: First-Run, Install, and Pilot](phase-6-first-run-install-pilot.md)

## Multi-Phase Plan

### 1. Define Beta Gates and Inventory

Create a beta readiness checklist with columns for area, owner, result, blocking status, evidence, and follow-up issue. Generate authoritative inventories from the current repo rather than hand-maintained lists:

- Curated sample graphs: `graphs/**/*.json`, grouped by intro, audio, GPU, filters, media, and live I/O.
- Reference and fixture graphs: `reference_graphs/**/*.json`, `tests/graphs/listening/**/*.json`, and `tests/graphs/parity/**/*.json`, reviewed separately from the beta onboarding surface.
- Operators: the runtime/operator registry surface, not raw directories, so dual-cadence wrappers and registered names match what users actually see.
- Environment-dependent cases: camera, mic, MIDI, OSC, Syphon, movie-file graphs, external display, and package-dependent examples.

### 2. Automated Release Baseline

Run the normal build and test baseline in Debug and RelWithDebInfo. Then run the release-oriented gates that already exist:

- `test_demo_graphs` across curated graphs, then separately across reference and fixture graphs.
- `UI_SMOKE`, `GUI_SMOKE`, and `GUI_ENV` where package/environment setup is available.
- Movie playback go/no-go from `docs/testing/MOVIE-PLAYBACK-GO-NO-GO.md`.
- Stability stress lanes from `docs/testing/STABILITY-STRESS-TESTS.md`, including the opt-in soak before release signoff.

Blocking failures include crash, hang, graph load failure, missing core operator, WebGPU validation error, audio device lockup, persistent silence in intended-audio graphs, black output in intended-visual graphs, scary clipping/feedback, or broken save/load.

### 3. All-Graph Audio/Video Review

For each graph, capture: graph path, expected purpose, required devices/assets, first-load result, audio result, visual result, A/V relationship, notable operator weirdness, and pass/fail.

Listen to every audio or A/V graph for at least 30-60 seconds, longer for sequenced/looping graphs. Flag clipping, runaway feedback, harsh highs, stuck notes, silence, unintended distortion, glitching, excessive volume jumps, bad loop seams, and anything likely to alarm a first-time user.

Watch every GPU/filter/video graph for black frames, frozen frames, unintentional flashing/strobing, unreadable text, broken media, bad aspect handling, obvious shader artifacts, or visuals that look accidental rather than exploratory.

For A/V graphs, verify the relationship is legible: audio visibly drives the intended visual behavior, sync is stable, and the graph does not feel like two unrelated demos running at once.

Use output analyzer metrics as triage evidence where useful, but make the final appropriateness call by human review.

### 4. Every Operator Inspector Review

For every registered operator, create or load a minimal graph that selects the node and exposes its inspector.

Review the inspector from the synth-savvy non-programmer beginner perspective: can the user tell what the operator does, which controls matter first, which values are safe, and what changes should be heard or seen?

Check interaction basics for each control type: slider, typed value, dropdown, toggle, color picker, XY pad, file picker, custom rich widget, MIDI mapping affordances, presets, and modulation controls where present.

Mark each operator as: `ready`, `minor copy/layout polish`, `confusing but usable`, or `blocking`.

Blocking inspector issues include clipped/overlapping controls, parameters with dangerous defaults, labels that require programmer knowledge for basic use, broken interaction, values that do not update runtime state, or custom inspectors that fail to fit or explain themselves.

### 5. Beginner Documentation Review

Audit only beginner-facing docs for this beta path: `README.md`, `docs/GETTING-STARTED.md`, `graphs/README.md`, `graphs/intro/README.md`, package install docs that appear in the beta flow, and any generated site pages a tester will see.

Add or revise first-run setup for macOS users who may need Homebrew/CMake, with a clear "you do not need to program" framing.

Rewrite confusing developer-first language in beginner docs into task-first language: open the app, open examples, hear/see what changed, tweak these controls, save a clip or scene, recover if something goes wrong.

Create a short "First 15 Minutes" path with a small curated example sequence and one fallback if audio, camera, or movie permissions are missing.

### 6. Install, Permissions, and Pilot

Test on a clean-ish macOS account or machine: clone/download, install prerequisites, configure/build if needed, launch Vivid, open the first example, and confirm audio and video work.

Validate first-run permission paths for microphone, camera, MIDI/IAC, Syphon if applicable, and file/media access.

Confirm errors are friendly when dependencies or devices are absent: no crash, no mystery silence, no permanent broken state.

Confirm the example browser metadata is useful: beginner examples are findable, package-required graphs are labeled, and environment-dependent graphs are not mistaken for starter content.

Run one internal full pass first; fix all blockers before outside sharing. Send a tiny pilot to one trusted tester only after the high-confidence gate passes internally. Watch for first-run friction: install confusion, audio device confusion, example browser confusion, unclear inspector labels, and panic-inducing audio/visual output.

After pilot fixes, send the beta to the wider friends/colleagues group with a concise known limitations note and a simple feedback form.

## Public Interfaces and Artifacts

No public runtime API or graph schema changes are planned for this readiness pass.

Add release artifacts rather than product features:

- Beta readiness checklist.
- All-graph A/V review matrix.
- Operator inspector review matrix.
- Beginner docs audit checklist.
- Known limitations note for beta testers.

If review finds beginner-blocking metadata gaps, update graph `meta` fields and user-facing docs only. Avoid changing graph behavior unless the graph is actually wrong or unsafe.

## Test Plan

- Automated: build, full test baseline, `test_demo_graphs`, `UI_SMOKE`, `GUI_SMOKE`, `GUI_ENV` where available, movie playback go/no-go, stability stress, soak.
- Manual: all-graph A/V review, every-operator inspector review, beginner docs walk-through, fresh setup path, permissions/device path, save/load/session smoke.
- Acceptance: zero crashes, zero scary-audio failures, zero unintended black/silent starter graphs, zero beginner-blocking docs gaps in the first-run path, and every blocking issue either fixed or removed from the beta surface.
