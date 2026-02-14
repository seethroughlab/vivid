# Vivid

WebGPU-based creative coding framework with hot-reload. Minimal core + optional libraries.

## Build Commands

```bash
cmake -B build && cmake --build build    # Full build
cmake --build build                       # Incremental build
./build/bin/vivid projects/getting-started/02-hello-noise    # Run project
doxygen Doxyfile                          # Generate API docs
```

## CLI Options

```bash
./build/bin/vivid <project-path>                              # Run normally
./build/bin/vivid <project-path> --snapshot output.png        # Capture single frame
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-11  # Capture 12 frames for GIF
./build/bin/vivid <project-path> --snapshot out.png --snapshot-frame 0-30:5  # Every 5th frame
./build/bin/vivid build <project-path>                         # Compile chain, report structured JSON errors
./build/bin/vivid check <project-path>                         # Run assertions, exit 0 (pass) or 1 (fail)
./build/bin/vivid inspect <project-path>                       # Dump inspection JSON to stdout
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
| Tool | Description |
|------|-------------|
| `get_runtime_status` | Get compile errors, runtime status, and operator list |
| `get_live_params` | Get real-time parameter values from running Vivid |
| `get_pending_changes` | Get slider changes waiting to be applied to chain.cpp |
| `clear_pending_changes` | Confirm changes were applied (call after editing code) |
| `discard_pending_changes` | Revert parameters to original values |
| `capture_frame` | Capture current frame to PNG from running instance |
| `set_param` | Set parameter on running operator immediately |
| `advance_frames` | Advance simulation by N frames |
| `orbit_camera` | Position camera around a target point |
| `capture_at_frame` | Advance to frame N and capture snapshot |
| `sweep_param` | Sweep a parameter across values, capturing frames at each step |
| `compare_frames` | Compare two PNG images (RMSE, per-channel diff, changed pixels) |
| `list_operators` | List all available operators (from registry) |
| `get_operator` | Get details for a specific operator |
| `search_docs` | Search Vivid documentation |

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
After editing `chain.cpp`, Vivid hot-reloads automatically. **You MUST check if compilation succeeded:**
```
# Use MCP tool:
get_runtime_status → check compileStatus.success

# Response when successful:
{"connected": true, "compileStatus": {"success": true}}

# Response when failed:
{"connected": true, "compileStatus": {"success": false, "message": "chain.cpp:42:10: error: ..."}}
```

If `compileStatus.success` is `false`, read the error message and fix the code before proceeding.

### Snapshot Mode (for CI/Testing)
The `--snapshot` flag runs the chain for a few frames, saves a PNG, and exits. Useful for:
- **Automated testing**: Verify visual output hasn't regressed
- **AI evaluation**: Claude can run chains and inspect the output
- **CI pipelines**: Generate thumbnails or verify examples compile and run
- **GIF creation**: Capture multiple frames for animation

Options:
- `--snapshot <path.png>` - Output path for the snapshot
- `--snapshot-frame <spec>` - Frame(s) to capture (default: 5)

Frame specification formats:
- `5` - Single frame (backwards compatible)
- `0,5,10,15` - Specific frames (comma-separated)
- `0-11` - Range (frames 0 through 11, inclusive)
- `0-20:2` - Range with step (frames 0, 2, 4, ..., 20)

When capturing multiple frames, filenames include frame numbers: `output.png` becomes `output_0000.png`, `output_0001.png`, etc.

### Visual Validation Workflow

When working on a Vivid project, Claude should run the project and use MCP tools for visual feedback:

1. **Start the project**: When the user wants to see their work, ask "Would you like me to run your project?" then use `run_project`
2. **Keep it running**: The visualizer stays open so you can capture frames and the user can adjust sliders
3. **Validate changes**: After code edits, use `capture_frame` to verify the visual output
4. **Explore the scene**: Use `orbit_camera` and `set_param` to view from different angles or test parameter values
5. **Monitor for user adjustments**: Periodically check `get_pending_changes` - if the user adjusted sliders in the visualizer, ask "I see you changed X to Y. Would you like me to update chain.cpp with these values?"
6. **Stop when done**: Use `stop_project` or let the user close the window

**Key MCP tools for visual work:**
- `capture_frame` - Capture current frame to PNG
- `capture_at_frame` - Advance to frame N and capture (for animations)
- `set_param` - Adjust parameters in real-time
- `orbit_camera` - Reposition camera view
- `advance_frames` - Progress animation forward

**Note:** CLI snapshot mode (`--snapshot` flag) is for CI pipelines and automated testing only - not part of the normal Claude workflow.

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
