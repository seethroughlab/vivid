# Plan: Production Gate — Phase 1 (CMake gate targets)

## Context

Phase 1 of `docs/plans/production-gate-and-health-plan.md` introduces the `production_gate*` CMake targets so a developer can answer "is this build safe to ship?" with one command. No new tests in this phase — only orchestration over what already exists, plus the JUnit XML output that Phase 2's report tool will consume.

This plan covers Phase 1 only. Working in worktree branch `worktree-production-gate-phase1` (path: `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-phase1`).

## Decisions locked in (from clarifying Q&A)

- `production_gate_core` includes **HEADLESS_SMOKE + UI_SMOKE + PACKAGE** — UI_SMOKE is deterministic (no real window) and smoke.yml already runs it.
- `test_movie_seek_stress` gets a `HEADLESS_SMOKE` label so the label-based filter picks it up.
- `validate_semantic_tags.sh` runs as a pre-step inside the gate target so the gate fails fast on tag violations and devs run the same check locally.
- Phase 1 target produces JUnit XML and exits with ctest's exit code. The report-tool invocation lands in Phase 2 — Phase 1 is shippable on its own.
- Profiles are **cumulative**: `gui` runs core + GUI_SMOKE, `env` runs gui + GUI_ENV, `soak` runs core + phase6_stress + phase6_soak.

## Scope

In scope:
- New `cmake/tests/90-production-gate.cmake` defining five custom targets.
- One-line label addition to `test_movie_seek_stress`.
- Wire-up include in `cmake/tests.cmake`.
- Update `.github/workflows/smoke.yml` and `.github/workflows/gui-env.yml` to call the gate targets.
- Documentation note in `cmake/CLAUDE.md` (test partition table) referencing the new partition.

Out of scope (later phases):
- The Python report tool (`tools/production_gate_report.py`) — Phase 2.
- `RuntimeHealthSnapshot` and any health JSON dumps — Phase 3+.
- Health budgets file — Phase 5.
- Touching what the underlying tests do.

## Files affected

New:
- `cmake/tests/90-production-gate.cmake` (~80 lines)

Modified:
- `cmake/tests.cmake` — add `include(cmake/tests/90-production-gate.cmake)` after line 176.
- `cmake/tests/40-packages-media-misc.cmake` — add `set_tests_properties(test_movie_seek_stress PROPERTIES LABELS "HEADLESS_SMOKE" TIMEOUT 120)` after line 385.
- `cmake/CLAUDE.md` — one row in the test partition table.
- `.github/workflows/smoke.yml` — replace bespoke ctest steps with `cmake --build build --target production_gate_gui`. Keep the validate-semantic-tags step removed (now a gate pre-step) and keep the artifacts upload step.
- `.github/workflows/gui-env.yml` — replace the `ctest -L '^GUI_ENV$'` step with `cmake --build build --target production_gate_env` only if the user wants daily CI to also re-run core+gui (see open question below).

## Step-by-step

### Step 1 — Label `test_movie_seek_stress`

Add one line after `cmake/tests/40-packages-media-misc.cmake:385`:

```cmake
set_tests_properties(test_movie_seek_stress PROPERTIES LABELS "HEADLESS_SMOKE" TIMEOUT 120)
```

Verify: `ctest --test-dir build -L '^HEADLESS_SMOKE$' -N` lists `test_movie_seek_stress`.

### Step 2 — New module `cmake/tests/90-production-gate.cmake`

Pattern modeled on the `phase6_stress` definition in `30-ops-stability-domains.cmake:426-431`. Use `${CMAKE_CTEST_COMMAND}` (not raw `ctest`) and `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}`. Each target writes a per-profile JUnit file under `${CMAKE_BINARY_DIR}/reports/`.

Sketch (final wording during implementation):

```cmake
# cmake/tests/90-production-gate.cmake
#
# Production-readiness gate. Wraps the release-critical CTest labels into
# tiered targets. Each target emits a JUnit XML report under
# ${CMAKE_BINARY_DIR}/reports/ that Phase 2 will consume.

set(_pg_reports_dir ${CMAKE_BINARY_DIR}/reports)
file(MAKE_DIRECTORY ${_pg_reports_dir})

# Pre-step: semantic tag validator. Fails fast before any test runs.
add_custom_target(production_gate_pretest
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate] validating semantic tags"
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate_semantic_tags.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# --- core: deterministic, headless + UI_SMOKE + PACKAGE ---
add_custom_target(production_gate_core
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_core] starting"
    COMMAND ${CMAKE_CTEST_COMMAND}
        -L "^(HEADLESS_SMOKE|UI_SMOKE|PACKAGE)$"
        --output-on-failure
        --output-junit ${_pg_reports_dir}/ctest-core.xml
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS
        production_gate_pretest
        # HEADLESS_SMOKE
        test_demo_graphs test_operator_sweep test_movie_seek_stress
        # UI_SMOKE
        test_ui_overlay_interactions test_ui_editor_interactions
        test_ui_widget_interactions test_ui_screenshot_smoke
        # PACKAGE
        test_package_compiler test_package_catalog test_package_manager
        test_runtime_bootstrap_packages test_package_scope_resolver
        test_package_scope_registry test_package_scaffolder
        test_package_update_logic test_app_update_manager
        test_package_test_runner test_package_contract_ecosystem
)

# --- gui: core + GUI_SMOKE (windowed, no external packages) ---
add_custom_target(production_gate_gui
    COMMAND ${CMAKE_COMMAND} -E env VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1
        ${CMAKE_CTEST_COMMAND}
            -L "^GUI_SMOKE$"
            --output-on-failure
            --output-junit ${_pg_reports_dir}/ctest-gui.xml
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS production_gate_core test_ui_screenshot_smoke
)

# --- env: gui + GUI_ENV (requires external packages and HOME setup) ---
add_custom_target(production_gate_env
    COMMAND ${CMAKE_CTEST_COMMAND}
        -L "^GUI_ENV$"
        --output-on-failure
        --output-junit ${_pg_reports_dir}/ctest-env.xml
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS production_gate_gui test_ui_screenshot_smoke
)

# --- soak: core + phase6_stress + phase6_soak ---
add_custom_target(production_gate_soak
    COMMAND ${CMAKE_CTEST_COMMAND}
        -R "test_runtime_stress|test_hot_reload_stress|test_package_stress|test_mixed_runtime_stability"
        --output-on-failure
        --output-junit ${_pg_reports_dir}/ctest-soak.xml
    COMMAND $<TARGET_FILE:test_mixed_runtime_stability> ${CMAKE_BINARY_DIR} soak
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS production_gate_core
        test_runtime_stress test_hot_reload_stress
        test_package_stress test_mixed_runtime_stability
)

# Default alias — dev-friendly tier.
add_custom_target(production_gate DEPENDS production_gate_core)
```

Notes on the sketch:
- `--output-junit` is supported (CTest 4.0.2 confirmed). One file per profile so Phase 2 can consume each independently.
- `production_gate_env` doesn't pre-set `VIVID_PACKAGE_PATHS` etc. — that's the caller's responsibility (CI or dev). Document this in the CI workflow change.
- `production_gate_soak` chains the existing `phase6_stress` discovery (-R pattern) with the soak invocation; can't simply `DEPENDS production_gate_core phase6_stress phase6_soak` because those are sibling custom targets, not test runs, and chaining custom targets via DEPENDS doesn't preserve ordering of their COMMAND output. Inlining keeps a single linear log.
- The `production_gate_pretest` target prints a heading then runs the validator. Failing here stops the build.

### Step 3 — Wire into the build

Edit `cmake/tests.cmake:177` (after the existing five `include(...)` lines):

```cmake
include(cmake/tests/90-production-gate.cmake)
```

### Step 4 — Update `cmake/CLAUDE.md`

Add one row to the "Key Files" partition table:

```
| `tests/90-production-gate.cmake` | Tiered `production_gate*` targets (core/gui/env/soak) wrapping the release-critical labels |
```

### Step 5 — Update `.github/workflows/smoke.yml`

Replace the four bespoke steps (validate semantic tags, HEADLESS_SMOKE, UI_SMOKE, GUI_SMOKE) with one gate invocation. The build step still pre-builds because the gate's `DEPENDS` list will trigger building, but doing it as an explicit step keeps the parallel `-j` flag and gives a clear log section.

```yaml
- name: Run production gate (gui profile)
  env:
    VIVID_ENABLE_UI_SCREENSHOT_SMOKE: "1"
    VIVID_UI_SMOKE_LANE: "gui_smoke"
    HOME: ${{ env.VIVID_GUI_SMOKE_HOME }}
  run: cmake --build build --target production_gate_gui
```

The artifact-upload step stays as-is. The build-targets step can keep its explicit list (small redundancy, fast incremental, makes log clearer).

### Step 6 — Update `.github/workflows/gui-env.yml`

Open question: do we want daily gui-env CI to also re-run core + gui (longer run, more coverage), or stay GUI_ENV-only?

Recommended approach: invoke `production_gate_env` (cumulative), accept the longer daily runtime. If runtime becomes a problem, we add a `production_gate_env_only` target later. This keeps the gate semantics clean: "env profile means everything below env passed too."

```yaml
- name: Run production gate (env profile)
  env:
    VIVID_ENABLE_UI_SCREENSHOT_SMOKE: "1"
    VIVID_ENABLE_GUI_ENV_SMOKE: "1"
    VIVID_UI_SMOKE_LANE: "gui_env"
    VIVID_GUI_ENV_PACKAGE_ROOT: "${{ env.VIVID_GUI_ENV_PACKAGE_ROOT }}/vivid-wavetable"
    VIVID_PACKAGE_PATHS: ${{ env.VIVID_GUI_ENV_PACKAGE_ROOT }}
    HOME: ${{ env.VIVID_GUI_ENV_HOME }}
  run: cmake --build build --target production_gate_env
```

If the user prefers env-only CI, change the target to a lightweight `production_gate_env_only` that runs just `-L '^GUI_ENV$'` without core/gui — but that's an extra target to maintain. I'll flag this for confirmation when implementing.

## Verification

Local (in worktree):
```bash
# Configure if needed
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64

# Build + run the default (core) gate
cmake --build build --target production_gate

# Confirm JUnit lands
ls -la build/reports/ctest-core.xml

# Confirm underlying labels still work standalone
ctest --test-dir build -L '^HEADLESS_SMOKE$' --output-on-failure
ctest --test-dir build -L '^PACKAGE$' --output-on-failure
ctest --test-dir build -L '^UI_SMOKE$' --output-on-failure

# Confirm the soak profile composes
cmake --build build --target production_gate_soak
ls build/reports/ctest-soak.xml
```

CI:
- Smoke workflow turns green on the new invocation.
- gui-env workflow turns green on the new invocation (acknowledging the longer runtime).

Negative test:
- Temporarily break a HEADLESS_SMOKE test (e.g. force a non-zero exit in `test_demo_graphs`), confirm `production_gate_core` exits non-zero and the JUnit XML records the failure.
- Temporarily break a semantic tag (e.g. invalid value in an operator's `VIVID_REGISTER`), confirm the pretest step fails fast before any tests run.

## Risks and open items

1. **Cumulative env in CI**: Daily gui-env workflow runtime will grow because it now includes core + gui. Acceptable per recommendation, but flagging for confirmation.
2. **Explicit DEPENDS list**: 17+ test targets enumerated. If a new HEADLESS_SMOKE/UI_SMOKE/PACKAGE test is added later, the contributor must remember to add it to the DEPENDS list. Same maintenance cost as today's CI workflow. Future improvement: a `vivid_register_test()` macro that adds to a global list, but that's a separate refactor.
3. **No report tool yet**: Phase 1's gate target exits with the ctest exit code only — no `production-gate.json`. Phase 2 will add a post-step. Anyone reading the plan should know to expect this.
4. **`scripts/validate_semantic_tags.sh` runs in CMake source dir**: Already cwd-independent (uses absolute roots when called with no args). Fine.
5. **Worktree note**: Implementation happens in `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-phase1`. The original tree's uncommitted changes (safe_mode.h work, etc.) are not in the worktree — that's intentional, this branch should land independently.

## What I will do on approval

Apply Steps 1–6 in order, then run the verification commands above (with `run_in_background: true` per memory). Report back with the JUnit file location and any test failures encountered on the first run.
