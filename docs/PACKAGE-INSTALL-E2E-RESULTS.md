# Package Install/Uninstall E2E Results

## Date

- 2026-03-04

## Scope

Milestone 1, item 6: real GitHub install/uninstall and failure-case testing.

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

- Install from GitHub: `PASS`
  - Clone/configure/build succeeded.
  - Registry probe succeeded for all 15 operators.
- Uninstall: `PASS`
  - Package directory removal succeeded.
- Reinstall from GitHub: `PASS`
  - Reinstall and operator probe succeeded again.

## Failure Case Matrix

- Bad URL (`https://github.com/seethroughlab/this-repo-does-not-exist-xyz987.git`): `PASS`
  - Clear clone error (`Repository not found`).
  - No staging/package residue left behind.
- Missing manifest (`vivid-package.json` absent): `PASS`
  - Clear error (`Invalid or missing vivid-package.json in package`).
  - No staging/package residue left behind.
- Compile error in package: `PASS`
  - Install returns failure with compile diagnostics.
  - Rollback cleanup now succeeds (failed package directory is removed).
- Network failure (`https://127.0.0.1:1/fail-network.git`): `PASS`
  - Clear network error (`Failed to connect`).
  - No staging/package residue left behind.
- Missing local build tools (`git`, `clang++`, `cmake`): `PASS`
  - Preflight now fails fast with clear remediation text:
    - `git`: install Git / Xcode Command Line Tools.
    - `clang++`: run `xcode-select --install`.
    - `cmake`: install via Homebrew (`brew install cmake`).
  - No partial package residue left behind in mocked-missing-tool validation.

## Live Palette/Type Visibility (No Restart)

- Live control-server runtime test (`/list_types`, `/install_package`, `/uninstall_package`): `PASS`
  - `vivid-glitch` uninstall removed `Stutter`/`Datamosh` from type list immediately.
  - `vivid-glitch` install restored `Stutter`/`Datamosh` in the same running session.
  - `vivid-3d` uninstall removed `Render3D`/`Particles3D`; install restored them in the same running session.
  - No app restart required for type-list/palette updates.
  - Note: long package installs can exceed current control-server request timeout and return `{\"ok\":false,\"error\":\"timeout\"}` even when install eventually completes and types become available.

## Conclusion

- `vivid-glitch` and `vivid-3d` real GitHub install/uninstall/reinstall flows are healthy.
- Failure handling is now good across tested cases (bad URL, missing manifest, compile failure, network failure).
