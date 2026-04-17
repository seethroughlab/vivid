# Project Lockfile — Phase 7 Execution Plan

Scope of this doc: Phase 7 only ("Export Strict Mode") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phases 0–6.

## Context

Phases 0–6 make the lockfile reachable (HTTP / MCP / CLI), enforceable at graph load, and visible in the GUI. Phase 7 extends enforcement to the export pipeline so CI / production builds can refuse to export a graph whose lockfile doesn't match the current environment.

After this phase:

```bash
vivid export --strict --graph prod.json --output prod
```

returns non-zero (exit 2) when the sibling `vivid.lock` reports `Mismatch`, emitting a structured JSON payload on stdout and a human-readable summary on stderr. Non-strict exports are unchanged.

## Scope decisions (per user answers)

- **CLI + pipeline gate.** Both — `--strict` on the subcommand AND a verify call inside `ExportPipeline::run`.
- **Failure threshold:** only `Mismatch`. `CompatibleDrift` passes with a stderr note.
- **Lockfile discovery:** sibling `<graph_dir>/vivid.lock` by default; `--lockfile` overrides. Missing lockfile in strict mode is an error (exit 3) — strict without a lockfile is meaningless.
- **Failure output:** stdout JSON + stderr pretty-print + non-zero exit, matching `vivid verify-lock`'s convention (2 = Mismatch, 3 = I/O).

## Deliverable

- `ExportOptions.strict` (bool) + `ExportOptions.lockfile_path` (string).
- `ExportPipeline::run(opts, registry, pm = nullptr)` — optional trailing PackageManager pointer. When `opts.strict`, runs verify immediately after graph load, before `load_manifest` or `resolve_operators`.
- Three ExportPipeline getters for the CLI: `strict_verify_failed()`, `last_strict_verify_status()`, `last_strict_verify_error_kind()` (`"" | "mismatch" | "no_lockfile" | "no_pm" | "io_error"`).
- `vivid export --strict [--lockfile <path>]` CLI flags + conditional `scan_packages` + PackageManager construction + structured failure output.
- 6 pipeline-level tests + 3 CLI subprocess tests.
- Three commits on `worktree-project-lockfile`.

## Commit layout (as landed)

1. **`Add strict-mode gate to ExportPipeline for project lockfile`** — ExportOptions fields + `run(pm=nullptr)` signature + verify block moved to the very top of `run()` (before `load_manifest`) so strict failures short-circuit without any pipeline setup. Three getters for caller inspection. New `tests/packages/test_export_strict.cpp` with 6 scenarios.
2. **`Wire vivid export --strict CLI flag`** — `--strict` / `--lockfile` options, conditional `scan_packages=true`, PackageManager construction, JSON-to-stdout + pretty-to-stderr + 2/3 exit codes on strict failure. 3 subprocess-driven tests (fail paths only — a happy-path export would run the full build).
3. **`Add Phase 7 execution plan doc for project lockfile`** — this doc.

## Files

### Modified
- `src/export/export_pipeline.h` — `ExportOptions.strict`, `ExportOptions.lockfile_path`; `ExportPipeline::run` signature; `strict_verify_failed()` + `last_strict_verify_status()` + `last_strict_verify_error_kind()` getters; corresponding private members.
- `src/export/export_pipeline.cpp` — verify block at the top of `run()` after graph load; reset of last-status fields; early returns populating the getters.
- `src/runtime/core/main.cpp` — `--strict` / `--lockfile` flags; conditional registry bootstrap; PackageManager construction when strict; failure-branch JSON + stderr emission; exit-code mapping.
- `tests/packages/test_project_lockfile_cli.cpp` — 3 new subprocess tests for the CLI surface.
- `cmake/tests/40-packages-media-misc.cmake` — register `test_export_strict`.

### New
- `tests/packages/test_export_strict.cpp` — 6 pipeline-level tests. Target compiles `src/export/export_pipeline.cpp` directly (same pattern as `test_export_pipeline` in the 10-runtime partition) since `ExportPipeline` isn't part of `vivid_runtime_testlib`.

## Data model

```cpp
// src/export/export_pipeline.h
struct ExportOptions {
    // ...existing fields...
    bool strict = false;                 // enable lockfile verify gate
    std::string lockfile_path;           // explicit lockfile; empty = sibling
};

class ExportPipeline {
public:
    // `pm` is required when opts.strict is true; otherwise may be nullptr.
    bool run(const ExportOptions& opts, OperatorRegistry& registry,
             PackageManager* pm = nullptr);

    // Populated when run() returns false due to a strict-verify failure.
    bool strict_verify_failed() const;
    const LockfileStatus& last_strict_verify_status() const;
    const std::string& last_strict_verify_error_kind() const;
    // error_kind values: "", "mismatch", "no_lockfile", "no_pm", "io_error"
};
```

## Pipeline verify insertion

The master plan said "before `resolve_operators`." The shipped implementation goes further and puts verify at the **very top** of `run()` (immediately after graph load, before `load_manifest`). That's strictly better:

- Strict failures short-circuit without any manifest load or filesystem churn.
- The gate is testable without a fixture `operator_manifest.json` — the existing `tests/media/test_export_pipeline.cpp` has to set up a whole staging tree to exercise pipeline behavior, but the strict tests just need a graph file and a lockfile.
- `CompatibleDrift` still proceeds; `Mismatch` always aborts.

## CLI wiring

`src/runtime/core/main.cpp`:

```cpp
bool export_strict = false;
std::string export_lockfile_path;
export_cmd->add_flag("--strict", export_strict,
    "Require the sibling vivid.lock to match (exit 2=mismatch, 3=io)");
export_cmd->add_option("--lockfile", export_lockfile_path,
    "Explicit lockfile for --strict (default: vivid.lock next to graph)");

// In the export handler:
vivid::OperatorRegistry registry;
vivid::PackageCompiler compiler(runtime_paths.source_dir, runtime_paths.build_dir);
vivid::PackageManager pm(compiler, registry);
vivid::RegistryBootstrapOptions bootstrap_opts;
bootstrap_opts.scan_packages = export_strict;         // only when strict
vivid::bootstrap_operator_registry(registry,
                                    export_strict ? &pm : nullptr,
                                    runtime_paths, bootstrap_opts);

// ExportOptions fills — includes opts.strict / opts.lockfile_path.

const bool run_ok = pipeline.run(opts, registry,
                                  export_strict ? &pm : nullptr);
if (!run_ok) {
    if (pipeline.strict_verify_failed()) {
        // Stdout JSON: {"ok": false, "error": "...", "status": {...}}
        // Stderr: pretty findings table
        // Exit: 2 on "mismatch", 3 otherwise
    }
    // Non-strict failures keep the existing "Export failed" path (exit 1).
}
```

Non-strict exports are byte-for-byte identical to the pre-Phase-7 behavior.

## Failure-output shape

**stdout (on Mismatch):**
```json
{
  "ok": false,
  "error": "export blocked by --strict: mismatch",
  "status": {
    "overall": "mismatch",
    "findings": [
      {"id": "descriptor_hash_mismatch", "severity": "critical",
       "subject": "audio_out",
       "message": "operator descriptor changed since lockfile was written",
       "suggestion": "rebuild core"}
    ]
  }
}
```

**stdout (on no_lockfile / no_pm / io_error):**
```json
{"ok": false, "error": "export blocked by --strict: no_lockfile"}
```

**stderr** (all cases): a `CRIT/WARN/INFO  id  subject  message` line per finding, plus `-> suggestion` when non-empty. Mirrors `vivid verify-lock --pretty` format.

## Tests

### Pipeline-level (`tests/packages/test_export_strict.cpp`)

1. **no_pm** — `opts.strict = true`, pass `pm = nullptr`: `run()` returns false, `last_strict_verify_error_kind() == "no_pm"`.
2. **no_lockfile** — no sibling `vivid.lock` and no override: `run()` false, `kind == "no_lockfile"`.
3. **mismatch** — stale lockfile with descriptor_hash_mismatch on `audio_out`: `run()` false, `kind == "mismatch"`, `last_strict_verify_status().overall == Mismatch`, and the `kDescriptorHashMismatch` finding is present.
4. **explicit --lockfile** — sibling is clean, `--lockfile` points at a stale one: `run()` false with `"mismatch"`, proving the override wins over sibling discovery.
5. **matching lockfile gate clears** — empty graph + matching empty lockfile: `strict_verify_failed()` stays false and `last_strict_verify_error_kind()` is empty. Downstream pipeline stages still fail in this minimal fixture (no manifest), which is fine — the test only asserts the gate.
6. **non-strict ignores lockfile** — `opts.strict = false` with a stale lockfile: `strict_verify_failed()` stays false, `error_kind` empty. Confirms the opt-in is strict.

### CLI-level (`tests/packages/test_project_lockfile_cli.cpp`)

7. **`vivid export --strict` no sibling** — exit 3, JSON with `ok:false` and `error` mentioning `no_lockfile`.
8. **`vivid export --strict` mismatch** — stale sibling, exit 2, JSON has `status.overall == "mismatch"`, stderr contains `CRIT`.
9. **`vivid export --strict --lockfile <stale-path>`** — explicit override, exit 2, status inlined.

Happy-path CLI testing (actually running the export build) is intentionally absent — that's covered by the pipeline test at scenario (5). Running the full export build in a subprocess test would add minutes per test run for little additional coverage.

## Verification

```bash
cmake --build build --target vivid test_export_strict test_project_lockfile_cli
ctest --test-dir build --output-on-failure -R "export_strict|project_lockfile_cli"
```

Manual smoke:

```bash
./build/vivid lock --graph graphs/intro/audio_demo.json

# 1. Happy path: strict export matches the lockfile.
./build/vivid export --strict --graph graphs/intro/audio_demo.json --output demo_out
# expect exit 0

# 2. Edit the lockfile (e.g. flip one descriptor_hash character).
./build/vivid export --strict --graph graphs/intro/audio_demo.json --output demo_out
# expect exit 2 + JSON on stdout + pretty findings on stderr
```

## Acceptance Criteria

- `ExportOptions.strict` and `ExportOptions.lockfile_path` exist and are honored by `ExportPipeline::run`.
- Strict-mode `run()` returns false on `Mismatch`, missing PackageManager, or missing lockfile; `CompatibleDrift` falls through with a stderr note.
- `strict_verify_failed()` + `last_strict_verify_status()` + `last_strict_verify_error_kind()` expose enough detail for structured output.
- `vivid export --strict` exits 0 on match, 2 on Mismatch, 3 on I/O / config error. Stdout parses as JSON on failure; stderr carries the human summary.
- Non-strict `vivid export` behavior is unchanged (same scan_packages=false, same pipeline flow).
- `ctest -R "export_strict|project_lockfile_cli"` stays green.
- No changes outside the "Modified" / "New" lists.
- Three commits on `worktree-project-lockfile` beyond the Phase 6b commits.

## Out of Scope (Phase 7)

- GUI export dialog's own strict toggle — the CLI is the gate; no GUI wiring.
- `Settings.export_strict_default` — explicitly declined as premature.
- Post-export integrity checks (re-verify the bundled lockfile after build) — different feature.
- Phase 8 (asset content hashing).
- `abi_mismatch` with `subject == "vivid_core"` special-case — Phase 7 treats it as a regular Critical finding that triggers `Mismatch`.

Phase 7 closes the final production-readiness gap: CI can now gate on lockfile verification before shipping a standalone binary.
