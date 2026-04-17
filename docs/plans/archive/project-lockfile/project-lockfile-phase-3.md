# Project Lockfile — Phase 3 Execution Plan

Scope of this doc: Phase 3 only ("Verification + Dependency Status") from [project-lockfile-reproducibility-plan.md](./project-lockfile-reproducibility-plan.md). Builds on Phase 2 ([project-lockfile-phase-2.md](./project-lockfile-phase-2.md)). Phase 0 prerequisites (git metadata capture, descriptor hashing) remain deferred; control-server dispatch, CLI, UI, strict mode, and asset hashes are out of scope.

## Context

Phase 2 delivered the producer: `build_lockfile_for_graph`, `canonicalize_graph_hash`, and `RuntimeAPI::write_project_lockfile`. A `vivid.lock` can be produced and saved.

Phase 3 delivers the **consumer**: given an existing lockfile, report how well the current environment satisfies it. Primary deliverables:

1. A pure `verify_lockfile` function that walks the lockfile's packages/operators and compares against the live `PackageManager` and `OperatorRegistry`, emitting structured findings.
2. A stable classification taxonomy (`LockfileOverall`, `LockfileSeverity`, finding-ID constants).
3. Two RuntimeAPI methods: `verify_project_lockfile` (explicit lockfile path) and `get_project_dependency_status` (sibling-lockfile discovery with a distinct `NoLockfile` overall state).

By the end of Phase 3, a developer can call `get_project_dependency_status(pm, graph_path)` from a test driver and get a diff-stable JSON payload describing every discrepancy between the current environment and the locked state.

## Classifications emitted

Phase 3 emits the subset of master-plan classifications that can be computed without Phase 0 (git metadata, descriptor hashes) or Phase 8 (asset content hashes):

| Finding ID                      | Severity | Source                                  |
|---------------------------------|----------|-----------------------------------------|
| `missing_package`               | Critical | `PackageManager::list()` lookup miss    |
| `missing_operator`              | Critical | `OperatorRegistry::operator_map()` miss |
| `compatible_update`             | Info     | `PackageManager::classify_version_delta` → `CompatibleUpdate` or `RemoteOlderOrEqual` |
| `incompatible_update`           | Critical | `classify_version_delta` → `IncompatibleUpdate` |
| `abi_mismatch`                  | Critical | Core or per-operator ABI differs from lockfile |
| `vivid_core_version_mismatch`   | Warning  | `VIVID_CORE_VERSION` differs from lockfile |
| `graph_content_drift`           | Info     | Freshly-hashed graph differs from `lockfile.graph.content_hash` |

Defined but not yet emitted (reserved for Phase 0 / Phase 8): `descriptor_hash_mismatch`, `linked_unpinned`, `asset_missing`, `asset_changed`. The constants live in `namespace lockfile_finding` so downstream callers can reference them from day one.

Overall severity:
- Any `Critical` finding → `LockfileOverall::Mismatch`
- Any `Warning` or `Info` finding → `LockfileOverall::CompatibleDrift`
- No findings → `LockfileOverall::Match`
- (`NoLockfile` is only produced by `get_project_dependency_status` when the sibling is absent; `verify_lockfile` itself never returns it.)

## Deliverable

- `LockfileOverall`, `LockfileSeverity`, `LockfileFinding`, `LockfileStatus`, and the finding-ID constants namespace in `project_lockfile.h`.
- `verify_lockfile(lockfile, graph, pm, registry) -> LockfileStatus` implementation in `project_lockfile.cpp`.
- `lockfile_status_to_json(status, indent=2) -> std::string` helper for RuntimeAPI layer.
- `RuntimeAPI::verify_project_lockfile(pm, graph_path, lockfile_path)` — explicit-path verify.
- `RuntimeAPI::get_project_dependency_status(pm, graph_path)` — sibling-discovery; `NoLockfile` when absent.
- Classification-matrix tests, a generate-then-verify round-trip, and three RuntimeAPI tests.
- Three commits on the existing `worktree-project-lockfile` branch.

## Worktree workflow

Continue inside `.claude/worktrees/project-lockfile` on branch `worktree-project-lockfile`. Phase 3 adds commits on top of Phase 2. The commit boundaries:

1. **`Add lockfile verification result types`** — header additions (enums, structs, finding-ID constants, `verify_lockfile` + `lockfile_status_to_json` declarations) plus `lockfile_status_to_json` implementation. Builds clean; no behavioral change.
2. **`Add verify_lockfile and classification tests`** — the algorithm implementation + 8 classification tests.
3. **`Add RuntimeAPI::verify_project_lockfile and get_project_dependency_status`** — the two new methods + 4 RuntimeAPI tests.

If (3) runs into architectural surprises, (1) and (2) stand alone.

## Files

### Modified
- `src/runtime/packages/project_lockfile.h` — add result types, finding-ID constants, `verify_lockfile` and `lockfile_status_to_json` declarations.
- `src/runtime/packages/project_lockfile.cpp` — implement `verify_lockfile` + `lockfile_status_to_json` + `to_string` helpers for enums.
- `src/runtime/control/runtime_api.h` — declare the two new RuntimeAPI methods in the persistence section.
- `src/runtime/control/runtime_api_persistence.cpp` — implement them.
- `tests/packages/test_project_lockfile.cpp` — classification and RuntimeAPI tests.

### New
- None. Phase 3 adds no files; it extends Phase 1/2 surfaces.

## Data model

Additions to `project_lockfile.h`:

```cpp
enum class LockfileOverall { Match, CompatibleDrift, Mismatch, NoLockfile };
enum class LockfileSeverity { Info, Warning, Critical };

namespace lockfile_finding {
    inline constexpr const char* kMatch                    = "match";
    inline constexpr const char* kMissingPackage           = "missing_package";
    inline constexpr const char* kMissingOperator          = "missing_operator";
    inline constexpr const char* kCompatibleUpdate         = "compatible_update";
    inline constexpr const char* kIncompatibleUpdate       = "incompatible_update";
    inline constexpr const char* kAbiMismatch              = "abi_mismatch";
    inline constexpr const char* kVividCoreVersionMismatch = "vivid_core_version_mismatch";
    inline constexpr const char* kGraphContentDrift        = "graph_content_drift";
    // Reserved (Phase 0 / Phase 8):
    inline constexpr const char* kDescriptorHashMismatch   = "descriptor_hash_mismatch";
    inline constexpr const char* kLinkedUnpinned           = "linked_unpinned";
    inline constexpr const char* kAssetMissing             = "asset_missing";
    inline constexpr const char* kAssetChanged             = "asset_changed";
}

struct LockfileFinding {
    std::string id;
    LockfileSeverity severity = LockfileSeverity::Info;
    std::string subject;
    std::string message;
    std::string suggestion;
};

struct LockfileStatus {
    LockfileOverall overall = LockfileOverall::Match;
    std::vector<LockfileFinding> findings;
};

LockfileStatus verify_lockfile(const ProjectLockfile& lockfile,
                               const Graph& graph,
                               PackageManager& package_manager,
                               const OperatorRegistry& operator_registry);

std::string lockfile_status_to_json(const LockfileStatus& status, int indent = 2);
```

`verify_lockfile` takes the graph explicitly so callers control which graph is verified. The RuntimeAPI layer loads the graph from disk and passes it in — same as `write_project_lockfile`.

## Algorithm

```
status = { overall: Match, findings: [] }

// Core
if (lockfile.vivid_core.version != VIVID_CORE_VERSION):
    add {kVividCoreVersionMismatch, Warning, "vivid_core", delta, "re-lock or downgrade core"}
if (lockfile.vivid_core.operator_abi != VIVID_OPERATOR_ABI_VERSION):
    add {kAbiMismatch, Critical, "vivid_core", delta, "rebuild operator dylibs against this core"}

// Graph content
if (!lockfile.graph.content_hash.empty()
    && canonicalize_graph_hash(graph) != lockfile.graph.content_hash):
    add {kGraphContentDrift, Info, lockfile.graph.path, ..., "re-run write_project_lockfile"}

// Packages
installed_by_name = index pm.list()
for each LockfilePackage p in lockfile.packages:
    if !installed: add {kMissingPackage, Critical, p.name, ..., "install p.name@p.version"}
    elif installed.version != p.version:
        switch PackageManager::classify_version_delta(p.version, installed.version):
            CompatibleUpdate|RemoteOlderOrEqual: add {kCompatibleUpdate, Info}
            IncompatibleUpdate:                   add {kIncompatibleUpdate, Critical}
            InvalidVersionData:                   add {kIncompatibleUpdate, Warning}
    // Phase 0 TODO: kLinkedUnpinned when info.linked && p.source.commit.empty()

// Operators
op_by_type = index operator_registry.operator_map()
for each LockfileOperator o in lockfile.operators:
    if !found: add {kMissingOperator, Critical, o.type, ..., install-or-rebuild suggestion}
    elif o.operator_abi != 0 && entry.abi_version != o.operator_abi:
        add {kAbiMismatch, Critical, o.type, delta, "rebuild " + entry.package_name}
    // Phase 0 TODO: kDescriptorHashMismatch

// Overall = worst severity seen
if any Critical: overall = Mismatch
elif any Warning or Info: overall = CompatibleDrift
```

Determinism: findings emitted in iteration order (core → graph → packages (lockfile order, already sorted by Phase 2) → operators (same)). The JSON output is diff-stable via `nlohmann::ordered_json`.

## RuntimeAPI methods

```cpp
CommandResult RuntimeAPI::verify_project_lockfile(PackageManager& pm,
                                                  const std::string& graph_path,
                                                  const std::string& lockfile_path) {
    // Load graph + lockfile; ok=false only on I/O errors.
    // Run verify_lockfile, return ok=true with message = lockfile_status_to_json(status).
}

CommandResult RuntimeAPI::get_project_dependency_status(PackageManager& pm,
                                                        const std::string& graph_path) {
    // Look for <graph_path.parent_path()>/vivid.lock.
    // Missing: return ok=true, message = {"overall":"no_lockfile","findings":[]}.
    // Present: delegate to verify_project_lockfile.
}
```

Return shape: `CommandResult.ok = true` except for I/O errors. A mismatch is expressed inside the JSON payload, not by failing the call. Matches the existing dispatch-handler idiom where structured output rides in `message`.

## Tests

Additions to `tests/packages/test_project_lockfile.cpp` under the existing `test_project_lockfile` target. Fresh cases:

**verify_lockfile (pure):**
1. Empty lockfile + empty graph → `Match`, zero findings.
2. `missing_package` — lockfile names an uninstalled package.
3. `missing_operator` — lockfile names an unregistered type.
4. `vivid_core_version_mismatch` — lockfile core version differs → Warning → `CompatibleDrift`.
5. Core `abi_mismatch` — lockfile operator_abi = 999 → Critical → `Mismatch`.
6. `graph_content_drift` — fake content_hash → Info → `CompatibleDrift`.
7. Overall precedence — Info + Critical → `Mismatch`.
8. Generate-then-verify — `build_lockfile_for_graph` + `verify_lockfile` on the same env → sensible result shape (no `missing_package` findings since nothing was locked).
9. `lockfile_status_to_json` — parse the output and assert field values.

**RuntimeAPI:**
10. `verify_project_lockfile` round-trip — write + verify; assert `overall` and `findings` exist in the JSON.
11. `verify_project_lockfile` missing lockfile path — `ok = false`, message mentions load failure.
12. `get_project_dependency_status` no sibling — `overall == "no_lockfile"`, empty findings.
13. `get_project_dependency_status` happy path — write first, then status; assert `overall != "no_lockfile"`.

All tests run under the existing `test_project_lockfile` target — no new CMake target.

## Verification

```bash
cmake --build build --target test_project_lockfile
ctest --test-dir build --output-on-failure -R project_lockfile
```

Run in the background per user preference. For manual sanity after commit (3), reuse test 13 or write a short driver that calls `get_project_dependency_status` against a demo graph and eyeballs the JSON.

## Commit Messages

Suggested subjects:

1. `Add lockfile verification result types`
2. `Add verify_lockfile and classification tests`
3. `Add RuntimeAPI::verify_project_lockfile and get_project_dependency_status`

Bodies: one short paragraph each on what's in, what's deferred (Phase 0 / Phase 4 / Phase 8), and which classifications are covered.

## Acceptance Criteria

- `verify_lockfile` emits the Phase 3 classifications with the right severity and computes `overall` from the worst finding.
- Generate-then-verify produces no `missing_package` findings for a lockfile that has no packages.
- `get_project_dependency_status` returns `overall = NoLockfile` when the sibling is missing, any other valid overall when present.
- `RuntimeAPI` methods return `ok = true` with JSON payload except for hard I/O errors.
- `ctest -R project_lockfile` stays green across all existing and new cases.
- No new files; changes confined to the Modified list.
- Three commits on `worktree-project-lockfile` beyond the Phase 2 commits.

## Out of Scope (Phase 3)

- Phase 0 prerequisites: git metadata capture, operator descriptor hashing (leaves `descriptor_hash_mismatch` / `linked_unpinned` unemitted).
- Asset hashing (Phase 8) — leaves `asset_missing` / `asset_changed` unemitted.
- Control-server dispatch entries and MCP tool wrappers (Phase 4).
- CLI subcommands `vivid verify-lock` (Phase 5).
- UI indicator + load modes + `locked_unavailable` reason (Phase 6).
- Export strict mode (Phase 7).

Phase 3 makes the lockfile useful for CI-style checking through the RuntimeAPI while leaving end-user surfaces for later phases.
