# Phase 6 — Export, Release Surfaces, And Final Readiness

## Scope Reviewed

- export pipeline
- app update path
- demo graphs and shipped examples
- release checklist alignment
- final blocker/defer decision

## Evidence Gathered

- Current repo/version state during Phase 6:
  - branch: `master`
  - worktree: dirty only from the active audit docs and the earlier screenshot-smoke follow-up docs/tests
  - version surfaces aligned:
    - `CMakeLists.txt`: `project(vivid VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)`
    - `src/runtime/main.cpp`: fallback `VIVID_CORE_VERSION "0.1.0"`
    - `src/ui/node_graph_draw.cpp`: fallback `VIVID_CORE_VERSION "0.1.0"`
- Release inventory:
  - `ctest --test-dir build -N | rg "test_(app_update_manager|package_update_logic|control_server|export_pipeline|demo_graphs|media_headless|capture_coordinator|ui_screenshot_smoke)"`
  - discovered `10` targeted release-facing lanes:
    - `test_export_pipeline`
    - `test_control_server`
    - `test_ui_screenshot_smoke`
    - `test_ui_screenshot_smoke_env`
    - `test_ui_screenshot_smoke_harness`
    - `test_demo_graphs`
    - `test_media_headless`
    - `test_package_update_logic`
    - `test_app_update_manager`
    - `test_capture_coordinator`
- Focused Phase 6 bundle:
  - `ctest --test-dir build -R "test_app_update_manager|test_package_update_logic|test_control_server|test_export_pipeline|test_demo_graphs|test_media_headless|test_capture_coordinator|test_ui_screenshot_smoke" --output-on-failure"`
  - result: `10/10` passed
- High-signal release rerun:
  - `ctest --test-dir build -R "test_app_update_manager|test_export_pipeline|test_demo_graphs|test_media_headless|test_capture_coordinator" --output-on-failure"`
  - result: `5/5` passed
- Local appcast/update sanity:
  - `python3 scripts/release/generate_appcast.py --version 0.1.0 --url https://example.com/Vivid-0.1.0.zip --length 123 --title Vivid --output /tmp/vivid_test_appcast.xml`
    - result: succeeded and wrote `/tmp/vivid_test_appcast.xml`
  - `VIVID_APPCAST_URL=file:///tmp/vivid_test_appcast.xml ./build/vivid check-core-updates --force`
    - result: `Core version: 0.1.0`
    - result: `Appcast: file:///tmp/vivid_test_appcast.xml`
    - result: `Up to date (0.1.0).`
  - initial follow-up investigation found a bundled operator probe failure:
    - `build/vivid.app/Contents/PlugIns/particles.dylib`
    - `symbol not found in flat namespace '__ZTV8Envelope'`
  - post-fix verification after adding composable support for `Envelope`:
    - `cmake --build build --target particles`
    - `cmake --build build --target vivid -j8`
    - `nm -u build/vivid.app/Contents/PlugIns/particles.dylib | rg 'Envelope|__ZTV8Envelope'`
      - result: no unresolved `Envelope` symbols remained
    - `VIVID_APPCAST_URL=file:///tmp/vivid_test_appcast.xml ./build/vivid check-core-updates --force`
      - result: registry now probes `Particles from particles.dylib` successfully
      - result: no `probe dlopen failed` line remained for `particles.dylib`
- Workflow/config readiness:
  - `ls -1 .github/workflows`
    - confirmed required workflows exist:
      - `release-macos.yml`
      - `release-macos-validate.yml`
      - `version-guard.yml`
      - `pages.yml`
  - confirmed required release files exist:
    - `scripts/release/generate_appcast.py`
    - `docs/release/RELEASE-CHECKLIST.md`
    - `docs/release/RELEASE-OPS.md`
  - `gh auth status`
    - result: logged in to GitHub with active account and `repo` / `workflow` scopes available
  - `gh secret list`
    - result: all required release secrets were present:
      - `APPLE_CERT_P12_B64`
      - `APPLE_CERT_PASSWORD`
      - `APPLE_CODESIGN_IDENTITY`
      - `APPLE_ID`
      - `APPLE_TEAM_ID`
      - `APPLE_APP_PASSWORD`
      - `VIVID_SPARKLE_PUBLIC_KEY`
  - live appcast URL check:
    - `curl -I -L --max-time 20 https://vivid.seethroughlab.com/appcast.xml`
    - result: `HTTP/2 200`
  - checked-in appcast path:
    - `site/appcast.xml` exists in the repo
    - release docs now align with the publish workflow and refer to `site/appcast.xml`
- Direct contract evidence from current docs and implementation:
  - [RELEASE-CHECKLIST.md](/Users/jeff/Developer/vivid/docs/release/RELEASE-CHECKLIST.md) defines repo-state, workflow, preflight, Pages/appcast, validation, publish, and post-release checks
  - [RELEASE-OPS.md](/Users/jeff/Developer/vivid/docs/release/RELEASE-OPS.md) defines the validate-vs-publish workflow split, appcast/update behavior, and version-guard expectations
  - [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md) defines the remaining manual-only release gates for audio/MIDI, GPU/display, save/load, capture, themes, fullscreen, and external displays
  - [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp) contains the `check-core-updates` CLI path and runtime update-check entry points
  - [node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp) contains the UI fallback `VIVID_CORE_VERSION` surface
- Historical boundary:
  - this phase uses current command evidence and current release/update/manual-validation contracts only
  - completed Phases 0-5 inform the release context, but they are not reused as proof for the Phase 6 release-surface verdict

## Findings

### 1. Release/version surfaces are aligned, but the repo is not yet tag-ready

- Classification: `required before release`
- Current read:
  - the CMake project version and both fallback `VIVID_CORE_VERSION` surfaces are all aligned at `0.1.0`
  - the audit ran on `master`
  - the worktree is still dirty from the active audit docs and earlier screenshot-smoke follow-up changes
- Why it matters:
  - the release workflows and checklist assume a deliberate, committed tagging point; the current workspace is a good audit workspace, but not yet a publish-ready repo state

### 2. The update/appcast path is healthy locally and reachable on the live site

- Classification: `pass`
- Current read:
  - `test_app_update_manager` passed in the focused bundle
  - local appcast generation succeeded
  - `./build/vivid check-core-updates --force` succeeded against the local file override and reported `Up to date (0.1.0).`
  - the live Pages-hosted appcast URL returned `HTTP/2 200`
  - required GitHub secrets were present and the required workflows exist in the repo
- Why it matters:
  - this is the core release-surface proof that update metadata generation, parsing, CLI update checks, workflow presence, and hosted feed reachability are all wired up

### 3. The shipped app bundle no longer reports the `particles.dylib` probe failure

- Classification: `pass`
- Current read:
  - Phase 6 initially caught a deterministic bundled `particles.dylib` load failure caused by unresolved `Envelope` symbols from embedded `ChildOp<Envelope>` usage
  - follow-up implementation added composable support for `Envelope`, linked `particles` against `vivid_composable_ops`, and rebuilt both the operator target and the app bundle
  - `nm -u build/vivid.app/Contents/PlugIns/particles.dylib` no longer shows unresolved `Envelope` destructor or vtable symbols
  - the local update sanity command now probes `Particles from particles.dylib` successfully
- Why it matters:
  - Phase 6 is judging the shipped surface, not just individual unit lanes; clearing this bundled operator probe failure removes the last deterministic code-level release blocker found by the audit

### 4. Release-facing automated preflight is healthy

- Classification: `pass`
- Current read:
  - the focused Phase 6 bundle passed `10/10`
  - the high-signal rerun passed `5/5`
  - this evidence covered:
    - `test_package_update_logic`
    - `test_control_server`
    - `test_export_pipeline`
    - `test_capture_coordinator`
    - `test_demo_graphs`
    - `test_media_headless`
    - `test_ui_screenshot_smoke`
    - `test_ui_screenshot_smoke_env`
    - `test_ui_screenshot_smoke_harness`
    - `test_app_update_manager`
- Why it matters:
  - these are the strongest automated proofs that the current release-facing surfaces still behave coherently under the active architecture

### 5. Workflow/config readiness is strong and the appcast docs now match the workflow

- Classification: `pass`
- Current read:
  - the required workflow files exist
  - `site/appcast.xml` exists in the repo
  - the publish workflow updates `site/appcast.xml`
  - the release docs now instruct readers to verify and update `site/appcast.xml`
- Why it matters:
  - Phase 6 is partly an operational audit; keeping the workflow and operator-facing release docs aligned removes a real source of release-process drift

### 6. Manual and environment-limited release validation is still outstanding

- Classification: `deferred`
- Current read:
  - [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md) still requires clean-machine/manual validation for:
    - fullscreen and external-display behavior
    - theme switching and custom theme load
    - audio output/device switching
    - MIDI learn/hot-plug and device recovery
    - Gatekeeper/notarized download behavior
    - in-app update behavior on a clean machine
  - earlier phases also left two release-relevant environmental follow-ups open:
    - GPU-available direct demo/media validation on a machine that can run non-skipped GPU/media graphs
    - sibling-package follow-up, especially `vivid-wavetable`, if those packages remain part of the intended release ecosystem
- Why it matters:
  - these are the remaining release-confidence steps that automation in this workspace does not fully close out on its own

### 7. Final release decision

- Classification: `pass with defer`
- Current read:
  - the Phase 6 automated preflight, update/appcast path, workflow presence, secrets, and live feed reachability are all healthy
  - the two deterministic release blockers established earlier in Phase 6 have now been cleared:
    - bundled `particles.dylib` probe failure
    - release-doc drift on `catalog/appcast.xml` vs `site/appcast.xml`
  - the remaining release work is now the expected final-release gating work:
    - intentional worktree cleanup/commit before tagging
    - remaining release-machine/manual validation
    - sibling-package follow-up if those packages remain part of the release story
- Why it matters:
  - Phase 6 is the release gate synthesis; the right conclusion after the fix is that the codebase is healthy at the automated release-surface level, with only final release-prep and environment/manual gates still open

## Required Fixes For Release

- Commit the intended release changes and return the repo to a deliberate tagging state with a clean `git status --short`.
- Decide whether sibling packages such as `vivid-wavetable` are part of the current release story; if they are, rebuild and revalidate them against the active core before publish.

## Deferred Follow-Ups

- Run the clean-machine release validation steps from [RELEASE-CHECKLIST.md](/Users/jeff/Developer/vivid/docs/release/RELEASE-CHECKLIST.md) and [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md):
  - notarized download and Gatekeeper acceptance
  - in-app `Check for Updates...`
  - fullscreen / external-display behavior
  - theme switching
  - audio / MIDI / device-dependent validation
- Re-run direct demo/media smoke on a GPU-available machine so the no-GPU skips from earlier phases are replaced with actual GPU-path evidence.
- Re-run any sibling-package graphs that depend on live MIDI/device initialization on a machine with that environment available.

## Signoff Status

- `pass with defer`
