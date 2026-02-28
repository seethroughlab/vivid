# Phase 22 Design: Export / Standalone Builds

Package a Vivid graph as a single self-contained binary — no loose dylibs, no build tools, no operator source tree. Two deployment scenarios: a double-clickable `.app` for sharing with collaborators, and a headless binary for LED walls, projection servers, and unattended installations.

## Motivation

Today, running a Vivid project requires the full development environment: the vivid binary, ~61 operator dylibs in the same directory, Dawn's WebGPU library, font files, and the graph JSON. That's fine for authoring but unusable for deployment. Three scenarios drive this phase:

**Gallery installation.** A finished piece runs unattended on a venue machine. It starts on boot, has no UI, and runs until power-off. The machine doesn't have Xcode or CMake. The operator should ship as a single binary with the graph baked in.

**Sharing with collaborators.** A musician wants to send a Vivid patch to a visual artist. The recipient double-clicks an `.app` and sees the piece running. No terminal, no build system, no explanation needed.

**Projection server.** A headless Linux or macOS machine drives a projector. It runs a graph over the network (optionally accepting parameter changes via the control server), but has no monitor attached to the machine itself.

## Existing Patterns in the Codebase

### Built-in operator registration (`src/runtime/builtin_operators.cpp`)

`audio_out` and `video_out` are already statically linked — no dylib, no dlopen. They register via `OperatorLoader::init_builtin()`, which takes four function pointers directly:

```cpp
registry.register_builtin("audio_out",
    audio_out_descriptor, audio_out_create, audio_out_destroy, audio_out_process);
```

This is the exact pattern the standalone build extends to all operators. The infrastructure exists; it just needs to scale from 2 operators to N.

### Deferred scanning and lazy loading (`src/runtime/operator_registry.h`)

`scan_deferred()` probes dylibs for metadata without fully loading them. `load_for_graph()` then loads only what the graph actually references. The standalone build is the static-linking analog: compile only what the graph references, link it all into one binary.

### macOS .app bundle (`docs/mac-bundle.md`)

The bundle design doc covers packaging the current dynamic-loading architecture as a `.app`. Phase 22 builds on this — same `.app` structure, but with operators statically linked instead of copied as dylibs into `Contents/MacOS/`.

### Headless mode (`src/runtime/main.cpp`)

`--headless` already exists: it sets `GLFW_VISIBLE=FALSE` so the window is created but hidden. GPU operators still run (Dawn renders to the invisible window's surface). This works for projection servers that have a GPU but no monitor.

## Design

### The `vivid export` command

A new CLI subcommand (or MCP tool) that produces a standalone binary:

```
vivid export --graph path/to/graph.json --output MyInstallation
vivid export --graph path/to/graph.json --output MyInstallation --headless
vivid export --graph path/to/graph.json --output MyInstallation --bundle
```

Flags:
- `--graph` (required) — the graph JSON to embed
- `--output` (required) — output name (binary or `.app` bundle)
- `--headless` — compile in headless mode (no UI, hidden window)
- `--bundle` — produce a macOS `.app` bundle (implies windowed mode)
- `--control-server` — include the HTTP control server (off by default)
- `--extra-operators` — comma-separated list of additional operator types to include beyond what the graph references

### The export pipeline

```
1. Parse graph JSON
2. Extract operator type names (tree-shake)
3. Map type names → source files
4. Generate static_registry.cpp (per-operator renamed symbols)
5. Compile operators as object files (with symbol renaming)
6. Compile a standalone main.cpp (embedded graph, minimal startup)
7. Link everything into one binary
8. (Optional) Package as .app bundle
```

Steps 3–7 are a single clang/CMake invocation. The export tool generates a temporary CMakeLists.txt (or a direct clang command sequence) and runs the build.

### Static linking via symbol renaming

The core challenge: every operator dylib exports the same `extern "C"` symbol names (`vivid_descriptor`, `vivid_create`, `vivid_destroy`, `vivid_process`). When statically linked, these collide.

Solution: compile each operator with `-D` flags that rename the symbols at the preprocessor level:

```
clang++ -std=c++17 -c
  -Dvivid_descriptor=vivid_descriptor_lfo
  -Dvivid_create=vivid_create_lfo
  -Dvivid_destroy=vivid_destroy_lfo
  -Dvivid_process=vivid_process_lfo
  -Dvivid_draw_thumbnail=vivid_draw_thumbnail_lfo
  -Dvivid_main_thread_update=vivid_main_thread_update_lfo
  -I <vivid_src>/src
  -o lfo.o
  operators/control/lfo/lfo.cpp
```

The `VIVID_REGISTER(LFO)` macro expands to `extern "C" void vivid_create_lfo()` instead of `extern "C" void vivid_create()` — no changes to the operator source code required.

### Generated static registry

The export tool generates a `static_registry.cpp` file listing every included operator:

```cpp
// AUTO-GENERATED by vivid export
#include "runtime/operator_registry.h"
#include "operator_api/types.h"

// Forward declarations (one set per operator)
extern "C" const VividOperatorDescriptor* vivid_descriptor_lfo();
extern "C" void* vivid_create_lfo();
extern "C" void  vivid_destroy_lfo(void*);
extern "C" void  vivid_process_lfo(void*, const VividProcessContext*);

extern "C" const VividOperatorDescriptor* vivid_descriptor_oscillator();
extern "C" void* vivid_create_oscillator();
extern "C" void  vivid_destroy_oscillator(void*);
extern "C" void  vivid_process_oscillator(void*, const VividProcessContext*);

// ... one block per operator ...

void register_static_operators(vivid::OperatorRegistry& registry) {
    registry.register_builtin("LFO",
        vivid_descriptor_lfo, vivid_create_lfo,
        vivid_destroy_lfo, vivid_process_lfo);
    registry.register_builtin("oscillator",
        vivid_descriptor_oscillator, vivid_create_oscillator,
        vivid_destroy_oscillator, vivid_process_oscillator);
    // ...
}
```

The type name (first argument to `register_builtin`) comes from the operator's descriptor — the export tool reads this from the dylib metadata that `scan_deferred()` already collected, or parses it from the source.

### Tree-shaking: graph-referenced operators only

The export pipeline:

1. Parse the graph JSON with yyjson (same parser the runtime uses)
2. Collect the set of `type` values from all nodes
3. Resolve aliases (e.g., `"HSV"` → `"WGSLFilter"` with a specific WGSL preset)
4. Map each type to its source file using the operator registry's `target_to_type_` mapping (or a build-time manifest)
5. Include only those operators in the static build

For WGSL data-driven filters, the export embeds the `.wgsl` file contents as string literals and registers them via the existing `init_data_driven()` path.

The `--extra-operators` flag allows including operators not in the graph (useful when the control server is enabled and you want to add nodes at runtime).

### Graph embedding

The graph JSON is embedded as a C++ string literal via a generated header:

```cpp
// AUTO-GENERATED by vivid export
static const char embedded_graph_json[] = R"JSON(
{
  "nodes": { ... },
  "connections": [ ... ]
}
)JSON";
```

The standalone `main()` calls `graph.load_from_string(embedded_graph_json)` instead of `graph.load(path)`. This requires adding a `Graph::load_from_string(const char* json)` method — trivial, since `load()` already parses a string buffer internally after reading the file.

The standalone binary can also accept `--graph <path>` to override the embedded graph at runtime, useful for testing.

### Standalone main.cpp

A simplified `main()` for standalone builds that strips out authoring infrastructure:

**Included:**
- GLFW window (or hidden window for headless)
- GPU context + fullscreen blit
- Audio engine
- Scheduler
- Graph loading (from embedded JSON)
- Operator registration (static, no dlopen)
- MIDI input (if graph uses it)
- Control server (if `--control-server` flag was passed to export)

**Excluded:**
- Hot-reload system (no source files to watch)
- File watcher
- Operator creator / scaffolding
- Node graph UI / inspector / patch panel / session grid
- REPL
- Text renderer (unless UI is enabled)
- Operator chooser
- Settings persistence

This can be implemented as either:
- **A separate `standalone_main.cpp`** with only the needed startup code
- **Conditional compilation in the existing `main.cpp`** via `#ifdef VIVID_STANDALONE`

The separate file is cleaner — `main.cpp` is already ~1100 lines and adding more `#ifdef` branches would hurt readability.

### Headless mode improvements

The current `--headless` creates a hidden GLFW window. For standalone headless builds, this is sufficient — GPU operators still need a Dawn surface, and a hidden window provides one without a visible screen.

For the standalone binary, headless is the default when exported with `--headless`. The binary:
- Creates a hidden GLFW window (same as today)
- Runs the graph at frame rate
- Optionally starts the control server for remote parameter control
- Runs indefinitely (Ctrl+C or SIGTERM to stop)
- No UI rendering, no text renderer, no input callbacks

### Audio-only mode (deferred follow-up)

If a graph has zero GPU operators, the Dawn/GLFW dependency is dead weight. A true audio-only mode would skip window creation entirely, allowing deployment on machines without GPUs (Raspberry Pi, Linux servers).

This is architecturally clean — the audio engine is already fully independent of the GPU pipeline — but adds conditional branching to the export pipeline and standalone main. Deferred to a follow-up within Phase 22 after the core standalone build works.

### WGSL filter handling

Data-driven WGSL filters (Phase 16b) need special treatment:
- The `.wgsl` file contents must be embedded as string literals
- The `DataDrivenFilterConfig` is built at startup from the embedded strings
- Registration uses the existing `init_data_driven()` path

For each WGSL filter the graph uses, the export generates:

```cpp
static const char wgsl_blur[] = R"WGSL(
/* { "name": "Blur", "params": [...] } */
@fragment fn main(...) { ... }
)WGSL";
```

### User-defined filters

Graphs can contain inline user-defined WGSL filters (stored in the graph JSON's `filters` array). These are already embedded in the graph JSON, so they come along for free when the graph is embedded.

## Implementation Steps

### Step 1: `Graph::load_from_string()`

Add a method that parses JSON from a string buffer instead of a file path. The existing `load()` already does `yyjson_read_file()` → parse; this just uses `yyjson_read()` instead.

**Files:**
- Modify: `src/runtime/graph.h`, `src/runtime/graph.cpp`

### Step 2: Operator source manifest

Build a mapping from operator type name → source file path. This can be:
- A JSON file generated at CMake configure time from the `add_vivid_operator()` calls
- Or parsed from `CMakeLists.txt` directly by the export tool

The manifest maps e.g. `"LFO" → "operators/control/lfo/lfo.cpp"` and includes metadata like domain, extra libs needed, and whether it has `VIVID_THUMBNAIL`.

**Files:**
- Modify: `CMakeLists.txt` (generate manifest at configure time)
- Create: `build/operator_manifest.json` (auto-generated)

### Step 3: Export tool — graph analysis + code generation

A new source file (or script) that:
1. Reads the graph JSON
2. Reads the operator manifest
3. Determines which operators to include
4. Generates `static_registry.cpp` and `embedded_graph.h`
5. Generates a standalone CMakeLists.txt (or clang command sequence)

This could be a C++ tool (reusing the existing yyjson parser) or a Python script. C++ is preferred since it can reuse `Graph::load_from_string()` and the operator manifest parser directly.

**Files:**
- Create: `src/export/vivid_export.cpp` (export tool main)
- Create: `src/export/CMakeLists.txt`

### Step 4: Standalone main

A minimal `main()` that:
- Includes the generated `static_registry.cpp` and `embedded_graph.h`
- Initializes GLFW (hidden or visible), GPU context, audio engine
- Builds scheduler from embedded graph
- Runs the main loop (render + audio tick)
- No UI, no hot-reload, no REPL

**Files:**
- Create: `src/export/standalone_main.cpp`

### Step 5: Build system integration

The export tool generates a build directory and invokes CMake/clang to produce the final binary. The standalone target links:
- Operator object files (with renamed symbols)
- Dawn/WebGPU (static or bundled dylib)
- miniaudio (header-only, already compiled in)
- GLFW (static)
- System frameworks (Cocoa, IOKit, QuartzCore, Metal, AudioToolbox)

**Files:**
- Create: `src/export/standalone.cmake.in` (template CMakeLists for standalone builds)

### Step 6: `.app` bundle packaging

When `--bundle` is passed, wrap the standalone binary in a macOS `.app` using the same structure from `docs/mac-bundle.md`, minus the loose dylibs (they're statically linked). Resources: font file (if UI enabled), embedded graph (already in binary), Dawn dylib (if dynamically linked).

**Files:**
- Reuse: `platform/macos/Info.plist.in` (from mac-bundle plan)

### Step 7: MCP tool + CLI subcommand

Expose the export pipeline as both:
- `vivid export --graph ... --output ...` CLI subcommand
- `export_standalone` MCP tool (for LLM-driven workflows)

**Files:**
- Modify: `src/cli/main.cpp` or equivalent (add `export` subcommand)
- Modify: `src/cli/mcp_server.cpp` (add `export_standalone` tool)

## Open Questions

### Build tool dependency

The export tool needs a C++ compiler (clang). On macOS, this comes with Xcode Command Line Tools — the same requirement as the current hot-reload system. Should the standalone binary require the user to have clang installed, or should the export happen on the authoring machine only?

**Recommendation:** Export happens on the authoring machine (which already has clang for hot-reload). The resulting binary is self-contained and needs no compiler.

### Dawn static vs dynamic linking

Dawn (`libwebgpu_dawn.dylib`) is currently linked dynamically. For a truly single-file binary, it would need to be linked statically. Dawn does support static builds, but it's a large library (~30MB). The alternative is to bundle the `.dylib` alongside the binary (or inside the `.app`).

**Recommendation:** Bundle the `.dylib` for now (same as current approach). Static Dawn linking can be a follow-up optimization.

### Cross-compilation

The export tool compiles for the host architecture. Cross-compilation (e.g., building an ARM64 binary on x86) is possible with clang but adds complexity.

**Recommendation:** Defer. Export for the current machine only.

## Verification

1. **Export with window**: `vivid export --graph graphs/fft_bars_demo.json --output fft_bars` → produces `fft_bars` binary. Run it — window shows FFT bars driven by oscillator. No loose dylibs needed.
2. **Export headless**: `vivid export --graph graphs/fft_bars_demo.json --output fft_bars --headless` → binary runs without visible window. Audio still plays.
3. **Export as bundle**: `vivid export --graph graphs/fft_bars_demo.json --output FftBars --bundle` → produces `FftBars.app`. Double-click in Finder — it opens and runs.
4. **Tree-shaking**: Export a graph that uses only `Clock`, `LFO`, `Noise`, `video_out`. Resulting binary should not contain `oscillator`, `gain`, `reverb`, etc. Verify with `nm` or binary size comparison.
5. **Control server**: `vivid export --graph ... --output ... --control-server` → binary starts with HTTP server on port 9876. `curl localhost:9876/inspect_graph` returns the graph JSON.
6. **Graph override**: `./my_binary --graph other_graph.json` loads the file instead of the embedded graph.
7. **WGSL filters**: Export a graph using `Blur` and `HSV` filters → filters render correctly in the standalone binary.
8. **MIDI**: Export a graph with `midi_input` → MIDI controller works in the standalone binary.
