#!/usr/bin/env bash
# Run a production-gate profile: ctest with the right labels, then the
# report tool with --strict. Exits non-zero if either step fails.
#
# Usage:
#   run_production_gate_profile.sh PROFILE BUILD_DIR SOURCE_DIR BUDGETS_TOML BUILD_TYPE
#
# PROFILE is one of: core | gui | env | soak.
# Cumulative profiles consume earlier profiles' JUnit files; the caller
# (CMake) must have ensured those exist via target dependencies.
#
# Side effects:
#   - core: clears BUILD_DIR/reports/health/ before ctest so per-graph
#     health JSONs are fresh per gate run.
#   - All: writes BUILD_DIR/reports/ctest-<profile>.xml + production-gate.json.
#
# Exit code = max(ctest exit, report-tool exit). Non-zero from the report
# tool means status=fail (--strict path).

set -uo pipefail

if [ $# -lt 5 ]; then
    echo "usage: $0 PROFILE BUILD_DIR SOURCE_DIR BUDGETS_TOML BUILD_TYPE" >&2
    exit 64
fi

PROFILE="$1"
BUILD_DIR="$2"
SOURCE_DIR="$3"
BUDGETS_TOML="$4"
BUILD_TYPE="$5"

REPORTS_DIR="$BUILD_DIR/reports"
HEALTH_DIR="$REPORTS_DIR/health"
REPORT_JSON="$REPORTS_DIR/production-gate.json"
REPORT_TOOL="$SOURCE_DIR/tools/production_gate_report.py"
# CTest writes its non-truncated per-test log here. The report tool's --strict
# fallback classifier reads this when JUnit's <system-out> (1KB cap) doesn't
# carry enough text to bucket a failure into a known classification.
CTEST_LOG_DIR="$BUILD_DIR/Testing/Temporary"
mkdir -p "$REPORTS_DIR"

# Resolve label filter and cumulative JUnit list per profile.
case "$PROFILE" in
    core)
        LABELS='^(HEADLESS_SMOKE|UI_SMOKE|PACKAGE)$'
        JUNIT_ARGS=( --junit "$REPORTS_DIR/ctest-core.xml" )
        rm -rf "$HEALTH_DIR"
        mkdir -p "$HEALTH_DIR"
        ;;
    gui)
        LABELS='^GUI_SMOKE$'
        JUNIT_ARGS=( --junit "$REPORTS_DIR/ctest-core.xml"
                     --junit "$REPORTS_DIR/ctest-gui.xml" )
        ;;
    env)
        LABELS='^GUI_ENV$'
        JUNIT_ARGS=( --junit "$REPORTS_DIR/ctest-core.xml"
                     --junit "$REPORTS_DIR/ctest-gui.xml"
                     --junit "$REPORTS_DIR/ctest-env.xml" )
        ;;
    soak)
        # Soak uses -R name regex instead of -L label.
        LABELS=""
        SOAK_REGEX='test_runtime_stress|test_hot_reload_stress|test_package_stress|test_mixed_runtime_stability'
        JUNIT_ARGS=( --junit "$REPORTS_DIR/ctest-core.xml"
                     --junit "$REPORTS_DIR/ctest-soak.xml" )
        ;;
    *)
        echo "unknown profile: $PROFILE" >&2
        exit 64
        ;;
esac

cd "$BUILD_DIR"

if [ "$PROFILE" = "soak" ]; then
    ctest -R "$SOAK_REGEX" --output-on-failure --output-junit "$REPORTS_DIR/ctest-soak.xml"
else
    ctest -L "$LABELS" --output-on-failure --output-junit "$REPORTS_DIR/ctest-${PROFILE}.xml"
fi
ctest_ec=$?

python3 "$REPORT_TOOL" \
    --profile "$PROFILE" \
    "${JUNIT_ARGS[@]}" \
    --health-dir "$HEALTH_DIR" \
    --budgets "$BUDGETS_TOML" \
    --ctest-log-dir "$CTEST_LOG_DIR" \
    --output "$REPORT_JSON" \
    --repo-root "$SOURCE_DIR" \
    --git-meta-from-git \
    --build-type "$BUILD_TYPE" \
    --strict
tool_ec=$?

if [ "$tool_ec" -ne 0 ] && [ "$ctest_ec" -eq 0 ]; then
    exit "$tool_ec"
fi
exit "$ctest_ec"
