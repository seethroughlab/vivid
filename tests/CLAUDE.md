# Tests

## Purpose

Integration and unit tests for the Vivid runtime, operators, and tooling. The directory structure mirrors the source layout, and tests are partitioned in cmake by resource requirements.

## Directory Structure

| Directory | Tests For |
|-----------|-----------|
| `core/` | Runtime core: hot-reload, graph compilation, bootstrap, settings |
| `control/` | Control-domain operators and RuntimeAPI commands |
| `audio/` | Audio engine, audio-cadence operators, audio hot-reload |
| `graph/` | Graph compiler, frame/audio executors, lane state |
| `lanes/` | Lane execution strategies, lane state identity, multi-lane audio |
| `operators/` | Individual operator unit tests |
| `ops/` | Operator integration tests (stability across cadences, domain cross-checks) |
| `packages/` | Package discovery, compilation, manifest parsing, test runner |
| `gpu/` | GPU context, shader parsing, texture operations |
| `ui/` | UI integration (live graph updates, snapshot sync) |
| `integration/` | Full-stack integration tests |
| `assets/` | Asset library discovery and metadata |
| `media/` | Export pipeline, media operations |
| `common/` | Shared test utilities |
| `fixtures/` | Test graphs (`.json`) and temporary assets used by tests |

## Test Partitions

Tests are grouped in cmake by resource requirements so CI can run subsets efficiently:

| Partition | File | Scope |
|-----------|------|-------|
| 10 | `cmake/tests/10-runtime-control-graph.cmake` | Core runtime, control server, graph compiler — no GPU, no audio, no window |
| 20 | `cmake/tests/20-ui-and-common.cmake` | UI integration, platform utilities |
| 30 | `cmake/tests/30-ops-stability-domains.cmake` | Operator stability across cadences |
| 40 | `cmake/tests/40-packages-media-misc.cmake` | Package scanning, export, asset library |
| 50 | `cmake/tests/50-assets.cmake` | Asset discovery and metadata |

## Conventions

- Tests link against `vivid_runtime_testlib` (a static library with runtime components, no window)
- Test operators are compiled as fixture dylibs (e.g., `test_op_v1`, `audio_reload_v1`)
- `test_helpers.h` provides: `ScopedTempDir`, `ScopedEnvVar`, `find_free_loopback_port()`, `check()` assertion macro
- Test naming: `test_*.cpp` files, one test executable per file
- Running a specific test: `cmake --build build --target test_<name> && ./build/test_<name>`

## See Also

- `docs/testing/README.md` — test strategy overview
- `docs/testing/INNER-OUTER-LOOP-TEST-PLAN.md` — inner/outer loop testing philosophy
- `cmake/tests.cmake` — test library definition and partition includes
