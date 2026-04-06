# Package System

## Purpose

This directory manages the full lifecycle of operator packages: discovery, installation, compilation, testing, and scaffolding. Packages are git repos with a `vivid-package.json` manifest that can contain one or more operators.

## Key Files

| File | Role |
|------|------|
| `package_manager.h` | `PackageManager` — main facade for install/uninstall/link/rebuild/scan |
| `package_manager_discovery.cpp` | Scans across scopes (local, workspace, user, extra) with precedence resolution |
| `package_manager_manifest.cpp` | Parses `vivid-package.json` → `PackageInfo` struct |
| `package_manager_install.cpp` | Git clone or local copy with recursive dependency resolution |
| `package_manager_build.cpp` | Delegates to `PackageCompiler` for operator compilation |
| `package_compiler.h/cpp` | Invokes clang++ or cmake to compile `.cpp` → `.dylib` |
| `package_test_runner.h/cpp` | Runs graph tests (`.json` files) and C++ unit tests (`test_*.cpp`) |
| `package_catalog.h/cpp` | Fetches remote package index on a detached thread, merges with local state |
| `package_scaffolder.h/cpp` | Generates boilerplate package structure from templates |

## How It's Organized

### Discovery

`PackageManager` scans for packages across multiple scopes in precedence order: project-local, workspace, user (`~/.vivid/packages/`), and any extra paths. Each scope can contain installed packages (git-cloned) or linked packages (symlinked for development). Discovery produces a merged view where higher-precedence scopes shadow lower ones.

### Compilation

`PackageCompiler` handles the build step. For simple packages it invokes clang++ directly; for packages with CMakeLists.txt it runs cmake. Build output is staged and truncated to prevent runaway compilation from consuming disk. The compiler validates paths stay within the package root to prevent path traversal.

### Testing

`PackageTestRunner` supports two test shapes:
- **Graph tests:** load a `.json` graph, build it, check for errors
- **C++ tests:** compile and run `test_*.cpp` files against the operator API

Test results are structured with stable status codes for machine consumption.

### Catalog

`PackageCatalog` fetches a remote package index (JSON) on a background thread. The UI package browser displays this alongside locally installed packages. Install requests flow through `PackageManager::install()` which handles git clone + dependency resolution + compilation.

## Relationships

- **Upstream:** `OperatorRegistry` loads dylibs discovered by package scanning
- **Downstream:** `ControlServer` exposes package endpoints; `RuntimeBootstrap` triggers initial package scan at startup
- **Build tools:** `ToolDiscovery` (in `src/runtime/core/`) finds cmake and clang++ on the system

## See Also

- `docs/runtime/package_system.md` — package system design and manifest format
- `AGENTS.md` §Package System — install/link/rebuild workflows
