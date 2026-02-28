# Community Operator Package System + Core Simplification

## Context

The PRD now explicitly states: "Vivid's value is the environment, not the operators." The core ships ~78 operators, many of which are standard DSP/effects that an LLM generates confidently. We want to:
1. Remove ~17 operators from core into installable community packages
2. Build a lightweight package system so users can `install_package` them back with one command
3. Establish the pattern for community-contributed operators

## What Gets Extracted

**vivid-drums** (7 operators):
- `drum_kick`, `drum_snare`, `drum_hihat`, `drum_clap`, `drum_tom`, `drum_cymbal`
- `drum_sequencer`
- Shared header: `drum_common/drum_dsp.h`

**vivid-glitch** (6 operators):
- `stutter`, `tape_stop`, `beat_repeat`, `reverse`, `scratch`, `glitch`
- Shared header: `glitch_common/glitch_dsp.h`

**vivid-effects** (4 operators):
- `bitcrush`, `distortion`, `freq_shift`, `stretch`

**Total: 17 operators removed from core, 61 remain.**

## What Stays in Core

- **Control (14):** clock, lfo, math, envelope, midi_input, fft_analysis, note_pattern, chord_progression, arpeggiator, logic, gate, random, smooth, sequencer
- **Pattern algebra (5):** euclidean, pattern_seq, stack, alternate, pat_transform
- **Audio (6):** oscillator, gain, wavetable_synth, reverb, delay, spread_adsr, spread_lfo
- **GPU (8+):** noise, shape, bars, composite, bloom, feedback, instance, time_machine, movie_file_in, movie_file_audio_in
- **Built-in (2):** audio_out, video_out
- **WGSL filters (19):** all stay (data-driven, zero binary cost, great LLM examples)

## Package System Design

### Package structure on disk

```
~/.vivid/packages/vivid-drums/
  vivid-package.json
  operators/
    audio/
      drum_kick/drum_kick.cpp
      drum_snare/drum_snare.cpp
      ...
      drum_common/drum_dsp.h
    control/
      drum_sequencer/drum_sequencer.cpp
  build/                          # auto-generated compiled dylibs
    drum_kick.dylib
    drum_snare.dylib
    ...
```

Directory layout mirrors the core `operators/` structure so relative `#include` paths work unchanged.

### Manifest: `vivid-package.json`

```json
{
  "name": "vivid-drums",
  "version": "1.0.0",
  "description": "808-style drum synthesis operators",
  "operators": [
    "audio/drum_kick",
    "audio/drum_snare",
    "audio/drum_hihat",
    "audio/drum_clap",
    "audio/drum_tom",
    "audio/drum_cymbal",
    "control/drum_sequencer"
  ],
  "gpu_operators": []
}
```

- Each entry in `operators` is `<domain>/<name>`, expects `operators/<domain>/<name>/<name>.cpp`
- `gpu_operators` lists operators needing WebGPU linkage (separate because they need extra flags)
- No dependency resolution between packages (v1 simplification)

### Compilation: direct clang invocation

Control/audio operators:
```
clang++ -std=c++17 -shared -fPIC -O2
  -I <vivid_src>/src                       # operator_api headers
  -I <package>/operators/<domain>          # for relative includes (drum_dsp.h etc)
  -o <package>/build/<name>.dylib
  <package>/operators/<domain>/<name>/<name>.cpp
```

GPU operators add:
```
  -I <build_dir>/_deps/webgpu-src/include  # webgpu.h
  -framework Cocoa -framework IOKit -framework QuartzCore  # macOS Dawn deps
  <build_dir>/_deps/webgpu-build/libwebgpu_dawn.dylib      # link Dawn
```

Why not CMake: operators are single .cpp files. CMake adds unnecessary ceremony for this simple case. The hot-reloader can use this same direct compilation path.

### Discovery on startup

Extend `main.cpp` startup sequence (after existing scans):

```
registry.scan_deferred(exe_dir)           // existing: core operators
registry.scan_wgsl_presets(filters_dir)   // existing: WGSL filters
registry.scan_packages(packages_dir)      // NEW: ~/.vivid/packages/*/build/
```

`scan_packages()` iterates package directories, checks for `vivid-package.json`, reads manifests, and calls `scan_deferred()` on each package's `build/` directory. Also scans `filters/` subdirectories for package-provided WGSL filters.

### Installation via MCP

Three new MCP tools (backed by HTTP endpoints on ControlServer):

- **`install_package`** `{url: "https://github.com/vivid-project/vivid-drums"}` -- git clone into `~/.vivid/packages/<name>/`, compile all operators, scan into registry
- **`uninstall_package`** `{name: "vivid-drums"}` -- remove directory
- **`list_packages`** -- list installed packages with their operators

Install flow:
1. Parse URL, derive package name from last path segment
2. `git clone --depth 1 <url> ~/.vivid/packages/<name>/`
3. Read and validate `vivid-package.json`
4. Compile each operator (via PackageCompiler)
5. `scan_deferred()` on the new `build/` directory
6. Operators immediately available in the chooser (no restart needed)

### Hot-reload for package operators

Extend `FileWatcher` to also watch `~/.vivid/packages/*/operators/`. On change, route to `PackageCompiler` instead of `cmake --build`. Same staging + dlclose/dlopen flow as core hot-reload.

## Implementation Steps

### Step 1: Promote `audio_dsp.h` to operator_api

- Move `operators/audio/audio_dsp.h` -> `src/operator_api/audio_dsp.h`
- Update includes in: `oscillator.cpp`, `spread_lfo.cpp`, `freq_shift.cpp`, `drum_dsp.h`, `glitch_dsp.h`
- From `#include "../audio_dsp.h"` to `#include "operator_api/audio_dsp.h"`
- This makes the header available to both core and package operators via the standard include path

**Files:** `src/operator_api/audio_dsp.h` (new), `operators/audio/audio_dsp.h` (delete), 5 files updating includes

### Step 2: Build PackageCompiler

New files: `src/runtime/package_compiler.h`, `src/runtime/package_compiler.cpp`

- `compile_operator(package_dir, operator_path, needs_gpu)` -> CompileResult
- Resolves include paths from known locations (vivid src dir, webgpu deps dir)
- Spawns clang++ subprocess, captures output
- Writes dylib to `<package_dir>/build/`
- `compile_all(package_dir)` reads manifest and compiles everything

**Files:** `src/runtime/package_compiler.h` (new), `src/runtime/package_compiler.cpp` (new), `src/cli/CMakeLists.txt` (add to source list)

### Step 3: Build PackageManager

New files: `src/runtime/package_manager.h`, `src/runtime/package_manager.cpp`

- `install(url)` -> git clone + compile_all + scan
- `uninstall(name)` -> remove directory
- `list()` -> installed packages with metadata
- `get_packages_dir()` -> `~/.vivid/packages/` (create if needed)
- Reads/validates `vivid-package.json` manifests

**Files:** `src/runtime/package_manager.h` (new), `src/runtime/package_manager.cpp` (new)

### Step 4: Extend OperatorRegistry with scan_packages()

- New method `scan_packages(const std::string& packages_dir)`
- Walks `<packages_dir>/*/build/` directories
- Calls existing `scan_deferred()` on each
- Tracks package provenance (which package each operator came from)

**Files:** `src/runtime/operator_registry.h`, `src/runtime/operator_registry.cpp`

### Step 5: Extend startup sequence

- After existing `scan_deferred()` and `scan_wgsl_presets()`, call `registry.scan_packages()`
- Pass packages_dir derived from platform config dir

**Files:** `src/runtime/main.cpp`

### Step 6: Add MCP tools + HTTP endpoints

Three new control server handlers:
- `POST /install_package` -> PackageManager::install()
- `POST /uninstall_package` -> PackageManager::uninstall()
- `GET /list_packages` -> PackageManager::list()

Three new MCP tool definitions in vivid_mcp.py.

**Files:** `src/runtime/control_server.cpp`, `mcp/vivid_mcp.py`

### Step 7: Extend FileWatcher + HotReloader for packages

- FileWatcher watches `~/.vivid/packages/*/operators/` directories
- HotReloader routes package operator changes to PackageCompiler instead of cmake
- Same staging + reload flow

**Files:** `src/runtime/file_watcher.h/cpp`, `src/runtime/hot_reload.h/cpp`

### Step 8: Create package repos and extract operators

For each package (vivid-drums, vivid-glitch, vivid-effects):
1. Create directory with `vivid-package.json` manifest
2. Move operator source files from `operators/` maintaining directory structure
3. Include shared headers (drum_dsp.h, glitch_dsp.h)
4. Update shared header includes from `"../audio_dsp.h"` to `"operator_api/audio_dsp.h"`

### Step 9: Remove extracted operators from core build

- Remove `add_vivid_operator()` calls from `CMakeLists.txt` for all 17 operators
- Remove operator source directories from `operators/`
- Update any demo graphs that reference extracted operators (move to packages or update docs)
- Remove associated tests or move them to packages

**Files:** `CMakeLists.txt`, `operators/audio/drum_*/` (remove), `operators/audio/glitch_common/` (remove), `operators/audio/stutter/` etc (remove), `tests/` (update)

## Verification

1. **Build succeeds** after removing 17 operators from CMakeLists.txt
2. **Existing tests pass** (minus tests for extracted operators)
3. **Package install works**: `install_package` via MCP -> operators appear in registry
4. **Package operators load in graphs**: load a graph using drum_kick -> works after installing vivid-drums
5. **Hot-reload works for package operators**: edit a package operator source -> recompiles and reloads
6. **Uninstall works**: `uninstall_package vivid-drums` -> operators gone from registry
7. **Demo graphs**: North Star demo and other core demos still work (they don't use extracted operators)

## Deferred (not in this plan)

- CLI subcommands (`vivid install` from terminal -- currently MCP-only)
- Package catalog / search registry
- Dependency resolution between packages
- Version pinning / lockfiles
- Windows/Linux compiler support in PackageCompiler (macOS first)
- Package-provided demo graphs appearing in app UI
- "Missing operator" suggestions ("DrumKick is available in vivid-drums")
