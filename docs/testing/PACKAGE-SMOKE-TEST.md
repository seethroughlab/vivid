# Package Smoke Test Protocol

This document describes how an external vivid package repo should set up CI smoke tests using vivid-core's `test_demo_graphs` binary.

## Why This Approach

`test_demo_graphs` is a general-purpose smoke test runner built into vivid-core. It:

- Accepts a `<graphs_dir>` argument pointing to any directory of `.json` graph files
- Scans its **current working directory** for operator `.dylib` plugin files (via `registry.scan_deferred(".")`)
- Loads each graph, ticks it for several frames, and validates that no crashes, WebGPU validation errors, or `warn`/`error` log lines occurred

Because the binary is general-purpose, packages do **not** need their own test infrastructure. They simply:

1. Build vivid-core (specifically `test_demo_graphs`)
2. Build their own operators
3. Copy operator `.dylib` files into vivid's build directory
4. Run `test_demo_graphs` from that directory, pointing at the package's `graphs/`

This keeps package repos thin and ensures smoke tests always run against the same vivid-core test harness.

## What Gets Checked

- **Crash-free execution**: every graph loads and ticks without crashing
- **No WebGPU validation errors**: the Dawn/wgpu validation layer is active; any GPU API misuse is fatal
- **No warn/error log output**: the test harness captures stderr and fails if it sees `[warn]` or `[error]` lines

## GPU-Optional Graphs

Graphs that require a GPU are automatically skipped if no GPU adapter is available on the runner (e.g., a headless CI machine without a GPU). Audio-only and control-only graphs always run.

## Protocol Steps

1. **Clone vivid-core** into a sibling directory (`../vivid` relative to the package)
2. **Configure vivid-core** with CMake
3. **Create the app bundle skeleton** — vivid expects `vivid.app/Contents/PlugIns/` to exist in the build dir (needed for operator loader initialization)
4. **Build `test_demo_graphs` and vivid core operators** — the `operators` meta-target builds all vivid operator plugins so graphs that use core operators (e.g. `Text`, `WavetableSynth`) can be loaded
5. **Configure the package** — pass `VIVID_SRC_DIR` and `VIVID_BUILD_DIR` so the package CMakeLists finds headers and WebGPU
6. **Build the package operators** — produces `.dylib` files in the package's `build/` directory
7. **Copy `.dylib` files** into vivid's build directory (the CWD for `test_demo_graphs`)
8. **Run `test_demo_graphs`** from vivid's build directory, passing the package's `graphs/` as the argument

## CI Workflow Template

Use this as `.github/workflows/smoke.yml` in your package repo:

```yaml
name: Smoke Tests

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  smoke:
    runs-on: [self-hosted, macOS]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Clone vivid-core
        run: |
          rm -rf ${{ github.workspace }}/../vivid
          git clone --recurse-submodules https://github.com/seethroughlab/vivid.git ${{ github.workspace }}/../vivid

      - name: Configure vivid-core
        run: cmake -B ${{ github.workspace }}/../vivid/build -S ${{ github.workspace }}/../vivid -DCMAKE_BUILD_TYPE=RelWithDebInfo

      - name: Create app bundle skeleton
        run: mkdir -p ${{ github.workspace }}/../vivid/build/vivid.app/Contents/PlugIns

      - name: Build test_demo_graphs and vivid core operators
        run: cmake --build ${{ github.workspace }}/../vivid/build --target test_demo_graphs operators -j$(sysctl -n hw.logicalcpu)

      - name: Configure package
        run: cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVIVID_SRC_DIR=${{ github.workspace }}/../vivid -DVIVID_BUILD_DIR=${{ github.workspace }}/../vivid/build

      - name: Build operators
        run: cmake --build build -j$(sysctl -n hw.logicalcpu)

      - name: Copy operators to vivid build dir
        run: cp build/*.dylib ${{ github.workspace }}/../vivid/build/

      - name: Run smoke tests
        run: cd ${{ github.workspace }}/../vivid/build && ./test_demo_graphs ${{ github.workspace }}/graphs
```

## Package CMakeLists.txt Requirements

Your package's `CMakeLists.txt` must:

- Accept `VIVID_SRC_DIR` and `VIVID_BUILD_DIR` cache variables (with `../vivid` defaults)
- Define a `vivid_operator_api` INTERFACE library that includes `${VIVID_SRC_DIR}/src` and `${VIVID_SRC_DIR}/deps/glfw/deps`
- Build each operator as a `MODULE` library with `PREFIX ""` and `SUFFIX ".dylib"` (or platform equivalent)
- For GPU operators, discover and link `wgpu_native` from vivid's build tree

See `vivid-3d/CMakeLists.txt` for a complete reference implementation.

## Adding New Graphs

Drop `.json` graph files into your package's `graphs/` directory. They will be picked up automatically by `test_demo_graphs`. No changes to CMake or CI are needed.
