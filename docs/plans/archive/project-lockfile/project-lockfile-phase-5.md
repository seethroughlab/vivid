# Project Lockfile — Phase 5 Execution Plan

Scope of this doc: Phase 5 only ("CLI Subcommands") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on the rest of the phases; see `project-lockfile-phase-{0,1,2,3,4}.md` for their individual execution records. This phase adds the command-line surface.

## Context

Phases 0–4 made the lockfile feature available via HTTP and MCP. Phase 5 adds the final surface a user reaches for in practice — the `vivid` binary itself, invoked from shell or CI pipelines. Two subcommands:

```bash
vivid lock        --graph path/to/graph.json [--output path/to/vivid.lock]
vivid verify-lock --graph path/to/graph.json [--lockfile path/to/vivid.lock] [--pretty]
```

After this phase lands, a CI job can gate on `vivid verify-lock --graph production.json` returning exit 0 (strict) or ≤1 (permissive), and a developer can regenerate a lock with one short command.

## Scope decisions (per user answers)

- **Both subcommands together** (`lock` + `verify-lock`). Natural pair; no reason to split.
- **Implementation:** call the lockfile free functions directly (`build_lockfile_for_graph`, `save_lockfile`, `load_lockfile`, `verify_lockfile`, `lockfile_status_to_json`). Skip the RuntimeAPI layer — the CLI doesn't need a live `Graph` / `RuntimeCore` / `AudioEngine`, so constructing those would be pure ceremony.
- **verify-lock output:** JSON to stdout by default (same shape as `get_project_dependency_status` HTTP response); `--pretty` switches to a human-readable findings list on stderr with stdout staying silent. Exit codes: `0` match, `1` compatible_drift, `2` mismatch, `3` I/O / lockfile-load error.
- **lock output:** defaults to sibling `vivid.lock`; `--output` overrides. Emits `{"ok": true, "message": <abs path>}` on success.

## Deliverable

- Two new CLI subcommands wired into `src/runtime/core/main.cpp`.
- A small early-exit handler block (between `export` and the generic CLI query block) that owns its own bootstrap with `scan_packages = true`.
- A pretty-output helper file-local to `main.cpp` — small enough not to warrant a separate TU.
- `tests/packages/test_project_lockfile_cli.cpp` — subprocess-driven integration test that shells out to the built `vivid` binary and inspects stdout / stderr / exit code.
- Three commits on the existing `worktree-project-lockfile` branch.

## Commit layout (as landed)

1. **`Add vivid lock CLI subcommand`** — subcommand declarations + shared bootstrap + both handler blocks (`lock` and `verify-lock`) landed together in the same main.cpp edit. The commit message names `lock` because that was the first piece designed; the implementation covers both.
2. **`Add vivid verify-lock CLI subcommand + CLI tests`** — new `test_project_lockfile_cli.cpp` + CMake registration. Covers the full CLI surface with subprocess tests.
3. **`Add Phase 5 execution plan doc for project lockfile`** — this doc.

A cleaner split would have been one commit per subcommand, but because both subcommands share declaration, bootstrap, and the shared `emit_json_line` lambda, separating them would have produced awkward mid-file diffs.

## Files

### Modified
- `src/runtime/core/main.cpp` — two new subcommand declarations, one new early-exit handler block, `#include "runtime/packages/project_lockfile.h"`.
- `cmake/tests/40-packages-media-misc.cmake` — register `test_project_lockfile_cli`.

### New
- `tests/packages/test_project_lockfile_cli.cpp` — subprocess-driven tests.

## CLI surface

### `vivid lock`

```
vivid lock --graph <FILE> [--output <FILE>]
```

Behavior:
- Loads the graph from `--graph` path.
- Bootstraps `OperatorRegistry` + `PackageManager` with `scan_packages = true`.
- Calls `build_lockfile_for_graph(graph, pm, registry)`.
- Sets `lf.graph.path = --graph` so the lockfile references the source graph.
- Saves to `--output` or the sibling `vivid.lock` next to the graph.
- Stdout: `{"ok": true, "message": <absolute-path>}` on success; `{"ok": false, "error": ...}` on failure.
- Exit: 0 on success, 1 on any failure.

### `vivid verify-lock`

```
vivid verify-lock --graph <FILE> [--lockfile <FILE>] [--pretty]
```

Behavior:
- Loads the graph and the lockfile (sibling `vivid.lock` if `--lockfile` omitted).
- Same bootstrap as `lock`.
- Calls `verify_lockfile(lockfile, graph, pm, registry)`.
- Stdout (default): `{"ok": true, "status": {"overall": ..., "findings": [...]}}`. Same schema as the `/get_project_dependency_status` HTTP response.
- Stdout (with `--pretty`): silent. Stderr carries a human-readable summary:

  ```
  [vivid verify-lock] MATCH: /path/to/vivid.lock
    (no findings)
  ```

  or

  ```
  [vivid verify-lock] MISMATCH: /path/to/vivid.lock
    CRIT missing_package                 vivid-wavetable  package not installed (locked 1.2.0)
           -> install vivid-wavetable@1.2.0
  ```

- Exit codes:
  - `0` — `overall = match`
  - `1` — `overall = compatible_drift`
  - `2` — `overall = mismatch`
  - `3` — I/O or lockfile-load error (graph missing, lockfile unreadable, etc.)

Pretty mode writes to stderr so scripts that pipe only stdout (for JSON) or only stderr (for logs) both work cleanly.

## Tests

`tests/packages/test_project_lockfile_cli.cpp` uses ProcessRunner via `/bin/sh -c '... >stdout 2>stderr'` to shell out with independent stream capture (ProcessRunner merges stdout/stderr by design, which is useful for interactive runs but not for assertions about "where did the text go").

Seven cases:

1. **lock happy path** — default sibling output, exit 0, JSON success, lockfile actually exists.
2. **lock --output override** — respects the override.
3. **lock missing graph** — exit 1, `{"ok": false, "error": ...}`.
4. **verify-lock round-trip** — write then verify. Exit 0 or 1 (environmental noise can push to compatible_drift); `status.overall` in `{"match", "compatible_drift"}`, never `"mismatch"`.
5. **verify-lock no sibling** — graph with no vivid.lock next to it, exit 3.
6. **verify-lock --lockfile missing** — explicit path that doesn't exist, exit 3.
7. **verify-lock --pretty** — stdout has no `status` JSON; stderr contains the header line.

Target registration:
```cmake
add_dependencies(test_project_lockfile_cli vivid)    # binary must exist
set_tests_properties(... PROPERTIES LABELS "PACKAGE" TIMEOUT 120)
```

Timeout is generous (120s) — the vivid binary's first-run bootstrap scans every installed package dylib (~100 operators + shader operators), which takes ~5–8 seconds on this machine. Seven subprocess invocations multiplied by that setup cost puts the test near 60s worst case.

## Verification

```bash
cmake --build build --target vivid test_project_lockfile_cli
ctest --test-dir build --output-on-failure -R project_lockfile_cli
```

Manual smoke:

```bash
./build/vivid lock --graph graphs/intro/audio_demo.json
./build/vivid verify-lock --graph graphs/intro/audio_demo.json --pretty
```

## Acceptance Criteria

- `vivid lock --graph <path>` writes `<dir>/vivid.lock` and exits 0 with a JSON success message.
- `vivid lock --output <path>` respects the override.
- `vivid verify-lock` exits 0/1/2/3 for match/drift/mismatch/error and emits the same JSON shape as `POST /get_project_dependency_status`.
- `--pretty` produces human-readable stderr output with stdout silent; exit codes unchanged.
- `ctest -R project_lockfile_cli` stays green.
- No changes outside the "Modified" / "New" file list.
- Three commits on `worktree-project-lockfile` beyond the Phase 0 commits.

## Out of Scope (Phase 5)

- UI indicator, load modes, `locked_unavailable` reason (Phase 6).
- `vivid export --strict` gate (Phase 7) — will wire strict-mode into the existing `export` subcommand, separate work.
- Asset hashing (Phase 8).
- `vivid_core.commit` build-time macro (deferred from Phase 0).
- Shell completion integrations.

Phase 5 is the last "how do I actually use this feature day-to-day" gap. After it lands, lockfile workflows are accessible through all four surfaces: HTTP (Phase 4), MCP (Phase 4), RuntimeAPI (Phase 2/3), and CLI (this phase).
