# Project Lockfile — Phase 1 Execution Plan

Scope of this doc: Phase 1 only ("Lockfile model and parser") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Phase 0 prerequisites and later phases are intentionally out of scope.

## Context

The master plan calls for a `ProjectLockfile` value type plus a JSON load/save layer in `src/runtime/packages/`. Phase 1 delivers that module in isolation — no RuntimeAPI, no CLI, no UI, no dependency on Phase 0 provenance capture. The struct mirrors the full JSON schema so later phases can populate fields without reshaping the type. Load/save behavior is correct and diff-stable on day one.

This is the smallest reviewable unit that still lands real code. A reviewer can merge Phase 1 without inheriting commitments to later phases.

## Deliverable

- Pure data module with JSON round-trip, canonical key order, and structured errors.
- Unit tests registered in the existing packages test partition.
- Single commit on a feature branch produced in an isolated git worktree.

## Worktree Workflow

Work happens inside a git worktree so the master working tree stays clean and mid-flight changes on `master` (see `git status` at session start) are untouched.

1. **Enter worktree.** `EnterWorktree(name: "phase-1-lockfile-module")` — creates `.claude/worktrees/phase-1-lockfile-module` on a new branch based on `master` HEAD (`8031bb59`) and switches the session into it.
2. **Work inside the worktree.** All edits, builds, and test runs happen against the worktree path. The main tree's uncommitted changes are not visible here.
3. **Build and test.** Configure + build + run the new test target (see Verification below). Iterate until green.
4. **Commit.** Single commit on the worktree branch. Commit message follows existing style (imperative, short subject, Co-Authored-By footer).
5. **Exit worktree.** `ExitWorktree(action: "keep")` — returns the session to the primary working directory but preserves the worktree and branch so the user can open a PR against `master`.

If tests reveal the module needs reshaping, iterate inside the worktree; do not bounce out until Phase 1 is green.

## Files

### New

- `src/runtime/packages/project_lockfile.h` — struct definitions, error type, and free-function API.
- `src/runtime/packages/project_lockfile.cpp` — `nlohmann::json` load/save, canonical key order, version validation.
- `tests/packages/test_project_lockfile.cpp` — unit tests described below.

### Modified

- `cmake/tests/40-packages-media-misc.cmake` — register `test_project_lockfile` following the pattern in `cmake/tests/10-runtime-control-graph.cmake`.
- `src/runtime/packages/CMakeLists.txt` (if source file enumeration is explicit there) — add `project_lockfile.cpp`. Check before editing; some CMake roots glob.

No other files are touched in Phase 1.

## Data Model

The struct mirrors the full v1 JSON schema from the master plan. Fields not populated in Phase 1 (e.g. `descriptor_hash`, `content_hash`, `commit`) default to empty strings and are serialized as empty strings, not omitted — this keeps the JSON shape predictable. Omitted-vs-empty policy can be revisited in Phase 2 if noisy diffs appear.

```cpp
// project_lockfile.h — sketch; match existing codebase conventions for
// namespace, include order, and error handling.

namespace vivid::packages {

struct LockfileGraph {
    std::string path;
    int schema_version = 0;
    std::string content_hash;
};

struct LockfileCore {
    std::string version;
    std::string commit;
    int operator_abi = 0;
};

struct LockfilePackageSource {
    std::string kind;   // "git" | "local" | "registry"
    std::string url;
    std::string commit;
};

struct LockfilePackage {
    std::string name;
    std::string version;
    std::string vivid_core;
    LockfilePackageSource source;
    bool linked = false;
    std::string linked_path;
};

struct LockfileOperator {
    std::string type;
    std::string package;
    std::string package_version;
    std::string descriptor_hash;
    int operator_abi = 0;
};

struct LockfileAsset {
    std::string asset_id;
    std::string kind;
    std::string path;
    std::string content_hash;
};

struct ProjectLockfile {
    int lockfile_version = 1;
    std::string generated_at;      // RFC3339 UTC
    LockfileGraph graph;
    LockfileCore vivid_core;
    std::vector<LockfilePackage> packages;
    std::vector<LockfileOperator> operators;
    std::vector<LockfileAsset> assets;
};

struct LockfileError {
    enum class Kind {
        IoError,
        ParseError,
        UnsupportedVersion,
        InvalidShape,
    };
    Kind kind;
    std::string message;  // human-readable; safe to surface in UI/CLI
};

}  // namespace vivid::packages
```

Free functions:

```cpp
// Match the error pattern used by Graph::load_from_json_doc
// (src/runtime/graph/graph.h:357). If the codebase already has an
// expected/result helper, reuse it; otherwise return std::variant or a
// status+value pair. Do not introduce a new error-handling style in this
// module.

LoadResult<ProjectLockfile> load_lockfile(const std::filesystem::path& path);
SaveResult               save_lockfile(const std::filesystem::path& path,
                                       const ProjectLockfile& lockfile);
```

### Canonical key order

`save_lockfile` writes top-level keys in a fixed order:

```
lockfile_version, generated_at, graph, vivid_core, packages, operators, assets
```

Inside `packages[]` / `operators[]` / `assets[]`, fields follow the schema order shown in the master plan. `nlohmann::json::dump(2)` preserves insertion order when the underlying `json` object is built key-by-key — the implementation uses that, not an `ordered_json` typedef, to keep the dependency surface identical to the rest of the codebase.

### Version validation

`load_lockfile` reads `lockfile_version` before any other field. Accepted: `== 1`. Rejected: `<= 0` (InvalidShape), `> 1` (UnsupportedVersion). Missing `lockfile_version` is InvalidShape.

Absent optional fields are loaded as defaults without error — rationale from master plan: "Absent fields are treated as 'unknown,' not 'doesn't match.'"

## Tests

`tests/packages/test_project_lockfile.cpp` — drive via the same test harness used by other tests in `tests/packages/`. Inspect a sibling test (likely `test_package_manager.cpp` or similar) before choosing the harness flavor.

Test cases:

1. **Round-trip full fixture.** Build a `ProjectLockfile` with every field populated, save to a `std::filesystem::temp_directory_path()` tempfile, load, assert equality field-by-field.
2. **Round-trip minimal fixture.** `lockfile_version = 1` plus empty arrays only. Assert defaults come back correctly and arrays are preserved as empty.
3. **Canonical key order.** After save, re-read the raw text; verify top-level keys appear in the order listed above. Regex or string-index comparison is fine.
4. **Forward version rejection.** Hand-write `{"lockfile_version": 2}`, expect `UnsupportedVersion`.
5. **Malformed JSON.** Pass a file containing `not json`, expect `ParseError`.
6. **Missing `lockfile_version`.** Pass `{}`, expect `InvalidShape`.
7. **Missing optional fields.** Pass `{"lockfile_version": 1, "packages": []}`, expect a valid struct with `generated_at == ""` and empty `operators`/`assets`.
8. **Unknown top-level keys are ignored.** Pass a lockfile with an extra `"notes": "hi"` field; load succeeds and ignores the unknown key.

### CMake registration

Add to `cmake/tests/40-packages-media-misc.cmake`:

```cmake
add_executable(test_project_lockfile tests/packages/test_project_lockfile.cpp)
target_include_directories(test_project_lockfile PRIVATE src tests)
target_link_libraries(test_project_lockfile PRIVATE
    vivid_runtime_testlib vivid_operator_api nlohmann_json::nlohmann_json webgpu)
add_test(NAME test_project_lockfile COMMAND test_project_lockfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(test_project_lockfile PROPERTIES TIMEOUT 15)
```

Before copying verbatim, skim one existing entry in `40-packages-media-misc.cmake` — link targets may already cover some deps transitively and the flavor (plain vs. Catch2 vs. gtest) should match.

## Verification

Inside the worktree:

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Run in the background — per user preference, test invocations use `run_in_background: true`.

Additional sanity checks:

- `git status` inside the worktree should show only the four intended file changes (three new files + one CMake edit, plus any CMakeLists.txt listing change if required).
- `git log master..HEAD` shows one commit.
- `grep -rn project_lockfile src/runtime/packages tests/packages cmake/tests` returns only the new references.

## Commit

Single commit on the worktree branch. Suggested subject:

```
Add project lockfile model and JSON parser
```

Body: one paragraph on what's in (struct, load/save, version validation, canonical key order) and what's out (generation, verification, CLI, runtime wiring — deferred to later phases). Include the existing `Co-Authored-By` footer style.

## Acceptance Criteria

- New files exist at the paths listed above and compile cleanly.
- `ctest -R project_lockfile` passes all eight test cases.
- No files outside the listed set are modified.
- Worktree branch `phase-1-lockfile-module` contains one commit and is preserved after `ExitWorktree(keep)`.
- `lockfile_version` round-trips correctly; forward versions are rejected with a structured error.
- Canonical key order is observed across repeated saves (diff-stable).

## Out of Scope (for Phase 1)

- Git metadata capture on install/link (Phase 0).
- Operator descriptor hashing (Phase 0).
- `canonicalize_graph_hash` helper — deferred to Phase 2 where generation needs it.
- `build_lockfile_for_graph` generation entry point (Phase 2).
- `verify_lockfile` and `LockfileStatus` (Phase 3).
- RuntimeAPI methods, control-server dispatch, MCP tools (Phase 4).
- CLI subcommands (Phase 5).
- UI indicator, load modes, export strict mode (Phases 6–7).

The module lands as a pure data type; its first caller arrives in Phase 2.
