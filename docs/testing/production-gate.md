# Production gate

The production gate is the single pass/fail verdict on a build's health: it runs a
label-filtered slice of the test suite, then judges the result against declared budgets.
Green ctest means "tests passed"; a green gate means "tests passed *and* the run was
complete and within budget" — the difference that makes it safe to block CI on.

## Profiles (cumulative)

| Profile | ctest label(s) | What it covers | Where it runs |
|---------|----------------|----------------|---------------|
| `core`  | `HEADLESS_SMOKE` | The headless suite — pure logic, operator loader/registry, persistence, hot-reload. No window/GPU/audio. | locally + every CI run |
| `gui`   | `+ GUI_SMOKE` | Windowed editor flows (none yet). | a macOS runner (future) |
| `env`   | `+ GUI_ENV` | External-package integration (none yet). | a macOS runner (future) |
| `soak`  | `+ SOAK` | Long-running stability (none yet). | nightly (future) |

Only `core` has tests today; the other tiers are wired and will light up as those tests
land. Each tier includes the ones above it, so running `gui` today still runs `core`.

## Run it

```sh
cmake --build app/build --target production_gate_core      # via CMake
scripts/run_production_gate.sh core                         # or directly
```

The script (`scripts/run_production_gate.sh`) self-tests the report tool, runs
`ctest -L <label> --output-junit …`, then `tools/production_gate_report.py --strict`,
which writes `app/build/reports/production-gate.json` and exits non-zero unless the
status is `pass`.

## Report shape

```json
{
  "schema_version": 1,
  "profile": "core",
  "tests": { "run": 12, "passed": 12, "failed": 0, "duration_seconds": 1.4 },
  "signals": { "budget_breaches": [] },
  "status": "pass"
}
```

`status` is `pass` | `degraded` (a warning budget breached) | `fail` (a test failed or an
error budget breached).

## Budgets

`tools/production_gate_budgets.toml` — `max_failures` (error), `min_tests` (error; guards
against a label that silently matches nothing), `max_duration_seconds` (warning). Keep it
small: every budget should be something we'd actually block or investigate on.

## Self-test

`uv run tools/production_gate_report.py --selftest` runs a synthetic JUnit doc through the
same parse + classify path. The gate script runs it first — the tool that judges the suite
is judged before it judges.
