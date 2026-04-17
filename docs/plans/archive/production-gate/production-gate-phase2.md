# Plan: Production Gate — Phase 2 (report generator)

## Context

Phase 1 left a `production_gate_core` CMake target that runs `ctest --output-junit build/reports/ctest-core.xml` and exits with ctest's exit code. Phase 2 adds the tool that consumes that JUnit XML and emits a stable, machine-readable `build/reports/production-gate.json` with classified failure buckets and a top-level `pass`/`degraded`/`fail` status — the artifact CI workflows, beta-readiness checklists, and humans actually read to decide "ship or not."

This plan covers Phase 2 only. Working in worktree branch `worktree-production-gate-and-health` at `/Users/jeff/Developer/vivid/.claude/worktrees/production-gate-and-health`. Phase 1's edits remain uncommitted in this worktree alongside Phase 2 — both phases will land together.

## Decisions locked in (with rationale)

- **Stdlib only for the report tool.** `xml.etree.ElementTree`, `argparse`, `json`, `subprocess`, `platform` — no external runtime deps.
- **`pytest` for tests, installed via `uv`.** Repo already uses `uv pip install --system` in `.github/workflows/llm-mcp-evals.yml:33`; consistent with the `uv` rule in memory. No `pyproject.toml` exists today; we don't need one for stdlib + pytest.
- **Gate target chains the tool via a `bash -c` wrapper** so the JSON is written even when ctest fails (CMake's COMMAND chain stops on first non-zero exit by default). Wrapper captures ctest's exit code, runs the tool unconditionally, then propagates ctest's exit code. macOS-only project so bash is fine.
- **Single output filename `build/reports/production-gate.json`, overwritten per gate run.** The `profile` field inside the JSON disambiguates. Cumulative profiles (gui = core + gui tests) consume multiple JUnit files in one tool invocation.
- **Self-test wired into CTest under HEADLESS_SMOKE.** A regression in the report tool would otherwise go unnoticed until CI changes shape. Adds `add_test(NAME test_production_gate_report COMMAND uv run --with pytest pytest ...)`.
- **JUnit `<system-out>` is truncated at 1KB by CTest.** Classification regexes target the front of the log; we accept that some failure types may fall back to `unknown` if their signature is past the truncation. Document this.

## Scope

In scope:
- New `tools/production_gate_report.py` (stdlib-only, ~250 lines).
- New `tests/cli/test_production_gate_report.py` plus `tests/cli/fixtures/junit/*.xml` golden files.
- New `tools/requirements-dev.txt` declaring `pytest` (one line).
- Wire pytest into CTest as a HEADLESS_SMOKE test in `cmake/tests/40-packages-media-misc.cmake` (or a new dedicated entry in `90-production-gate.cmake`).
- Update `cmake/tests/90-production-gate.cmake` so each `production_gate_{core,gui,env,soak}` target chains the report tool after ctest.
- One row in `cmake/CLAUDE.md` Key Files table for `tools/`.

Out of scope:
- `RuntimeHealthSnapshot` and `--health-json` budget evaluation (Phase 3 lands the snapshot, Phase 5 wires it through the report tool — Phase 2 just declares the flag and leaves `signals` as zeros / empty arrays).
- Trend storage, comparison-with-previous, or PR-comment posting.
- Failure reproduction commands or remediation hints.

## Files

New:
- `tools/production_gate_report.py`
- `tools/requirements-dev.txt` (one line: `pytest>=8`)
- `tests/cli/test_production_gate_report.py`
- `tests/cli/fixtures/junit/passing.xml`
- `tests/cli/fixtures/junit/webgpu_failure.xml`
- `tests/cli/fixtures/junit/audio_init_failure.xml`
- `tests/cli/fixtures/junit/graph_load_failure.xml`
- `tests/cli/fixtures/junit/missing_operator_failure.xml`
- `tests/cli/fixtures/junit/crash_failure.xml`
- `tests/cli/fixtures/junit/skipped_only.xml`

Modified:
- `cmake/tests/90-production-gate.cmake` — add bash-wrapped report-tool chain to each profile target; add `add_test(NAME test_production_gate_report ...)` with HEADLESS_SMOKE label.
- `cmake/CLAUDE.md` — one row in the Key Files table for `tools/`.

## Tool design

### CLI

```
python3 tools/production_gate_report.py \
    --profile {core,gui,env,soak} \
    --junit PATH [--junit PATH ...] \
    --output PATH \
    [--commit SHA] [--branch NAME] [--build-type TYPE] \
    [--git-meta-from-git] \
    [--health-json PATH]    # accepted but unused in Phase 2
```

- `--junit` is repeated for cumulative profiles (e.g. gui passes both `ctest-core.xml` and `ctest-gui.xml`).
- `--git-meta-from-git` runs `git rev-parse HEAD` and `git rev-parse --abbrev-ref HEAD`; explicit `--commit`/`--branch` win when both supplied.
- Tool always exits 0 on successful report generation; the gate's exit code reflects ctest, not the tool. (This keeps the tool composable — it's a transformer, not a gatekeeper.)

### Output schema

```json
{
  "schema_version": 1,
  "timestamp": "2026-04-16T13:44:32Z",
  "profile": "core",
  "git": {"commit": "db8868f6", "branch": "master"},
  "build": {"build_type": "RelWithDebInfo", "macos_version": "14.0",
            "hardware": {"machine": "arm64", "cpu": "Apple M2 Pro"}},
  "tests": {
    "run": 18, "passed": 18, "failed": 0, "skipped": 0,
    "duration_seconds": 134.66,
    "failures": [
      {"name": "test_demo_graphs", "classname": "test_demo_graphs",
       "duration_seconds": 99.4, "labels": ["HEADLESS_SMOKE"],
       "classification": "graph_load",
       "log_excerpt": "...first 256 chars of <system-out>..."}
    ],
    "skipped_reasons": {"requires_gui": 0, "requires_package": 0}
  },
  "signals": {
    "webgpu_validation_errors": 0,
    "audio_init_failures": 0,
    "graph_load_failures": 0,
    "missing_operators": []
  },
  "stress": {"phase6_stress_seconds": 0, "phase6_soak_seconds": 0},
  "status": "pass"
}
```

### Classification rules (first match wins)

Applied to each failed `<testcase>`. Patterns target the truncated `<system-out>` text; failure attribute messages are also concatenated for matching.

| `classification` | Pattern (case-insensitive) |
|---|---|
| `webgpu_error` | `validation error\|device lost\|wgpu\|webgpu` in log |
| `audio_init` | `miniaudio init\|audio device.*(fail\|error)\|AudioEngine.*failed` |
| `graph_load` | `testname == 'test_demo_graphs'` OR `failed to load graph\|FAIL: \S+\.json` |
| `missing_operator` | `(operator\|Registry).*(not_found\|not_built\|abi_mismatch)` |
| `crash` | `SIGSEGV\|SIGABRT\|SIGBUS\|signal \d+\|Assertion .* failed` |
| `timeout` | testcase has `status="notrun"` AND name appears in CTest's truncated reason as "Timeout" — we infer from `<failure type="Timeout">` if CTest emits it; otherwise fall back |
| `unknown` | fallback |

Each classification also increments the matching `signals.*` counter:
- `webgpu_error` → `signals.webgpu_validation_errors += 1`
- `audio_init` → `signals.audio_init_failures += 1`
- `graph_load` → `signals.graph_load_failures += 1`
- `missing_operator` → operator name appended to `signals.missing_operators`

### Status mapping

- Any `failed > 0` → `fail`.
- Otherwise — Phase 2 always emits `pass` because no signal sources beyond test failures exist yet.
- Phase 5 will add `degraded` when budgets are tripped without any test failing (the placeholder `--health-json` flag wires that in).

### Git-meta capture

```python
def auto_git_meta(repo_root: Path) -> dict:
    def run(*args):
        try:
            return subprocess.check_output(["git", *args], cwd=repo_root,
                                           stderr=subprocess.DEVNULL).decode().strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            return ""
    return {"commit": run("rev-parse", "HEAD"),
            "branch": run("rev-parse", "--abbrev-ref", "HEAD")}
```

Best-effort; CI overrides via flags. The `--branch` flag matters because GitHub Actions checks out a detached HEAD (`HEAD` shows the SHA, not the branch name).

### JUnit parser

`xml.etree.ElementTree.parse()` per file. Tolerate the `<testsuite>` wrapper variants CTest produces; we observed `<testsuite name="(empty)">` in the actual output. Walk every `<testcase>`; merge across multiple JUnit files for cumulative profiles.

Per-test extraction:
- `name`, `classname`, `time` from attributes.
- `labels` from `<properties><property name="cmake_labels" value="..."/></properties>` (split on `;`).
- `failed` if `<failure>` child present (or `status != "run"` and no `<skipped>`).
- `skipped` if `<skipped>` child present or `status == "notrun"`.
- `log` from `<system-out>` text (trimmed and capped at 1KB to keep the JSON small).

## CMake integration

### Chain the tool into each profile target

The current `production_gate_core` target ends with the ctest invocation. Replace with a bash-wrapped chain so the report runs even on ctest failure, but the gate's exit code still reflects ctest:

```cmake
add_custom_target(production_gate_core
    COMMAND ${CMAKE_COMMAND} -E echo "[production_gate_core] running HEADLESS_SMOKE|UI_SMOKE|PACKAGE"
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "${CMAKE_CTEST_COMMAND} -L '^(HEADLESS_SMOKE|UI_SMOKE|PACKAGE)$$' \
                 --output-on-failure \
                 --output-junit ${_pg_reports_dir}/ctest-core.xml; \
                 ec=$$?; \
                 python3 ${CMAKE_SOURCE_DIR}/tools/production_gate_report.py \
                     --profile core \
                     --junit ${_pg_reports_dir}/ctest-core.xml \
                     --output ${_pg_reports_dir}/production-gate.json \
                     --git-meta-from-git; \
                 exit $$ec"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    VERBATIM
    DEPENDS ...
)
```

Same shape for `_gui` (passes `--junit ctest-core.xml --junit ctest-gui.xml`), `_env` (three `--junit`), and `_soak` (core + soak).

### Self-test entry

```cmake
add_test(NAME test_production_gate_report
    COMMAND uv run --with pytest python -m pytest
            ${CMAKE_SOURCE_DIR}/tests/cli/test_production_gate_report.py -q
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(test_production_gate_report PROPERTIES
    LABELS "HEADLESS_SMOKE" TIMEOUT 60)
```

This puts the tool's tests on the gate's own critical path. CI already has `uv` available (per the `llm-mcp-evals` workflow).

## Test design

`tests/cli/test_production_gate_report.py` covers:

1. **Schema shape** — passing fixture produces JSON with all top-level keys present, `status: "pass"`.
2. **Each classification rule** — one fixture per rule, asserts the failure's `classification` equals the expected bucket and the matching `signals.*` counter is incremented.
3. **Cumulative profiles** — feed two fixtures, assert tests are merged and counts add up.
4. **Skipped-only run** — all tests skipped, `status: "pass"` and `tests.failed == 0`.
5. **Multiple failures of mixed kinds** — synth fixture with one webgpu + one missing_operator + one unknown; assert per-class counts.
6. **Git-meta override** — calling with `--commit X --branch Y` wins over `--git-meta-from-git`.
7. **Determinism** — same input + same overrides produces byte-identical output (sort_keys, no timestamp leaking unless set).

Tests use `tmp_path` for output paths. No network, no subprocess (except controlled `git` invocations stubbed via env or skipped when `--git-meta-from-git` not set).

## Verification

Local (in worktree):
```bash
# Generate a report from the existing JUnit and inspect it
python3 tools/production_gate_report.py \
    --profile core \
    --junit build/reports/ctest-core.xml \
    --output build/reports/production-gate.json \
    --git-meta-from-git
cat build/reports/production-gate.json | python3 -m json.tool | head -40

# Run the tool's own pytest suite
uv run --with pytest pytest tests/cli/test_production_gate_report.py -v

# Run the full gate; report tool runs as part of it
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
ls -la build/reports/production-gate.json   # should exist with status: pass
```

Negative test:
- Force a deliberate failure in `test_demo_graphs` (e.g. add an obviously-broken graph), rerun the gate, confirm `production-gate.json` reports `status: "fail"`, `tests.failed >= 1`, and the failure's classification is `graph_load`.

CI:
- `smoke.yml` already uploads `build/reports/ctest-*.xml` after the gate. Add `production-gate.json` to the same upload glob (or a separate upload).

## Risks and open items

1. **CTest's 1KB stdout truncation** caps classification fidelity. We mitigate with regex-on-front-of-log and document the limitation. If it bites in practice, Phase 2.5 could add a paired raw-log path (`Testing/Temporary/LastTest.log` is non-truncated and always written).
2. **`uv` availability**: the gate's self-test assumes `uv` is on PATH. CI has it; local devs may not. Document in `cmake/CLAUDE.md` "Production gate self-test requires `uv` (`brew install uv` on macOS)".
3. **Bash dependency in CMake target**: macOS-only project, low risk. Documented inline in 90-production-gate.cmake comment.
4. **Empty-suite case**: when ctest filters yield zero tests (e.g. label typo), CTest still writes a JUnit with `<testsuite tests="0">`. Tool handles this and emits `status: "pass"` with all-zero counts — flag this as a known footgun in the docs (a "passing" gate that ran nothing is still a problem, but that's a CMake-target-config bug, not a report-tool bug).
5. **Phase 5 schema additions**: when we add `--health-json` consumption, the schema gains evaluated budgets. Bumping `schema_version` from 1 to 2 at that time is fine; consumers should treat the version field as a contract.

## What I will do on approval

1. Create `tools/production_gate_report.py` with the design above.
2. Create the seven fixture XMLs (hand-crafted to mimic CTest's output).
3. Create `tests/cli/test_production_gate_report.py`.
4. Edit `cmake/tests/90-production-gate.cmake` to chain the tool and add the self-test entry.
5. Update `cmake/CLAUDE.md` Key Files table.
6. Run `uv run --with pytest pytest tests/cli/test_production_gate_report.py -v` (in background per memory).
7. Run `cmake --build build --target production_gate_core` to confirm the chained report tool produces `production-gate.json` with `status: "pass"`.
8. Report results.
