# Production-readiness gate.
#
# Wraps the release-critical CTest labels into tiered targets so a developer
# (or CI workflow) can answer "is this build safe to ship?" with one command.
# Each target writes a per-profile JUnit XML report under
# ${CMAKE_BINARY_DIR}/reports/ and runs tools/production_gate_report.py to
# emit ${CMAKE_BINARY_DIR}/reports/production-gate.json.
#
# Profiles are cumulative:
#   production_gate         -> alias for production_gate_core
#   production_gate_core    -> HEADLESS_SMOKE + UI_SMOKE + PACKAGE
#   production_gate_gui     -> core + GUI_SMOKE
#   production_gate_env     -> gui  + GUI_ENV
#   production_gate_soak    -> core + phase6 stress + soak
#
# The orchestration (ctest invocation, label filter, cumulative JUnit list,
# report-tool invocation, exit-code combination) lives in
# scripts/run_production_gate_profile.sh — extracted from inline bash to keep
# CMake and shell escaping cleanly separated.
#
# See docs/plans/production-gate-and-health-plan.md for the full design and
# docs/plans/production-gate-phase{1..5}.md + production-gate-followups.md for
# per-phase plans.

set(_pg_reports_dir ${CMAKE_BINARY_DIR}/reports)
file(MAKE_DIRECTORY ${_pg_reports_dir})

set(_pg_runner ${CMAKE_SOURCE_DIR}/scripts/run_production_gate_profile.sh)
set(_pg_budgets ${CMAKE_SOURCE_DIR}/tools/production_gate_budgets.toml)

# Pre-step: semantic-tag validator. Runs before any test so a tag violation
# fails fast without burning a full test cycle.
add_custom_target(production_gate_pretest
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate] validating semantic tags"
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/validate_semantic_tags.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM
)

# --- core: deterministic, headless + UI_SMOKE + PACKAGE -----------------------
add_custom_target(production_gate_core
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_core] running HEADLESS_SMOKE|UI_SMOKE|PACKAGE"
    COMMAND ${_pg_runner} core ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${_pg_budgets} ${CMAKE_BUILD_TYPE}
    VERBATIM
    DEPENDS
        production_gate_pretest
        # HEADLESS_SMOKE
        test_demo_graphs
        test_operator_sweep
        test_movie_seek_stress
        # UI_SMOKE
        test_ui_overlay_interactions
        test_ui_editor_interactions
        test_ui_widget_interactions
        test_ui_screenshot_smoke
        # PACKAGE
        test_package_compiler
        test_package_catalog
        test_package_manager
        test_runtime_bootstrap_packages
        test_package_scope_resolver
        test_package_scope_registry
        test_package_scaffolder
        test_package_update_logic
        test_app_update_manager
        test_package_test_runner
        test_package_contract_ecosystem
)

# --- gui: core + GUI_SMOKE ----------------------------------------------------
# Caller must have a window server available. VIVID_ENABLE_UI_SCREENSHOT_SMOKE
# is set as a per-test ENVIRONMENT property in 20-ui-and-common.cmake, so the
# target itself does not need to set it. Cumulative: report consumes both
# ctest-core.xml (from the production_gate_core dep) and ctest-gui.xml.
add_custom_target(production_gate_gui
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_gui] running GUI_SMOKE"
    COMMAND ${_pg_runner} gui ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${_pg_budgets} ${CMAKE_BUILD_TYPE}
    VERBATIM
    DEPENDS production_gate_core test_ui_screenshot_smoke
)

# --- env: gui + GUI_ENV -------------------------------------------------------
# Caller is responsible for staging external packages and setting
# VIVID_PACKAGE_PATHS / VIVID_ENABLE_GUI_ENV_SMOKE.
add_custom_target(production_gate_env
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_env] running GUI_ENV"
    COMMAND ${_pg_runner} env ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${_pg_budgets} ${CMAKE_BUILD_TYPE}
    VERBATIM
    DEPENDS production_gate_gui test_ui_screenshot_smoke
)

# --- soak: core + phase6 stress + soak ---------------------------------------
# The runner script handles the phase6 stress ctest invocation; the standalone
# soak invocation runs after as a separate COMMAND so its long runtime is
# visible in the gate's console output (not captured in JUnit).
add_custom_target(production_gate_soak
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_soak] running phase6 stress"
    COMMAND ${_pg_runner} soak ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${_pg_budgets} ${CMAKE_BUILD_TYPE}
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_soak] running soak lane"
    COMMAND $<TARGET_FILE:test_mixed_runtime_stability> ${CMAKE_BINARY_DIR} soak
    VERBATIM
    DEPENDS production_gate_core
        test_runtime_stress
        test_hot_reload_stress
        test_package_stress
        test_mixed_runtime_stability
)

# --- alias --------------------------------------------------------------------
add_custom_target(production_gate DEPENDS production_gate_core)

# --- self-test ----------------------------------------------------------------
# Runs the report tool's pytest suite. Labelled HEADLESS_SMOKE so it gates the
# core profile — a regression in the report tool's classification rules will
# fail the gate immediately rather than waiting for CI to notice.
# Requires `uv` on PATH (CI installs it; on macOS: `brew install uv`).
add_test(NAME test_production_gate_report
    COMMAND uv run --with pytest python -m pytest
            ${CMAKE_SOURCE_DIR}/tests/cli/test_production_gate_report.py -q
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(test_production_gate_report PROPERTIES
    LABELS "HEADLESS_SMOKE"
    TIMEOUT 60)
