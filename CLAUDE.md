# Vivid

WebGPU-based creative coding framework with hot-reload. Minimal core (~600 lines) + modular addons.

## Build Commands

```bash
cmake -B build && cmake --build build    # Full build
cmake --build build                       # Incremental build
./build/bin/vivid examples/getting-started/02-hello-noise    # Run example
doxygen Doxyfile                          # Generate API docs
```

## CLI Options

```bash
./build/bin/vivid <project-path>                              # Run normally
./build/bin/vivid <project-path> --snapshot output.png        # Capture frame and exit
./build/bin/vivid <project-path> --snapshot=output.png        # Alternative syntax
./build/bin/vivid <project-path> --snapshot output.png --snapshot-frame 10  # Wait 10 frames
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

Options:
- `--snapshot <path.png>` - Output path for the snapshot
- `--snapshot-frame <N>` - Wait N frames before capture (default: 5, allows warm-up)

## Project Structure

```
core/           Runtime engine + integrated features:
                - context, chain, hot-reload, ImGui visualizer
                - io/ (image loading via stb)
                - effects/ (25+ 2D texture operators)
                - network/ (OSC, UDP, WebSocket)
addons/         Optional modular features:
  vivid-video/        Video playback (HAP, platform codecs)
  vivid-render3d/     3D rendering (PBR, CSG, instancing)
  vivid-audio/        Audio synthesis, sequencing, analysis
examples/       Curated user examples (getting-started/, 2d-effects/, audio/, 3d-rendering/)
testing-fixtures/  Test examples for CI/regression testing
docs/           LLM-REFERENCE.md, RECIPES.md, OPERATOR-API.md
```

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
| Add new 2D effect | `core/include/vivid/effects/` |
| Add network operator | `core/include/vivid/network/` |
| Modify chain visualizer UI | `core/imgui/chain_visualizer.cpp` |
| Hot-reload logic | `core/src/hot_reload.cpp` |
| Main runtime loop | `core/src/main.cpp` |
| Operator base class | `core/include/vivid/operator.h` |

## Documentation

- `docs/LLM-REFERENCE.md` - Compact operator reference (optimized for LLM context)
- `docs/RECIPES.md` - Complete chain.cpp examples
- `docs/ROADMAP.md` - Architecture decisions and development history
