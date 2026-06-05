# Audit 07: Package, Build, Export & Dependencies

**Date:** 2026-06-26
**Status:** Audited 2026-06-05 (verify-gated; 9 candidates → 2 confirmed, 7 dismissed)

## Purpose

Audit the package lifecycle, CMake build structure, standalone export pipeline, and dependency management for reproducibility, boundary clarity, and developer-loop reliability.

## Scope

- `src/runtime/packages/`
- `cmake/`
- `CMakeLists.txt`
- `src/export/`
- `docs/runtime/package_system.md`
- `docs/ARCHITECTURE.md` dependency manifest
- Package, export, CLI, and build-related tests

## Primary Questions

- [ ] Are package install, link, rebuild, uninstall, and dependency resolution semantics clear?
- [ ] Are build targets modular enough for app, operators, tests, packages, and export?
- [ ] Are dependency versions and platform frameworks declared in one trustworthy place?
- [ ] Does standalone export preserve graph/operator behavior without hidden runtime assumptions?
- [ ] Are package compilation failures reported with actionable diagnostics?
- [ ] Are test partitions aligned with actual resource requirements?
- [ ] Are generated or copied build artifacts isolated from source-tracked state?

## Subsystem Checklist

- [ ] Trace package manifest parsing through install/link/rebuild/uninstall.
- [ ] Review `add_vivid_operator()` and package compilation behavior for seed and package operators.
- [ ] Inspect test CMake partitions for duplication, stale dependencies, and resource labels.
- [ ] Check export pipeline assumptions about graph assets, operator sources, and static linking.
- [ ] Compare dependency declarations with docs and platform assumptions.
- [ ] Verify tests cover broken manifests, missing package dependencies, compile failures, and export fixture behavior.
- [ ] Identify build files that are oversized or own too many concerns.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings

This subsystem audited **clean on correctness, reproducibility, and tests**: the verify pass refuted 7
of 9 candidates — including **four "no test" claims where the test already exists** and two
recommendations that are technically impossible. **No package-manager findings.** The 2 confirmed
findings are both **Low** and both about the **`vivid_package_operator` CMake package path** (the
alternate, currently-unused build path — the standard path is direct clang++ via `package_compiler.cpp`).

> Reproducibility ✅ — `cmake/dependencies.cmake` is the single trustworthy place: all FetchContent deps
> pinned by tag/release, platform frameworks conditionally linked, included once. Artifact isolation ✅ —
> generated `*_generated_registration.cpp`, staged dylibs, `operator_manifest.json`, factory presets all
> land in `CMAKE_BINARY_DIR`, not source (minor: no `.gitignore` guidance for cmake-package authors re
> their `build/` output). Export ✅ — embeds operator sources via the manifest + byte-identical graph JSON;
> lane strategy is recomputed but from arch-independent inputs (dismissed 07-F7).

### CMake / build findings

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 07-F1 | Low | Maintainability | `vivid_package_operator()`'s `GPU` flag is parsed (`cmake_parse_arguments`) but **never used** — a dead parameter. The cmake package path has no working webgpu-linkage for GPU operators | `cmake/VividPackageSupport.cmake:3,18` |
| 07-F4 | Low | Docs | The CMake package build path (`VividPackageSupport.cmake` / `vivid_package_operator`) is undocumented in `package_system.md` — a third-party author can't discover it without reading cmake source | `docs/runtime/package_system.md`; `cmake/VividPackageSupport.cmake` |

> Both were filed Medium/Low and confirmed at **Low** — the GPU flag is dead/confusing but causes no
> current breakage (no cmake-based package exists; the scaffolder emits the clang++ path; the one test
> using the flag passes a no-op scalar op). **These tie directly to the deferred Audit-02 sibling
> migration**, which wants siblings (incl. GPU-heavy `vivid-3d`/`vivid-glitch`) to build via
> `vivid_package_operator` — so making the GPU flag actually work + documenting the path is groundwork
> for that migration.

### Evidence & Recommendation

**07-F1 — `vivid_package_operator` GPU flag is dead** (Low, Maintainability · CMake)
- *Evidence:* `VividPackageSupport.cmake:3` documents `vivid_package_operator(name source [GPU]
  [EXTRA_LIBS ...])`; line 18 parses it (`cmake_parse_arguments(ARG "GPU" ...)` → `ARG_GPU`), but the
  function body (17-64) never references `ARG_GPU`. The package build is a **standalone** cmake project
  rooted at the package dir, so `webgpu`/`vivid_operator_api` targets from the main tree **don't exist**
  there — meaning `EXTRA_LIBS webgpu` wouldn't resolve either. The real webgpu support lives only in the
  clang++ path (`package_compiler.cpp:282` `needs_gpu`, derived from the manifest `gpu_operators`).
- *Impact:* A GPU operator built via the cmake path gets no webgpu include/lib. Latent (no cmake-package
  exists today), but a blocker for the Audit-02 sibling migration onto this path.
- *Recommendation:* Implement `ARG_GPU` by **threading webgpu include/lib paths through cmake cache vars**
  (mirror how `package_manager_build.cpp` passes Highway/dragonbox via `VIVID_*_INCLUDE_DIR/LIBRARY`, and
  how `package_compiler.cpp:282-331` handles wgpu) — *not* by linking a `webgpu` target that doesn't
  exist in the package context. Until then, either remove the flag or document it as a no-op.

**07-F4 — CMake package path undocumented** (Low, Docs)
- *Evidence:* `package_system.md` documents the clang++ default compile path in depth but never names
  `VividPackageSupport.cmake` / `vivid_package_operator()` nor shows an example `CMakeLists.txt`. Grep
  confirms these appear in **no** user-facing docs (only in these audit plan files). *(The macro
  signature **is** in the cmake header's line 3, contrary to part of the finding's evidence.)*
- *Impact:* A CMake-based package author can't discover the integration without reading cmake source.
- *Recommendation:* Add a "CMake-Based Packages" section to `package_system.md` with an example
  `CMakeLists.txt` using `vivid_package_operator()`, the GPU/EXTRA_LIBS pattern, and the `VIVID_*` cache
  vars (`VIVID_SRC_DIR`, `VIVID_BUILD_DIR`, `VIVID_PLUGIN_SUFFIX`).

### Test Gaps

Reported separately from findings (several finder-proposed gaps were **refuted as already covered** — see
Dismissed). Genuinely-missing coverage, with repro commands:

- **GPU operator via the cmake path** end-to-end (no test exercises `vivid_package_operator` with a real
  WebGPU-using operator). Repro after a fix: a fixture cmake-package GPU op →
  `ctest -R test_package_manager`.
- Concurrent package operations (install/rebuild in parallel on the same package) — `ctest -R test_package_stress`.
- Circular dependency detection across **>2** levels (A→B→C→A) — `ctest -R test_package_manager`.
- Partial-install **rollback** when a transitive dep fails mid-chain (the success/skip paths *are* tested).

### Docs to Update
- `docs/runtime/package_system.md` — "CMake-Based Packages" section (07-F4).
- `cmake/VividPackageSupport.cmake` — header: document the full `vivid_package_operator` signature +
  invocation example; clarify GPU handling (07-F1/07-F4).
- `docs/ARCHITECTURE.md` §Export — note lane strategy is recomputed (deterministic from topology;
  basis of dismissed 07-F7).
- `src/export/export_pipeline.h` — minor: the strict/lockfile contract is already documented; the
  per-operator resolution errors are already descriptive (basis of dismissed 07-F5/07-F8).

## Follow-up

**Immediate** — none. No correctness / reproducibility defect.

**Near-term** — ✅ **DONE 2026-06-05** (build + package tests green)
- 07-F1: `vivid_package_operator`'s `GPU` flag is now real — extracted `PackageCompiler::managed_webgpu_paths()`
  (shared with the clang++ path), the package manager threads `VIVID_WEBGPU_INCLUDE_DIR`/`VIVID_WEBGPU_LIB_DIR`,
  and the cmake module adds the include + links `wgpu_native` under `GPU` (warns if paths absent).
- 07-F4: documented the CMake package path — "CMake-Based Packages (`vivid_package_operator`)" section in
  `package_system.md` (example CMakeLists, GPU/EXTRA_LIBS, injected `VIVID_*` vars) + expanded the cmake
  module header.

**Backlog**
- Add the GPU-cmake-path + concurrent-op + >2-level-cycle + partial-rollback tests.
- Minor: `.gitignore` guidance for cmake-package authors (`build/`, dylib outputs).
- The export/ARCHITECTURE doc clarifications above.

### Dismissed (verification-refuted)

Seven candidates were refuted — notably **four** that claimed missing tests which in fact exist:

- **07-F2** (no timeout on package tests) — refuted: CTest applies a default timeout (no "indefinite
  block"), runs parallel, and the file-wide convention omits explicit TIMEOUT except for known-slow tests.
- **07-F3** (package-test resource-lock coordination) — refuted: the tests write **disjoint named
  subdirs** and never wipe the shared root; the finding conflated the user packages dir with the local
  discovery root (which *does* have the lock, applied to the right tests).
- **07-F5** (dependency export behavior undocumented) — refuted: the strict/lockfile contract is in the
  header and `resolve_operators()` emits descriptive "cannot resolve operator…" / "not found in manifest"
  errors.
- **07-F6** (no already-installed-transitive-dep test) — **refuted**: `test_package_manager.cpp:534-580`
  Test 11 ("Already-installed dependency is skipped") asserts `installed_deps.empty()` — exactly this path.
- **07-F7** (lane-strategy export divergence by arch) — refuted: planner inputs (lane_behavior,
  strategy_independent, channel counts, lane sets) are all arch-independent; the embedded graph is
  byte-identical. No mechanism for divergence.
- **07-F8** (no missing-lockfile strict-export test) — **refuted**: `test_export_strict.cpp:109`
  (`test_strict_without_sibling_lockfile_fails`) asserts `run()==false` + `error_kind=="no_lockfile"`.
- **07-F9** (VividPackageSupport missing webgpu/operator_api linking) — refuted as written: those targets
  **don't exist** in the standalone package cmake context, so the recommendation is impossible. (It does
  gesture at the real 07-F1 GPU-flag gap, captured there.)

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Package-manager findings are separated from CMake/export/dependency findings. *(No package-manager
  findings; both confirmed are CMake/build.)*
- [x] Reproducibility and artifact-location risks are explicitly checked. *(dependencies.cmake = single
  source ✅; artifacts in CMAKE_BINARY_DIR ✅; one minor `.gitignore`-guidance gap noted.)*
- [x] Test partition gaps include commands needed to reproduce failures.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
