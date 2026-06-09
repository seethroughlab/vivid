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

Each operator dir produces exactly one cmake target whose name matches the dir name (e.g. `drum_sequencer/` → target `drum_sequencer`). Operators that are embedded into other operators via `ChildOp<T>` (currently `lfo`, `envelope`, `smooth`) split out a `<op>_embeddable.cpp` that's linked into both the operator's dylib and the `vivid_embeddable_op_support` static library so ChildOp consumers can resolve the operator's out-of-line virtuals.

### Test Partitioning

Tests are split across numbered partition files so CI can run subsets based on available resources. Partition 10 (no GPU, no audio, no window) runs everywhere; higher partitions may require a display server or audio device.

### Performance benchmarks

`tests/benchmarks/bench_*` are standalone chrono-timed executables — **NOT** `add_test`'d, because perf is machine-sensitive and would flake the default `ctest` run. Run them manually.

The value-model graph perf gate (lane-value Phase 8d): `bench_value_graphs` times the scalar / many-valued-frame / audio-lifted / bridge-heavy graph shapes and emits JSON. The opt-in regression gate is:

```
uv run tools/bench_regression.py [build_dir]   # default build_dir = ./build
```

It compares against `tests/benchmarks/value_graphs_baseline.json` and exits non-zero only if `scalar_us` regresses > 15% **and** by > 0.3 µs (the absolute floor keeps sub-microsecond jitter from false-failing). The baseline is **machine-specific** (captured on the dev machine); refresh on a new machine with `./build/bench_value_graphs "$PWD/build" > tests/benchmarks/value_graphs_baseline.json`.

## See Also

- `AGENTS.md` §Building — build commands and workflow
- `tests/CLAUDE.md` — test directory structure and conventions
