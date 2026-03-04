# Package Install/Uninstall E2E Results

## Date

- 2026-03-04

## Scope

Milestone 1, item 6: real GitHub install/uninstall testing.

## Environment

- Core repo: `/Users/jeff/Developer/vivid`
- Package install root: `~/Library/Application Support/Vivid/packages`

## Tested Repos

- `https://github.com/seethroughlab/vivid-glitch.git`
- `https://github.com/seethroughlab/vivid-3d.git`

## Results

### vivid-glitch

- Install from GitHub: `PASS`
  - Clone succeeded.
  - All package operators compiled.
  - Registry probe succeeded for all 17 operators.
- Uninstall: `PASS`
  - Package directory removal succeeded.
- Reinstall from GitHub: `PASS`
  - Reinstall and operator probe succeeded again.

### vivid-3d

- Install from GitHub: `PARTIAL`
  - Operator dylib compilation succeeded for all 15 3D operators.
  - Install step failed due package test target build error:
    - `fatal error: 'yyjson.h' file not found`
    - Failure occurs in package test target (`test_render_3d`) while compiling core `operator_registry.cpp`.
- Runtime package load/probe check (from linked package build): `PASS`
  - `PackageManager: loaded package vivid-3d (15 operators)`
  - All 15 operators probed at startup.

## Conclusion

- `vivid-glitch` real GitHub install/uninstall flow is healthy.
- `vivid-3d` packaging is close, but install is blocked by package-side CMake test include configuration (`yyjson.h` in test targets), not by operator compilation.
