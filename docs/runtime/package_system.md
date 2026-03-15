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
Returns `TestCompileResult { success, executable_path, error_output, test_name }`.

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
