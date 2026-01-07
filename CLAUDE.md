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
| `get_pending_changes` | Get slider changes waiting to be applied to chain.cpp |
| `get_live_params` | Get real-time parameter values from running Vivid |
| `clear_pending_changes` | Confirm changes were applied (call after editing code) |
| `discard_pending_changes` | Revert parameters to original values |
| `get_runtime_status` | Get compile errors and runtime status |
| `list_operators` | List all available operators with parameters |
| `get_operator` | Get details for a specific operator |
| `search_docs` | Search Vivid documentation |

### Claude-First Workflow
1. Claude starts Vivid: `./build/bin/vivid <project>`
2. User adjusts sliders in visualizer (preview updates immediately)
3. Claude calls `get_pending_changes` to see what changed
4. Claude edits chain.cpp with the new values
5. Claude calls `clear_pending_changes` to confirm
6. Hot-reload applies the changes

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

## Documentation

- `docs/RECIPES.md` - Complete chain.cpp examples
- `docs/ROADMAP.md` - Architecture decisions and development history
