#!/usr/bin/env bash
# Production gate (P4.2): run a profile's label-filtered ctest, then judge the result
# against tools/production_gate_budgets.toml. Exit non-zero unless the gate is `pass`.
#
#   scripts/run_production_gate.sh [core|gui|env|soak]
#
# Profiles are CUMULATIVE label sets. Today only `core` (HEADLESS_SMOKE) has tests; the
# gui/env/soak tiers are wired and will pick up windowed/integration/soak tests as those
# land (each still includes core, so running them today runs the headless suite).
set -euo pipefail

PROFILE="${1:-core}"
REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BUILD_DIR="${VIVID_BUILD_DIR:-app/build}"
# Absolutize: `ctest --output-junit` writes RELATIVE TO --test-dir, not CWD, so a relative
# build dir would scatter the JUnit into a nested path. Resolve everything to absolute.
case "$BUILD_DIR" in /*) ;; *) BUILD_DIR="$REPO_ROOT/$BUILD_DIR" ;; esac
REPORTS_DIR="$BUILD_DIR/reports"
mkdir -p "$REPORTS_DIR"

case "$PROFILE" in
  core) LABEL="HEADLESS_SMOKE" ;;
  gui)  LABEL="GUI_SMOKE|HEADLESS_SMOKE" ;;
  env)  LABEL="GUI_ENV|GUI_SMOKE|HEADLESS_SMOKE" ;;
  soak) LABEL="SOAK|HEADLESS_SMOKE" ;;
  *) echo "unknown profile: $PROFILE (expected core|gui|env|soak)" >&2; exit 2 ;;
esac

JUNIT="$REPORTS_DIR/ctest-$PROFILE.xml"

# Judge the judge: the report tool self-tests before it parses the real suite.
uv run tools/production_gate_report.py --selftest

# Run the label-filtered suite. Do NOT abort on a test failure — the report tool decides
# the gate status (so a failure still produces a report + a clear FAIL, not a bare ctest
# exit code).
ctest --test-dir "$BUILD_DIR" -L "$LABEL" --output-junit "$JUNIT" --output-on-failure || true

uv run tools/production_gate_report.py \
  --junit "$JUNIT" --profile "$PROFILE" \
  --output "$REPORTS_DIR/production-gate.json" --strict
