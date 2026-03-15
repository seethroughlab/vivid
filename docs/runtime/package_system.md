# Package System — Install, Compile, Link, Test

## Overview

The package system lets users extend Vivid with third-party operator collections.
Two classes handle different concerns:

- **`PackageCompiler`** — knows how to invoke clang++ or cmake to compile `.cpp` → `.dylib`
- **`PackageManager`** — knows about package directories, manifests, dependencies, git URLs

## Package Structure

A package is a directory containing:
- `vivid-package.json` — manifest (name, version, operators list, dependencies)
- `audio/`, `control/`, `gpu/` subdirectories with `.cpp` operator files
- Optional `tests/` directory with C++ test sources and/or graph test files

## Package Test Contract

The manifest test surface is intentionally narrow:

- `tests.graphs`
- `tests.cpp`

Those two arrays have different ownership boundaries:

- `tests.graphs`
  - graph smoke / graph contract coverage
  - package-relative `.json` graph files
  - run through the core graph loader and scheduler
- `tests.cpp`
  - lightweight package tests that fit the generic core runner
  - package-relative `.cpp` entrypoints only
  - must be self-contained single-source tests with a standalone `main()`
  - compile only against:
    - Vivid headers
    - package `operators/` headers
    - declared vendored include dirs
- package-local CMake / CTest
  - canonical home for heavier package-specific C++ tests
  - use this for framework-based tests, custom link environments, or multi-source test setups

The generic runner is not a best-effort clone of package-local build systems.
Manifest-declared `tests.cpp` entries that fall outside the supported subset fail early
with explicit classification instead of ambiguous compile noise.

## `vivid-package.json` Fields

```json
{
  "name": "vivid-glitch",
  "version": "1.2.0",
  "vivid_core": ">=1.0.0 <2.0.0",
  "description": "Glitch effects for Vivid",
  "author": "Someone",
  "operators": ["control/glitch_switcher"],
  "gpu_operators": ["gpu/pixel_sort"],
  "dependencies": {
    "packages": ["vivid-base"],
    "vendor": [{ "name": "stb_image", "include": "deps/stb" }]
  },
  "tests": {
    "graphs": ["tests/test_glitch.json"],
    "cpp": ["tests/test_glitch_ops.cpp"]
  }
}
```

Parsed by `PackageManager::parse_manifest()` into `PackageInfo`.

## `PackageInfo`

```cpp
struct PackageInfo {
    std::string name, version, vivid_core;
    std::string description, author, category;
    std::vector<std::string> tags;
    std::vector<std::string> operators;      // e.g. "audio/drum_kick"
    std::vector<std::string> gpu_operators;  // operators needing Dawn
    std::string path;           // absolute path on disk
    std::string source_scope;   // local|workspace|user|extra
    std::string build_type;     // "" = clang++ (default), "cmake" = cmake build
    bool linked;                // true = symlinked via vivid link
    PackageDependencies dependencies;
    PackageTests tests;
};
```

## `PackageCompiler`

```cpp
PackageCompiler(const std::string& vivid_src_dir, const std::string& vivid_build_dir);
```

### Compile One Operator
```cpp
CompileResult compile_operator(package_dir, operator_rel_path,
                               bool needs_gpu,
                               const std::vector<std::string>& extra_include_dirs = {});
```
`operator_rel_path` is relative to `package_dir`, e.g. `"audio/drum_kick"`.
`needs_gpu = true` adds Dawn include paths and framework linkage.
Returns `CompileResult { success, dylib_path, error_output, operator_name }`.

### Compile All
```cpp
std::vector<CompileResult> compile_all(const std::string& package_dir);
// or from pre-parsed lists:
std::vector<CompileResult> compile_all(package_dir, operators, gpu_operators, vendor_include_dirs);
```

### Compile Test
```cpp
TestCompileResult compile_test(package_dir, test_rel_path, extra_include_dirs = {});
```
Returns `TestCompileResult` with:

- `success`
- `test_name`
- `normalized_rel_path`
- `executable_path`
- `code`
- `message`
- `error_output`

Representative stable `code` values include:

- `cpp_ready`
- `cpp_compiled`
- `missing_test_file`
- `unsupported_test_extension`
- `path_outside_package`
- `unsupported_cpp_test_shape`
- `cpp_compile_failed`

## `PackageManager`

```cpp
PackageManager(PackageCompiler& compiler, OperatorRegistry& registry);
```

### Install / Uninstall
```cpp
InstallResult install(const std::string& url);  // git URL or local path
bool uninstall(const std::string& name);
```
`install()` → clone/copy to packages dir → `parse_manifest()` → resolve deps → `compile_package()` → `registry.scan()`.
`InstallResult` fields: `success`, `error`, `info`, `compile_results`, `installed_deps`.

If a package mutation affects the active graph, the runtime now rebuilds through transactional
snapshot-apply paths instead of mutating the live scheduler or audio engine in place. This keeps
install/link/rebuild/unlink flows consistent with the same restore-on-failure guarantees used by
`RuntimeAPI`.

### Link / Unlink (Development)
```cpp
InstallResult link(const std::string& path);   // symlink local dir into packages dir
bool unlink(const std::string& name);          // remove symlink only, never source
```

### Rebuild
```cpp
InstallResult rebuild(const std::string& name);
```
Recompiles all operators for an installed/linked package.

### List / Scan
```cpp
std::vector<PackageInfo> list();  // all installed packages
void scan_installed();            // scan all packages into registry (called at startup)
static std::string packages_dir(); // platform config dir / packages
```

## `run_package_tests()`

`run_package_tests()` classifies each manifest-declared test into a stable result shape:

- `name`
- `type`
- `status`
- `code`
- `reason`
- `output`

Package-level output also includes:

- summary counts
- `notes` for contract guidance such as:
- package has no manifest tests
- some manifest `tests.cpp` entries are outside the generic runner contract and should stay in package-local CMake / CTest

This split is intentional:

- manifest tests are the lightweight, core-owned package contract surface
- package-local CMake / CTest remains the canonical home for heavier repo-specific coverage
- unsupported manifest `tests.cpp` shapes fail early and explicitly instead of silently depending
  on whatever build environment happens to exist locally

Representative stable result codes include:

- graph results:
  - `graph_passed`
  - `graph_needs_gpu`
  - `graph_needs_audio`
  - `graph_load_failed`
  - `graph_build_failed`
  - `graph_node_error`
  - `unsupported_graph_test_shape`
- shared validation:
  - `missing_test_file`
  - `path_outside_package`
  - `duplicate_test_entry`
- cpp results:
  - `cpp_passed`
  - `unsupported_test_extension`
  - `unsupported_cpp_test_shape`
  - `cpp_compile_failed`
  - `cpp_runtime_failed`
  - `cpp_runtime_launch_failed`
  - `cpp_runtime_abnormal`

### Dependency Resolution
```cpp
void set_resolver(PackageResolver resolver);
// PackageResolver = std::function<std::string(const std::string& package_name)>
```
Used to convert package names to install URLs. Set by main.cpp using the package catalog.
Circular dependency detection via `installing_chain` set in `install_with_chain()`.

### URL Normalization
```cpp
static std::string normalize_github_url(const std::string& url);
```
Handles shorthand (`user/repo`), strips browser paths, ensures `.git` suffix.

### Version Assessment
```cpp
static PackageUpdateAssessment assess_update(installed, remote_version, remote_vivid_core, core_version);
static PackageUpdateClass classify_version_delta(saved_version, installed_version);
```

`PackageUpdateClass`: `UpToDate`, `CompatibleUpdate` (same major), `IncompatibleUpdate` (major changed),
`RemoteOlderOrEqual`, `InvalidVersionData`.

## Package Scopes

Packages are resolved across multiple scopes (precedence: local > workspace > user > extra):
- **local** — packages dir next to the app binary
- **workspace** — project-local packages
- **user** — `~/Library/Application Support/Vivid/packages` (macOS)
- **extra** — additional configured paths

`resolve_packages()` discovers candidates across all scopes and picks winners by precedence.
