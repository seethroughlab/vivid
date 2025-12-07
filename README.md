# Vivid

A creative coding framework for real-time graphics with hot-reloadable C++ chains.

## Features

- **Hot Reload** - Edit your C++ code and see changes instantly without restarting
- **WebGPU Backend** - Modern GPU API via wgpu-native (Metal on macOS, Vulkan/DX12 elsewhere)
- **Chain-Based Architecture** - Connect operators to build visual pipelines
- **Addon System** - Modular design with automatic dependency discovery
- **State Preservation** - Feedback loops and animations survive hot reloads
- **LLM-Friendly** - Designed for AI-assisted development (see below)

## Quick Start

### Requirements

- CMake 3.20+
- C++17 compiler (Clang, GCC, or MSVC)
- macOS, Windows, or Linux

### Build

```bash
git clone https://github.com/seethroughlab/vivid.git
cd vivid
cmake -B build
cmake --build build
```

### Run an Example

```bash
./build/bin/vivid examples/chain-demo
```

Press `F` to toggle fullscreen, `Tab` to view chain visualizer, `Esc` to quit.

## Usage

Create a `chain.cpp` file:

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f);

    chain.add<HSV>("color")
        .input("noise")
        .hueShift(0.6f)
        .saturation(0.8f);

    chain.output("color");
}

void update(Context& ctx) {
    // Parameter tweaks go here (optional)
}

VIVID_CHAIN(setup, update)
```

Run it:

```bash
./build/bin/vivid path/to/your/project
```

Edit your code while it's running - changes apply automatically.

## How It Works

- **setup()** is called once on load and on each hot-reload
- **update()** is called every frame
- The core automatically calls `chain.init()` after setup and `chain.process()` after update
- Operator state (like Feedback buffers, video playback position) is preserved across hot-reloads

## Available Operators

### Generators
- `Noise` - Fractal noise (Perlin, Simplex, Worley, Value)
- `SolidColor` - Constant color
- `Ramp` - Linear gradient
- `Gradient` - Multi-mode gradients (linear, radial, angular, diamond)
- `Shape` - SDF shapes (circle, rect, triangle, star, polygon)
- `LFO` - Oscillators (sine, triangle, saw, square)
- `Image` - Load images from disk

### Effects
- `Blur` - Gaussian blur
- `Transform` - Scale, rotate, translate
- `HSV` - Hue/saturation/value adjustment
- `Brightness` - Brightness, contrast, gamma
- `Mirror` - Axis mirroring and kaleidoscope
- `Displace` - Texture-based distortion
- `Edge` - Sobel edge detection
- `Pixelate` - Mosaic effect
- `ChromaticAberration` - RGB separation
- `Bloom` - Glow effect
- `Tile` - Texture tiling
- `Feedback` - Frame feedback with decay

### Retro/Post-Processing
- `Dither` - Ordered dithering (Bayer 2x2, 4x4, 8x8)
- `Quantize` - Color palette reduction
- `Scanlines` - CRT-style lines
- `CRTEffect` - Full CRT simulation (curvature, vignette, phosphor)
- `Downsample` - Low-res pixelated look

### Modulation
- `Math` - Mathematical operations (add, multiply, clamp, remap, etc.)
- `Logic` - Comparison and logic (greater than, in range, toggle, etc.)

### Compositing
- `Composite` - Blend multiple inputs
- `Switch` - Select between inputs

### Media
- `VideoPlayer` - Video playback (HAP, H.264, ProRes)
- `Webcam` - Camera capture
- `Image` - Static image loading

### Particles
- `Particles` - 2D particle system with physics
- `PointSprites` - GPU point rendering

### 3D (vivid-render3d addon)
- `Render3D` - 3D scene renderer
- `MeshBuilder` - Procedural geometry with CSG

## UI & Visualization

Vivid includes a built-in chain visualizer powered by ImGui and ImNodes.

### Controls
- `Tab` - Toggle the visualizer overlay
- `F` - Toggle fullscreen
- `Esc` - Quit

### Chain Visualizer Features
- **Node Graph** - See your operator chain as connected nodes
- **Live Thumbnails** - Each node shows its real-time output texture
- **Parameter Display** - View current parameter values on each node
- **Connection Visualization** - See how operators are wired together
- **Performance Overlay** - FPS, frame time, and resolution display

## Project Structure

```
vivid/
├── core/                     # Runtime engine with integrated UI
│   ├── src/                  # Main runtime, hot-reload, addon discovery
│   ├── include/vivid/        # Public API headers
│   ├── imgui/                # Chain visualizer (ImGui/ImNodes)
│   └── shaders/              # Blit and text shaders
├── addons/                   # Optional feature packages
│   ├── vivid-effects-2d/     # 2D texture operators (always linked)
│   ├── vivid-video/          # Video playback (HAP, H.264, etc.)
│   └── vivid-render3d/       # 3D rendering (CSG, Manifold)
├── examples/                 # Demo projects
└── assets/                   # Shared resources
```

## Addon System

Addons are automatically discovered by scanning your chain.cpp `#include` directives:

```cpp
#include <vivid/effects/noise.h>   // → vivid-effects-2d addon
#include <vivid/video/player.h>    // → vivid-video addon
```

Each addon has an `addon.json` with metadata:
```json
{
  "name": "vivid-video",
  "version": "0.1.0",
  "operators": ["VideoPlayer", "AudioPlayer"]
}
```

The hot-reload system automatically adds include paths and links libraries for discovered addons.

## LLM-Friendly Design

Vivid is designed with AI-assisted development in mind:

- **Minimal Core** - ~600 lines of runtime code that fits in context windows
- **Self-Contained Operators** - Each operator is a single .h/.cpp pair with embedded shaders
- **Consistent Patterns** - All operators follow the same structure (init/process/cleanup)
- **Fluent API** - Method chaining makes code readable and easy to generate
- **Comprehensive Documentation** - LLM-optimized reference docs and recipes
- **Hot Reload** - Instant feedback loop when iterating with AI assistance
- **Automatic State Management** - No boilerplate for chain lifecycle

### Documentation for LLMs

| File | Purpose |
|------|---------|
| [docs/LLM-REFERENCE.md](docs/LLM-REFERENCE.md) | Compact operator reference (~200 lines) |
| [docs/RECIPES.md](docs/RECIPES.md) | Complete chain.cpp examples for common effects |
| [ROADMAP.md](ROADMAP.md) | Full architecture and development history |

### Using AI Assistants with Your Project

Create a `CLAUDE.md` file in your project folder to give AI assistants context about your specific project:

```markdown
# My Vivid Project

## Goal
[What effect you're trying to create]

## Current Chain
[Brief description of your operator chain]

## Resources
- docs/LLM-REFERENCE.md - Operator reference
- docs/RECIPES.md - Effect examples
```

See `examples/template/` for a complete starter project with CLAUDE.md.

## License

MIT

## Contributing

Contributions welcome! Please read the [ROADMAP.md](ROADMAP.md) for current development priorities.
