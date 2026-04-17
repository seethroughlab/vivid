# Build System

## Purpose

CMake modules that define the application target, operator plugin compilation, test infrastructure, and third-party dependency management.

## Key Files

| File | Role |
|------|------|
| `app.cmake` | Main `vivid` executable target, UI library, core framework linking |
| `operators.cmake` | `add_vivid_operator()` macro — builds each operator as a shared library with the right includes, links, and optional factory preset copying |
| `tests.cmake` | `vivid_runtime_testlib` (static test library), fixture operator targets, includes test partition files |
| `dependencies.cmake` | Third-party dependency versions, FetchContent declarations, system framework linking |
| `tests/10-runtime-control-graph.cmake` | Test partition: core runtime, no GPU/audio/window |
| `tests/20-ui-and-common.cmake` | Test partition: UI integration, platform utilities |
| `tests/30-ops-stability-domains.cmake` | Test partition: operator stability across cadences |
| `tests/40-packages-media-misc.cmake` | Test partition: packages, export, assets |
| `tests/50-assets.cmake` | Test partition: asset discovery and metadata |
| `tests/90-production-gate.cmake` | Tiered `production_gate*` targets (core/gui/env/soak) wrapping the release-critical labels; chains `tools/production_gate_report.py` to emit `build/reports/production-gate.json` |

The production-gate self-test (`test_production_gate_report`) runs the report tool's pytest suite via `uv` — install with `brew install uv` if missing locally; CI already has it.

## How It's Organized

### Operator Compilation

`add_vivid_operator(name source ...)` creates a `MODULE` library target that:
- Links against the operator API headers
- Sets the output directory to `CMAKE_BINARY_DIR` for hot-reload discovery
- Accepts `FACTORY_PRESETS` to copy preset JSON alongside the dylib
- Accepts `EXTRA_LIBS` for additional dependencies (e.g., `webgpu`)
- Accepts `COMPOSABLE_SUPPORT` for operators embeddable via `ChildOp<T>`

Dual-cadence operators register two targets (e.g., `lfo_fr` and `lfo_au`) with shared source files added via `target_sources()`.

### Test Partitioning

Tests are split across numbered partition files so CI can run subsets based on available resources. Partition 10 (no GPU, no audio, no window) runs everywhere; higher partitions may require a display server or audio device.

## See Also

- `AGENTS.md` §Building — build commands and workflow
- `tests/CLAUDE.md` — test directory structure and conventions
