# Package Ecosystem Design (Milestone 3, Phase 0)

Date: 2026-03-04
Status: Phase 0 spec freeze

## 1. Current Contract (implemented today)

### Manifest (`vivid-package.json`)

Required:
- `name` (string)

Optional:
- `version` (string, defaults to `"0.0.0"` if omitted)
- `vivid_core` (string SemVer range for core compatibility, e.g. `">=0.1.0 <2.0.0"`)
- `description` (string)
- `author` (string)
- `build` (string: `""` for clang path, `"cmake"` for CMake package build path)
- `operators` (array of target names/paths)
- `gpu_operators` (array of target names/paths that need Dawn/WebGPU build settings)
- `dependencies.packages` (array of vivid package names)
- `dependencies.vendor[]` (`{name, include}` include-path additions for package-local deps)
- `tests.graphs` (array of graph paths)
- `tests.cpp` (array of C++ test source paths)

### CLI lifecycle contract

- `vivid install <url-or-path>`
  - clones/copies package into config packages dir staging folder
  - parses manifest, resolves and installs package dependencies (if resolver configured)
  - compiles package operators
  - scans compiled dylibs into operator registry
  - rolls back package directory on compile failure
- `vivid uninstall <name>`
  - unregisters package operators from registry
  - removes package dir (or symlink safely)
- `vivid link <path>`
  - validates manifest and creates symlink under package dir
  - compiles through symlink (build artifacts land in source tree)
- `vivid unlink <name>`
  - removes symlink only (never deletes source tree)
- `vivid rebuild <name>`
  - unregisters package operators, recompiles, rescans
- `vivid list-packages`
  - enumerates packages under package root and prints version/operator listing

### Current package root and scan model

- Active package root:
  - macOS: `~/Library/Application Support/Vivid/packages`
  - Linux: `$XDG_CONFIG_HOME/vivid/packages` or `~/.config/vivid/packages`
  - Windows: `%APPDATA%/Vivid/packages`
- Startup package load:
  - `PackageManager::scan_installed()` scans only that root
  - installed packages are topologically ordered by `dependencies.packages`
  - each package `build/` directory is scanned and registered

## 2. Phase 1-2 Target Model (to implement after Phase 0)

### Version model

- Keep `version` as SemVer string in package manifest.
- Add optional compatibility metadata:
  - `vivid_core` (SemVer range, e.g. `>=1.0.0 <2.0.0`)
- For update checks:
  - package identity key is `name`
  - compare installed version to remote/catalog version
  - classify update as:
    - `compatible_update`
    - `incompatible_update` (requires newer/other core constraint)

### Update policy

- Default behavior: explicit/manual update checks (CLI + MCP/control-server tool).
- Optional startup check can be added later behind a setting.
- Alerts must be non-blocking:
  - no startup failure due to update fetch/check problems
  - no forced upgrade before graph load
- User-facing alert should include a concrete remediation command.

### Scope precedence rules (target)

Planned operator/package source precedence:
1. Local graph scope
2. Project workspace scope
3. User scope (`get_config_dir()/packages`)
4. System scope (if introduced)
5. Built-in core operators

Tie-breakers:
- First scope in precedence order wins by package `name`.
- Within a scope, duplicate names are an error and must be reported explicitly.
- Diagnostics should always expose winning source path and scope.

## 3. Non-goals for Milestone 3

- Binary package distribution
- Automatic background package mutation without explicit user action
- Lockfile/reproducible transitive pinning (tracked separately as post-1.0 work)

## 4. Open questions to resolve in Phase 1

- Exact schema for `vivid_core` compatibility constraint
- Where remote “latest version” metadata comes from (catalog-only vs repo introspection)
- Whether update checks should include dependency drift warnings
