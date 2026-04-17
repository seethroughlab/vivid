# Project Lockfile — Phase 2 Execution Plan

Scope of this doc: Phase 2 only ("Generation + RuntimeAPI") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phase 1 ([project-lockfile-phase-1.md](./project-lockfile-phase-1.md)). Phase 0 prerequisites (git metadata capture, operator descriptor hashing) remain deferred; later phases (control-server dispatch, CLI, UI, strict mode, asset hashes) are out of scope.

## Context

Phase 1 landed the `ProjectLockfile` data type and JSON load/save (commit `a26f7064` on `worktree-project-lockfile`). Nothing calls it yet. Phase 2 wires up the first producer:

1. A pure function `build_lockfile_for_graph` that walks a `Graph` and resolves each node's package/operator provenance against the live `PackageManager` and `OperatorRegistry`.
2. A helper `canonicalize_graph_hash` that returns `"sha256:<hex>"` for a graph, used to detect graph drift without rewriting the lockfile.
3. A `RuntimeAPI::write_project_lockfile(graph_path, output_path)` method that loads a graph, runs the generator, and writes `vivid.lock`.

Phase 0 fields — `package.source.url`, `package.source.commit`, `vivid_core.commit`, `operator.descriptor_hash` — remain empty strings. The lockfile produces correct **shape**; Phase 0 later fills in the **provenance**. The `assets[]` array is intentionally empty until Phase 8.

By the end of Phase 2, a developer can run the RuntimeAPI method from a test driver and get a stable, diff-friendly `vivid.lock` next to any graph.

## Deliverable

- `build_lockfile_for_graph(Graph, PackageManager, OperatorRegistry) -> ProjectLockfile`.
- `canonicalize_graph_hash(Graph) -> std::string` (format: `"sha256:<64-hex>"`).
- `sha256_hex(std::string_view) -> std::string` as a small utility in `src/common/hash_util.{h,cpp}`.
- `RuntimeAPI::write_project_lockfile(graph_path, output_path) -> CommandResult`.
- Tests covering generation, determinism, hash correctness, and an end-to-end RuntimeAPI round-trip.
- Three reviewable commits on the existing `worktree-project-lockfile` branch.

## Worktree Workflow

No new worktree. Continue in `.claude/worktrees/project-lockfile` on branch `worktree-project-lockfile`. Phase 2 adds commits on top of Phase 1 so a single PR covers the whole lockfile rollout. `ExitWorktree(action: "keep")` at the end preserves the branch for review.

Suggested commit boundaries (three small commits keep the diff reviewable):

1. **`src/common/hash_util.{h,cpp}`** + an `"abc"` NIST test-vector check (either as a new test target or inlined in the existing one).
2. **`build_lockfile_for_graph` + `canonicalize_graph_hash`** in `project_lockfile.{h,cpp}` + generation + hash tests.
3. **`RuntimeAPI::write_project_lockfile`** + round-trip test.

If (3) runs into RuntimeAPI plumbing surprises, land (1) and (2) independently.

## Files

### New

- `src/common/hash_util.h` — `std::string sha256_hex(std::string_view)` declaration.
- `src/common/hash_util.cpp` — public-domain single-file SHA-256 (~200 LOC). No third-party dependency.

### Modified

- `src/runtime/packages/project_lockfile.h` — add `build_lockfile_for_graph` and `canonicalize_graph_hash` declarations. Forward-declare `Graph`, `PackageManager`, `OperatorRegistry` to avoid pulling heavy headers into the lockfile header.
- `src/runtime/packages/project_lockfile.cpp` — implementation (includes the real runtime headers).
- `src/runtime/control/runtime_api.h` — declare `write_project_lockfile` in the persistence section alongside `save` / `save_as` (header around line 156).
- `src/runtime/control/runtime_api_persistence.cpp` — implementation.
- `tests/packages/test_project_lockfile.cpp` — add generation and hash test cases.
- `cmake/app.cmake` — add `src/common/hash_util.cpp` to the `vivid` executable source list (next to existing `src/common/*.cpp` entries, or at the end of the list if no `common/` section exists).
- `cmake/tests.cmake` — add `src/common/hash_util.cpp` to `vivid_runtime_testlib`.

If case (10) of the test plan (RuntimeAPI round-trip) proves too heavy to live in `test_project_lockfile`, add `tests/control/test_runtime_api_lockfile.cpp` and a matching `add_executable` in `cmake/tests/40-packages-media-misc.cmake`. Prefer the single-file approach first.

## Data / API additions

`project_lockfile.h` additions:

```cpp
namespace vivid {
class Graph;
class PackageManager;
class OperatorRegistry;

ProjectLockfile build_lockfile_for_graph(
    const Graph& graph,
    const PackageManager& package_manager,
    const OperatorRegistry& registry);

std::string canonicalize_graph_hash(const Graph& graph);
}  // namespace vivid
```

`runtime_api.h` addition (persistence section):

```cpp
CommandResult write_project_lockfile(const std::string& graph_path,
                                     const std::string& output_path);
```

`hash_util.h`:

```cpp
#pragma once
#include <string>
#include <string_view>

namespace vivid {
std::string sha256_hex(std::string_view input);
}
```

## Generation algorithm

```
ProjectLockfile lf;
lf.lockfile_version     = LOCKFILE_VERSION;
lf.generated_at         = rfc3339_utc_now();
lf.graph.path           = "";  // caller may override
lf.graph.schema_version = GRAPH_SCHEMA_VERSION;
lf.graph.content_hash   = canonicalize_graph_hash(graph);

lf.vivid_core.version      = VIVID_CORE_VERSION;
lf.vivid_core.commit       = "";                            // Phase 0
lf.vivid_core.operator_abi = VIVID_OPERATOR_ABI_VERSION;

// Index operator map once: type_name -> OperatorMapEntry
std::unordered_map<std::string, OperatorMapEntry> op_by_type;
for (auto& e : registry.operator_map()) op_by_type[e.type_name] = e;

std::set<std::string> seen_types;
std::set<std::string> pkg_names;
for (const auto& node : graph.nodes()) {
    seen_types.insert(node.type);
    if (const auto* p = registry.package_for_type(node.type)) {
        pkg_names.insert(*p);
    } else if (!node.pkg_name.empty()) {
        pkg_names.insert(node.pkg_name);  // fallback: on-disk provenance
    }
}

auto installed = package_manager.list();
std::unordered_map<std::string, const PackageInfo*> pkg_by_name;
for (const auto& info : installed) pkg_by_name[info.name] = &info;

for (const auto& name : pkg_names) {  // std::set iterates sorted
    auto it = pkg_by_name.find(name);
    if (it == pkg_by_name.end()) continue;  // unresolved — verify() handles
    const auto& info = *it->second;

    LockfilePackage p;
    p.name        = info.name;
    p.version     = info.version;
    p.vivid_core  = info.vivid_core;
    p.linked      = info.linked;
    p.linked_path = info.linked ? info.path : "";
    p.source.kind   = info.linked ? "local" : "git";
    p.source.url    = "";  // Phase 0
    p.source.commit = "";  // Phase 0
    lf.packages.push_back(std::move(p));
}

for (const auto& type_name : seen_types) {  // sorted
    LockfileOperator o;
    o.type = type_name;

    auto map_it = op_by_type.find(type_name);
    if (map_it != op_by_type.end()) {
        o.package      = map_it->second.package_name;
        o.operator_abi = static_cast<int>(map_it->second.abi_version);
        if (!o.package.empty()) {
            if (auto pi = pkg_by_name.find(o.package); pi != pkg_by_name.end()) {
                o.package_version = pi->second->version;
            }
        }
    }
    o.descriptor_hash = "";  // Phase 0
    lf.operators.push_back(std::move(o));
}

// lf.assets intentionally left empty — Phase 8.
return lf;
```

Determinism: using `std::set<std::string>` for both `seen_types` and `pkg_names` gives sorted iteration for free. Lockfile output is byte-stable across re-runs on identical input.

### `rfc3339_utc_now`

Small file-local helper in `project_lockfile.cpp`:

```cpp
static std::string rfc3339_utc_now() {
    auto t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%FT%TZ", &tm);
    return buf;
}
```

No need for a shared date utility yet.

### `canonicalize_graph_hash`

```cpp
std::string canonicalize_graph_hash(const Graph& graph) {
    std::string json;
    graph.save_to_string(json);            // already deterministic via ordered_json
    return "sha256:" + sha256_hex(json);
}
```

`Graph::save_to_string` is already deterministic (`nlohmann::ordered_json` throughout `build_graph_json_doc` at `src/runtime/graph/graph.cpp:1139`). No separate canonicalizer is needed.

### `RuntimeAPI::write_project_lockfile`

```cpp
CommandResult RuntimeAPI::write_project_lockfile(
    const std::string& graph_path, const std::string& output_path) {

    Graph g;
    if (!g.load_from_file(graph_path))  // confirm exact API when implementing
        return {false, "failed to load graph: " + graph_path};

    auto lf = build_lockfile_for_graph(g,
                                       impl_->package_manager,
                                       impl_->operator_registry);
    lf.graph.path = graph_path;

    std::filesystem::path out = output_path.empty()
        ? (std::filesystem::path(graph_path).parent_path() / "vivid.lock")
        : std::filesystem::path(output_path);

    auto err = save_lockfile(out, lf);
    if (!err.ok()) return {false, err.message};
    return {true, out.string()};
}
```

Before coding:
- Confirm `Graph::load_from_file` exists, or use the existing `load_from_string(read_file(path))` pattern used elsewhere.
- Confirm the exact pimpl member names for `PackageManager` and `OperatorRegistry` inside the RuntimeAPI impl. Phase 1 exploration noted `runtime_api_persistence.cpp` reaches them via an impl struct; the real names may differ.

## Tests

Additions to `tests/packages/test_project_lockfile.cpp` (the existing test binary registered in `cmake/tests/40-packages-media-misc.cmake`):

1. **empty graph** — `build_lockfile_for_graph` on a graph with zero nodes yields empty `packages`/`operators`, non-empty `generated_at`, `graph.content_hash` starting with `"sha256:"`, correct `vivid_core.version` and `operator_abi`.
2. **builtin-only graph** — load a fixture graph that uses only built-in operators. `packages` stays empty; `operators` lists every type with `package == ""`.
3. **package-backed graph** — following the fixture pattern in `tests/packages/test_package_manager.cpp`, install a test package and build a graph that uses its operator. Assert `packages[]` contains exactly one entry with the expected name/version; `operators[]` references it.
4. **operator sort stability** — two graphs with the same content but different node-insertion orders produce the same `operators[]` sequence (sorted by type).
5. **package sort stability** — analogous, sorted by name.
6. **canonical hash stability** — `canonicalize_graph_hash` on the same graph twice returns the same string.
7. **canonical hash sensitivity** — adding a node changes the hash.
8. **hash prefix** — returned string starts with `"sha256:"` and contains 64 hex chars after the colon.
9. **sha256_hex NIST test vector** — `sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"`.
10. **write_project_lockfile round-trip** — construct a minimal RuntimeAPI, save a small graph to a temp path, call `write_project_lockfile(graph_path, "")`, then `load_lockfile` the sibling file. Assert:
    - returned `CommandResult.ok == true`,
    - `result.message` equals the sibling path,
    - loaded `ProjectLockfile` matches `build_lockfile_for_graph` output (except `generated_at`, which may differ across calls — compare field-by-field skipping it, or regenerate and assert shape).

If (10) proves heavy (full RuntimeAPI bootstrap), split into `tests/control/test_runtime_api_lockfile.cpp` with its own CMake entry.

## CMake notes

- `src/common/hash_util.cpp` goes into both `cmake/app.cmake` (`add_executable(vivid …)` source list) and `cmake/tests.cmake` (`vivid_runtime_testlib` source list), same way `project_lockfile.cpp` was added in Phase 1.
- No new test target unless (10) has to split out. `test_project_lockfile` already links `vivid_runtime_testlib` which will pick up the new sources.

## Verification

Inside the worktree:

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Tests run in the background (per user preference: `run_in_background: true`).

Additional manual sanity check after commit (3): write a short C++ driver (or reuse case 10) that loads a demo graph, calls `write_project_lockfile`, and prints the sibling `vivid.lock`. Eyeball the result; `diff` against a golden fixture (stripped of `generated_at`) for determinism.

## Commit Messages

Suggested subjects:

1. `Add SHA-256 hash utility in src/common`
2. `Add project lockfile generation and graph content hash`
3. `Add RuntimeAPI::write_project_lockfile`

Bodies: one short paragraph each on what's in, what's deferred (Phase 0 / Phase 4 / Phase 8), and which tests cover the change. Include the existing `Co-Authored-By` footer.

## Acceptance Criteria

- `build_lockfile_for_graph` produces a `ProjectLockfile` with correct `lockfile_version`, `vivid_core`, stable-sorted `packages[]` and `operators[]`, empty `assets[]`.
- `canonicalize_graph_hash` returns `"sha256:<64-hex>"`; stable across re-runs; changes when the graph changes.
- `sha256_hex` passes the `"abc"` NIST test vector.
- `RuntimeAPI::write_project_lockfile(graph_path, "")` writes `vivid.lock` next to the graph and returns `{ok: true, message: <absolute path>}`.
- `ctest -R project_lockfile` stays green (all Phase 1 tests still pass; new cases pass).
- No files modified outside the "New files" / "Modified files" lists.
- Three commits on `worktree-project-lockfile` beyond `a26f7064`.

## Out of Scope (Phase 2)

- Phase 0 prerequisites: git metadata capture on install/link, operator descriptor hashing.
- `verify_lockfile` + `LockfileStatus` + classification (Phase 3).
- Control-server dispatch entries, MCP tool wrappers (Phase 4).
- `vivid lock` / `vivid verify-lock` CLI subcommands (Phase 5).
- UI indicator, load modes, `locked_unavailable` reason (Phase 6).
- Export strict mode (Phase 7).
- Asset content hashing / asset enumeration (Phase 8).

Phase 2 produces a real lockfile shape with stub provenance. Phase 0 backfills the provenance; later phases expose the generator through the broader product surface.
