# Package Ecosystem Baseline (Before Milestone 3 Implementation)

Date: 2026-03-04
Scope: Baseline evidence captured before Phase 1 changes

## 1. Core package CLI baseline

Command:
```bash
./build/vivid list-packages
```

Observed:
- Linked packages detected and listed with versions/operators:
  - `vivid-sequencers` (`0.1.0`, linked, 8 ops)
  - `vivid-3d` (`0.1.0`, linked, 15 ops)
  - `vivid-wavetable` (`0.1.0`, linked, 1 op)
  - `vivid-plexus` (`0.1.0`, linked, 2 ops)
  - `vivid-drums` (`0.1.0`, linked, 6 ops including `drum_tom`)
- Package metadata shown by CLI currently includes:
  - `name`, `version`, linked marker, description, operator lists

## 2. Core package manager test baseline

Command:
```bash
./build/test_package_manager ./build
```

Observed:
- Most install/uninstall/link/dependency/error-path checks pass.
- Current known failing area: cmake-built package fixture in `test_package_manager`.
- Failure details:
  - test fixture CMake package cannot find `operator_api/operator.h`
  - Result: 4 failures in cmake install/uninstall assertions for that fixture run

Interpretation:
- This is a useful “before” snapshot and should be fixed or intentionally adjusted in Milestone 3 Phase 1 when touching package build/versioning paths.

## 3. MCP/control-server package tool baseline

Command:
```bash
./build/test_control_server ./build
```

Observed:
- Control server test suite passes overall (0 failures), including graph CRUD + undo/redo paths.
- No direct automated coverage currently found for package RPC methods in test sources:
  - `install_package`, `uninstall_package`, `link_package`, `unlink_package`, `rebuild_package`, `list_packages`

Interpretation:
- Package RPC behavior exists in `src/runtime/control_server.cpp` but needs dedicated test coverage as Milestone 3 proceeds.

## 4. Package repo smoke-workflow baseline

Command:
```bash
for r in vivid-wavetable vivid-drums vivid-plexus vivid-sequencers vivid-3d vivid-glitch; do
  ls "/Users/jeff/Developer/$r/.github/workflows"
done
```

Observed:
- All package sibling repos currently include `smoke.yml` workflow files.

Notes:
- This confirms workflow presence locally, not GitHub run status.
- Prior smoke protocol and expectations are documented in:
  - `docs/testing/PACKAGE-SMOKE-TEST.md`
