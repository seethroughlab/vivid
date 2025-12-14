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
```

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
core/           Runtime engine (context, chain, hot-reload, ImGui visualizer)
addons/         Self-contained modular features (each has README, examples/, tests/):
  vivid-io/           Image loading (stb)
  vivid-effects-2d/   2D texture operators (25+ effects)
  vivid-video/        Video playback (HAP, platform codecs)
  vivid-render3d/     3D rendering (PBR, CSG, instancing)
  vivid-audio/        Audio synthesis, sequencing, analysis
  vivid-network/      OSC, UDP, WebSocket communication
  vivid-ml/           ML inference via ONNX Runtime
examples/       getting-started/ (onboarding) + showcase/ (multi-addon demos)
testing-fixtures/  Core tests (build-verification/, hardware/, value-operators/)
docs/           LLM-REFERENCE.md, RECIPES.md, OPERATOR-API.md
assets/         Shared assets (materials/, videos/, audio/, hdris/, meshes/, fonts/)
```

### Addon Structure
Each addon is self-contained with:
```
addons/vivid-XXX/
├── include/          # Public headers
├── src/              # Implementation
├── addon.json        # Metadata (name, version, operators)
├── README.md         # Documentation
├── examples/         # Addon-specific examples
├── tests/            # Unit tests + fixtures/
└── assets/           # Addon-specific assets (if any)
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

### Fluent API
All setters return `*this` for method chaining:
```cpp
chain.add<Noise>("noise").scale(4.0f).speed(0.5f).octaves(4);
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
| Add new 2D effect | `addons/vivid-effects-2d/include/vivid/effects/` |
| Modify chain visualizer UI | `core/imgui/chain_visualizer.cpp` |
| Hot-reload logic | `core/src/hot_reload.cpp` |
| Main runtime loop | `core/src/main.cpp` |
| Operator base class | `core/include/vivid/operator.h` |

## Documentation

- `docs/LLM-REFERENCE.md` - Compact operator reference (optimized for LLM context)
- `docs/RECIPES.md` - Complete chain.cpp examples
- `ROADMAP.md` - Architecture decisions and development history
