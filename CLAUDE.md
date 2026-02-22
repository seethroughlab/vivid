# Vivid

WebGPU-based creative coding framework with hot-reload. Minimal core + optional libraries.

## Build Commands

```bash
cmake -B build && cmake --build build    # Full build
cmake --build build                       # Incremental build
./build/bin/vivid projects/getting-started/02-hello-noise    # Run project
doxygen Doxyfile                          # Generate API docs
```

## Framework Development

When working on Vivid's own C++ codebase (not user chain.cpp projects), use this workflow.

### Test Commands

```bash
ctest --test-dir build -L "unit|integration" --output-on-failure   # Fast feedback (~7s)
ctest --test-dir build -L "unit|integration|build|mcp|cli" --output-on-failure  # Full non-GPU suite
ctest --test-dir build --output-on-failure                          # Everything including GPU tests
ctest --test-dir build -R "Noise" --output-on-failure               # Single test by name
```

### Test Labels

| Label | What it covers | Speed |
|-------|---------------|-------|
| `unit` | Operator params, effects, audio analysis, particles, GUI layout, synth/network/serial modules | ~5s |
| `integration` | Chain assembly | ~2s |
| `build` | Version check, library existence | <1s |
| `mcp` | MCP JSON-RPC protocol, operator metadata | ~20s |
| `cli` | `vivid build/params/graph/docs/inspect/export/check` | ~60s |
| `smoke` | Example projects run without crashing (requires GPU) | ~30s |
| `visual` | Pixel comparison against reference images (requires GPU) | ~60s |
| `gui` | GUI visual regression with scripted interactions (requires GPU) | ~30s |
| `batch` | Batch compile all projects + generate operator coverage index | ~4min |
| `qualitative` | Assert visual/audio output correctness via `vivid check` (requires GPU) | ~3min |

### Inner Loop (Framework)

```
Edit C++ source
    → cmake --build build              — incremental build (~5-15s)
    → ctest -L "unit|integration"      — fast tests (~7s)
    → iterate or commit
```

### Before Pushing

Run the full non-GPU suite — this is what CI runs on all platforms:

```bash
ctest --test-dir build -L "unit|integration|build|mcp|cli" --output-on-failure
```

GPU-dependent tests (`smoke`, `visual`, `gui`) run in CI on Linux with xvfb but are `continue-on-error`. Run locally on macOS if changing rendering code.

### Batch Build Verification

Verify all example projects and test fixtures still compile:

```bash
ctest --test-dir build -L batch --output-on-failure
```

### Operator Coverage

After running batch build, check which projects use a specific operator:

```bash
cat build/operator-coverage.json | python3 -c "import sys,json; d=json.load(sys.stdin); print('\n'.join(d.get(sys.argv[1],[])))" Bloom
```

Use this when modifying an operator to find projects to test against.

### Qualitative Output Tests

Verify visual and audio output correctness across projects with `vivid-assertions.json`:

```bash
ctest --test-dir build -L qualitative --output-on-failure -j1
```

Projects with `vivid-assertions.json` are automatically discovered and tested. To add assertions to a new project, create a `vivid-assertions.json` file in the project directory (see `projects/telegram-test/vivid-assertions.json` for an example).

## CLI Options

```bash
./build/bin/vivid <project-path>                              # Run normally (stays open for hot-reload)
./build/bin/vivid <project-path> --exit-on-error              # Exit on compile error (agent/CI)
./build/bin/vivid <project-path> --snapshot output.png        # Capture single frame
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-11  # Capture 12 frames for GIF
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-30:5  # Every 5th frame
./build/bin/vivid <project-path> --audio-snapshot out.wav                   # Capture 1s audio
./build/bin/vivid <project-path> --audio-snapshot out.wav --audio-snapshot-duration 5  # 5s audio
./build/bin/vivid build <project-path>                         # Compile chain, report structured JSON errors
./build/bin/vivid check <project-path>                         # Run assertions, exit 0 (pass) or 1 (fail)
./build/bin/vivid check <project-path> --duration 2            # Run for 2s before evaluating assertions
./build/bin/vivid inspect <project-path>                       # Dump inspection JSON to stdout
./build/bin/vivid inspect <project-path> --per-operator        # Include per-operator texture analysis
./build/bin/vivid inspect <project-path> --duration 2 --samples 5  # Multi-sample: 5 snapshots over 2s
./build/bin/vivid inspect <project-path> --resolution 960x540  # Inspect at custom resolution
./build/bin/vivid params <project-path>                        # List all tweakable parameters as JSON
./build/bin/vivid graph <project-path>                         # Dump chain topology (operators, connections) as JSON
./build/bin/vivid export <project-path> -o out.mp4 --duration 10  # Export video (headless)
./build/bin/vivid export <project-path> -o out.mp4 --script events.json  # Export with scripted events
./build/bin/vivid docs search "bloom"                          # Search documentation
./build/bin/vivid docs recipe                                  # List all recipes
./build/bin/vivid docs recipe feedback-loop                    # Show a specific recipe
./build/bin/vivid docs example Noise                           # Code examples for an operator
./build/bin/vivid mcp                                         # Run MCP server for Claude Code
```

## Agent Workflow

Vivid is designed for autonomous LLM iteration using two nested loops.

### Inner Loop (seconds, no human needed)

The fast, autonomous cycle. Edit code, build, inspect structured data, validate assertions, repeat. The LLM reasons about JSON metrics — not pixels.

```
Edit chain.cpp
    → vivid build (or validate_chain MCP)   — MUST pass (exit 0) before continuing
    → inspect_chain (MCP) or vivid inspect  — read metrics JSON
    → vivid check                           — run assertions (exit 0 = pass)
    → iterate or done
```

**Important:** `vivid build` is the only CLI command that exits with a non-zero code on compile failure. Other commands (`vivid inspect`, `vivid check`, `vivid export`) hang indefinitely on errors. Always use `vivid build` as a gate before running them.

**Key inspection metrics and healthy ranges:**

| Metric | Field | Healthy Range |
|--------|-------|---------------|
| Brightness | `meanBrightness` | 0.2–0.8 |
| Contrast | `contrast` | 0.15–0.35 |
| Texture | `textureEntropy` | >0.5 detail, <0.2 flat |
| Edges | `edgeDensity` | 0.1–0.3 detailed, <0.05 soft |
| Sharpness | `sharpness` | >0.05 crisp, <0.01 blurred |
| Clipping | `clipBlackPct`/`clipWhitePct` | <0.05 |
| Center | `visualCenterX`/`Y` | ~0.5 centered |
| Temperature | `colorTemperature` | 0.3–0.7 neutral |
| Audio RMS | `rmsLevel` | 0.1–0.5 music, >0.9 clipping |
| Loudness | `integratedLUFS` | -23 broadcast |

For complete field tables (FrameAnalysis, TemporalAnalysis, AudioAnalysis, AudioVisualAnalysis), JSON examples, and comparison/analysis tool return formats, see `docs/INTROSPECTION-REFERENCE.md`.

**Audio evaluation**: Use `capture_audio`/`compare_audio` for before/after diffs. Use `sweep_param_audio` for parameter exploration. Export sidecar: `vivid export --audio` produces `<output>.audio-analysis.json`.

**AV reactivity**: Use `analyze_av_reactivity` (MCP) or `av.*` assertion paths. Multi-sample inspect with `--duration` includes `"audioVisual"` when audio chain is present.

**Assertions** (`vivid check`): Define in `vivid-assertions.json`. Paths: `output.*`, `audio.*`, `temporal.*`, `av.*`, `operators.<name>.metrics.*`, `operators.<name>.textureAnalysis.*`. Operators: `>`, `>=`, `<`, `<=`, `==`, `!=`, `between`, `exists`, `not_exists`. Optional: `name`, `after_frame`, `when_path`/`when_check`/`when_value` (conditional guard). Example:
```json
{"name": "brightness-ok", "path": "output.meanBrightness", "op": "between", "value": [0.2, 0.8]}
{"path": "output.contrast", "op": ">", "value": 0.15, "after_frame": 30, "message": "Contrast stabilizes after warmup"}
{"path": "audio.rmsLevel", "op": ">", "value": 0.01, "message": "Audio not silent"}
{"path": "temporal.isFrozen", "op": "==", "value": 0, "message": "Animation is running"}
{"path": "av.correlation", "op": ">", "value": 0.3, "message": "Visuals respond to audio"}
```

For the full assertion catalog (~50 examples), syntax details, and all assertable paths, see `docs/ASSERTIONS-REFERENCE.md`.

### Outer Loop (minutes, human review)

When the inner loop is satisfied, export media for subjective human review:

```
vivid build path/to/project               — gate check (must pass before export)
vivid export path/to/project -o /tmp/preview.mp4 --duration 15
    → send video to user
    → user provides subjective feedback
    → back to inner loop
```

The outer loop runs infrequently — once for every 5–20 inner loop iterations.

**Do NOT export video after every change.** The inner loop (build → inspect → check) gives you enough structured data to evaluate changes autonomously. Only export when you've iterated to a point worth showing the user. Exporting is slow — unnecessary exports waste time.

## MCP Server (Claude Code Integration)

The `vivid mcp` command runs an MCP server that Claude Code can use for live integration with Vivid.

### Setup
Add to your Claude Code MCP config (`~/.claude.json`):
```json
{
  "mcpServers": {
    "vivid": {
      "command": "/path/to/vivid",
      "args": ["mcp"]
    }
  }
}
```

### Available MCP Tools

50+ tools organized by category. For full descriptions, see `docs/MCP-TOOLS.md`.

- **Project Lifecycle**: `run_project`, `stop_project`, `create_project`, `bundle_project`, `list_templates`, `list_project_assets`
- **Build & Reload**: `validate_chain`, `get_runtime_status`, `get_compile_errors`, `wait_for_reload`
- **Introspection**: `inspect_chain` (pass `per_operator_analysis: true` for per-node texture), `get_chain_structure`, `get_live_params`, `get_frame_info`, `get_performance_stats`
- **Parameter Control**: `set_param`, `get_pending_changes`, `clear_pending_changes`, `discard_pending_changes`
- **Capture & Compare**: `capture_frame`, `capture_at_frame`, `capture_snapshot`, `capture_audio`, `sweep_param`, `sweep_param_audio`, `compare_frames`, `compare_audio`, `export_video`
- **Animation & Timing**: `advance_frames`, `reset_time`, `orbit_camera`
- **Snapshots & Presets**: `save_snapshot`, `recall_snapshot`, `list_snapshots`, `delete_snapshot`, `save_preset`, `load_preset`
- **Solo & Window**: `solo_operator`, `exit_solo`, `get_solo_state`, `get_window_state`, `set_window_mode`
- **Documentation**: `list_operators`, `get_operator`, `get_example`, `get_recipe`, `search_docs`, `list_modules`
- **Visual Analysis**: `analyze_color_harmony`, `analyze_symmetry`, `analyze_spatial_balance`, `analyze_av_reactivity`

### Starting Vivid
The MCP server queries and controls a running Vivid instance. **Start Vivid before using MCP tools:**

```bash
# Minimal output window (use with external editor)
./build/bin/vivid path/to/project

# With built-in devtools (recommended for MCP workflow)
./build/bin/vivid path/to/project --show-ui
```

The `--show-ui` flag enables built-in devtools with:
- **Node Graph** — Always visible; visual chain with live thumbnails
- **Inspector** — Auto-shows when a node is selected, auto-hides on deselection
- **Performance** (`Cmd+1`) — Real-time performance metrics (toggle)
- **Status Bar** — FPS, frame time, resolution, memory, record/snapshot

Press the backtick/tilde key (`` ` ``) to toggle devtools on/off at runtime. Works with or without `--show-ui`.

If Vivid isn't running, MCP tools return `{"connected": false}` with a helpful suggestion.

### Claude-First Workflow
1. User starts Vivid with `--show-ui` or via Vivid IDE
2. Claude connects automatically via MCP (port 9876)
3. User adjusts sliders in the Inspector (preview updates immediately)
4. Claude calls `get_pending_changes` to see what changed
5. Claude edits chain.cpp with the new values
6. Claude calls `clear_pending_changes` to confirm
7. Hot-reload applies the changes
8. **IMPORTANT**: Claude calls `get_runtime_status` to verify compilation succeeded

### Checking Compile Status (Critical!)
After editing `chain.cpp`, **you MUST check if compilation succeeded** before running any other command.

**CLI Compile Check (agent/CI workflows):**
```bash
vivid build path/to/project
# Exit 0 = success, non-zero = failure with error details
# MUST pass before running vivid inspect, vivid check, or vivid export
```

**MCP Compile Check (live instance):**
```
get_runtime_status → check compileStatus.success

# Response when successful:
{"connected": true, "compileStatus": {"success": true}}

# Response when failed:
{"connected": true, "compileStatus": {"success": false, "message": "chain.cpp:42:10: error: ..."}}
```

If compilation failed, read the error message and fix the code before proceeding.

### Introspection & Validation

The inner loop relies on structured data from `inspect_chain` (MCP) or `vivid inspect` (CLI). For complete field tables, see `docs/INTROSPECTION-REFERENCE.md`.

**Sample inspection JSON** (abbreviated):
```json
{
  "frame": 0, "time": 0.0,
  "operators": {"noise": {"scale": 4.0, "speed": 0.5}},
  "output": {"meanBrightness": 0.48, "contrast": 0.22, "textureEntropy": 0.61, "edgeDensity": 0.14}
}
```

With `--duration 2 --samples 5`, output is wrapped: `{"project", "duration", "sampleCount", "samples": [...], "temporal": {...}, "audioVisual": {...}}`. With `per_operator_analysis: true`, each texture operator includes `textureAnalysis` with all FrameAnalysis fields — useful for diagnosing where brightness/contrast drops occur.

**Multi-sample inspect**: `--duration N` sets capture window (seconds), `--samples K` distributes inspections. With `--out <dir>`, saves `inspection.json`, `snapshot_NNNN.png`, and `waveform.png` (if audio).

**Snapshot/capture modes**: `--snapshot <path.png>` captures frames and exits. `--audio-snapshot <path.wav>` captures audio. Frame specs: `5` (single), `0,5,10` (list), `0-11` (range), `0-20:2` (range+step). Multi-frame filenames: `output_0000.png`, etc.

**Playback scripts**: `--script events.json` supports `param_set`, `param_ramp`, `key_press`, `key_release`, `trigger`, `midi_note`, `midi_note_off`, `midi_cc`, `mouse_move`, `mouse_click`, `snapshot_recall`. See `docs/MCP-TOOLS.md` for field details.

### Validation Workflow

Use the inner loop for autonomous iteration:

1. **Start/edit**: `run_project` (MCP) or launch CLI, then edit chain.cpp
2. **Verify compile**: `vivid build` (CLI) or `get_runtime_status` (MCP) — fix errors before proceeding
3. **Inspect**: `inspect_chain` for structured metrics. Use `capture_frame`/`capture_audio` for output files
4. **Compare**: `compare_frames`/`compare_audio` to measure effect of changes. `compare_frames` includes semantic diffs (`brightness_diff`, `entropy_diff`, `sharpness_diff`, etc.)
5. **Analyze**: `analyze_color_harmony`, `analyze_symmetry`, `analyze_spatial_balance` on any PNG. `analyze_av_reactivity` on running audio-reactive projects
6. **Debug**: `solo_operator` to isolate nodes. `set_param` to test values before committing to code. `sweep_param`/`sweep_param_audio` to explore parameter space
7. **Iterate**: If metrics are off or assertions fail, go back to step 1

**Monitor for user adjustments**: Periodically check `get_pending_changes` — if the user adjusted sliders in the visualizer, ask "I see you changed X to Y. Would you like me to update chain.cpp with these values?"

**Export for human review** (outer loop): When satisfied with metrics, use `export_video` to produce a video for the user to review subjectively.

## User Modules

Third-party modules extend Vivid with additional operators. See `docs/MODULES.md` for full documentation.

```bash
# Install from GitHub
vivid modules install https://github.com/seethroughlab/vivid-onnx

# List installed modules
vivid modules list

# For development: link a local module
vivid modules link ~/Developer/vivid-example

# Unlink when done
vivid modules unlink vivid-example
```

Modules are stored in `~/.vivid/modules/`. Linked modules point to the original location.

## Project Structure

```
src/
  vivid-core/         Runtime engine (required)
  cli/                Command-line interface and app
modules/               Optional libraries (all ship with Vivid):
  vivid-audio/        Audio input, FFT, oscillators
  vivid-video/        Video playback (HAP, AVFoundation)
  vivid-render3d/     3D rendering with PBR, GLTF, CSG
  vivid-network/      OSC, UDP, WebSocket
  vivid-serial/       Serial port, DMX
  vivid-midi/         MIDI input/output
  vivid-imgui/        Dear ImGui integration (UI addon template)
  vivid-opencv/       OpenCV integration (stub template)
projects/           Runnable example projects (each with own assets/ folder)
docs/               RECIPES.md, CREATING-OPERATORS.md
tests/              Automated tests, fixtures, and test assets (Git LFS)
dev/                Developer tools and planning docs
~/.vivid/modules/      User-installed third-party libraries
```

All libraries in `modules/` use `module.json` for metadata.

## Key Patterns

### Operator Pattern
All operators inherit from `Operator` or `TextureOperator`:
```cpp
class MyEffect : public TextureOperator {
    void init(Context& ctx) override;     // Called once
    void process(Context& ctx) override;  // Called every frame
    void cleanup() override;              // Called on destruction
    std::string name() const override;    // Display name
};
```

### Setter Pattern
Use void setters for configuration:
```cpp
auto& noise = chain.add<Noise>("noise");
noise.scale = 4.0f;
noise.speed = 0.5f;
noise.octaves = 4;
```

### Parameter System
Use `Param<T>` wrapper and implement param methods:
```cpp
Param<float> m_scale{"scale", 1.0f, 0.0f, 10.0f};  // name, default, min, max

std::vector<ParamDecl> params() override { return { m_scale.decl() }; }
bool getParam(const std::string& name, float out[4]) override;
bool setParam(const std::string& name, const float value[4]) override;
```

### Chain Entry Point
User chains use the VIVID_CHAIN macro:
```cpp
void setup(Context& ctx) { /* add operators */ }
void update(Context& ctx) { ctx.chain().process(); }
VIVID_CHAIN(setup, update)
```

For custom window configuration, use `VIVID_CHAIN_CONFIG`:
```cpp
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = false
}))
```

## Code Conventions

- **C++17** required
- **WebGPU** (wgpu-native) for all GPU operations
- **WGSL** for shaders
- **Platform code**: macOS (.mm files), Windows (special DLL handling), Linux (stubs)
- **Param<T> casting**: Use explicit `static_cast<float>()` when passing to std:: functions
  ```cpp
  // Wrong: std::max(0.0f, m_inputA)  -- template deduction fails
  // Right: std::max(0.0f, static_cast<float>(m_inputA))
  ```

## Common File Locations

| Task | File |
|------|------|
| Add new 2D effect | `modules/vivid-core/include/vivid/effects/` |
| Add network operator | `modules/vivid-network/include/vivid/network/` |
| Modify chain visualizer UI | `modules/vivid-core/src/chain_visualizer.cpp` |
| Hot-reload logic | `modules/vivid-core/src/hot_reload.cpp` |
| Main runtime loop | `modules/vivid-core/src/main.cpp` |
| Operator base class | `modules/vivid-core/include/vivid/operator.h` |

## Video Audio Modes

When using `VideoPlayer` with audio, there are two modes:

### Internal Audio (recommended for playback)
```cpp
video.setInternalAudioEnabled(true);  // Default
```
- Audio handled directly by AVPlayer (macOS) / platform decoder
- **Perfect A/V sync** - audio and video are synchronized by the native player
- No audio processing or effects available
- Use this when you just want to play a video with sound

### Chain Audio (for effects/processing)
```cpp
video.setInternalAudioEnabled(false);

auto& videoAudio = chain.add<VideoAudio>("videoAudio");
videoAudio.setSource("video");  // Extract audio from VideoPlayer

auto& delay = chain.add<Delay>("delay");
delay.input("videoAudio");

auto& output = chain.add<AudioOutput>("out");
output.setInput("delay");
chain.audioOutput("out");
```
- Audio extracted and routed through Vivid's audio chain
- Enables audio effects (delay, reverb, gain, etc.)
- **May have A/V sync issues** due to processing latency
- Use this when you need audio-reactive visuals or audio effects

See `modules/vivid-video/examples/video-audio/` for a complete example with both modes.

## External Modules

Some modules have been moved to separate repositories for independent versioning and CI:

- **vivid-opencv**: https://github.com/seethroughlab/vivid-opencv
  - Computer vision operators (Contours, OpticalFlow, BlobTrack)
  - Builds OpenCV from source to avoid MSVC STL ABI issues on Windows

- **vivid-onnx**: https://github.com/seethroughlab/vivid-onnx
  - Machine learning inference (PoseDetector, FaceDetector)
  - Uses ONNX Runtime

Install external modules with: `vivid modules install <repo-url>`

## Documentation

- `docs/RECIPES.md` - Complete chain.cpp examples
- `docs/ROADMAP.md` - Architecture decisions and development history
- `docs/INTROSPECTION-REFERENCE.md` - Field tables for FrameAnalysis, TemporalAnalysis, AudioAnalysis, AudioVisualAnalysis + JSON examples
- `docs/ASSERTIONS-REFERENCE.md` - Full assertion catalog (~50 examples), syntax, assertable paths
- `docs/MCP-TOOLS.md` - Complete MCP tool catalog, playback script events, capture/inspect details
