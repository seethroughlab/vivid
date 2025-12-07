# Vivid

A creative coding framework for real-time graphics with hot-reloadable C++ chains.

## Features

- **Hot Reload** - Edit your C++ code and see changes instantly without restarting
- **WebGPU Backend** - Modern GPU API via wgpu-native (Metal on macOS, Vulkan/DX12 elsewhere)
- **Chain-Based Architecture** - Connect operators to build visual pipelines
- **Addon System** - Modular design with optional feature packages
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

Press `F` to toggle fullscreen, `Esc` to quit.

## Usage

Create a `chain.cpp` file:

```cpp
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain(ctx, 1280, 720);

    chain->add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f);

    chain->add<HSV>("color")
        .input("noise")
        .hue(0.6f)
        .saturation(0.8f);

    chain->add<Output>("out")
        .input("color");
}

void update(Context& ctx) {
    chain->process();
}

VIVID_CHAIN(setup, update)
```

Run it:

```bash
./build/bin/vivid path/to/your/project
```

Edit your code while it's running - changes apply automatically.

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
- `Output` - Final output to screen

### Media
- `Video` - Video playback (HAP, H.264, ProRes)
- `Webcam` - Camera capture
- `Image` - Static image loading

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

### Registering Operators for Visualization

To see your operators in the visualizer, register them in your setup function:

```cpp
void setup(Context& ctx) {
    chain->add<Noise>("noise").scale(4.0f);
    chain->add<HSV>("color").input("noise");
    chain->add<Output>("out").input("color");

    // Register for visualization
    ctx.registerOperator("noise", &chain->get<Noise>("noise"));
    ctx.registerOperator("color", &chain->get<HSV>("color"));
    ctx.registerOperator("out", &chain->get<Output>("out"));
}
```

## Project Structure

```
vivid/
├── core/           # Runtime engine (~600 lines)
├── addons/         # Optional feature packages
│   └── vivid-effects-2d/
├── examples/       # Demo projects
└── assets/         # Shared resources
```

## LLM-Friendly Design

Vivid is designed with AI-assisted development in mind:

- **Minimal Core** - ~600 lines of runtime code that fits in context windows
- **Self-Contained Operators** - Each operator is a single .h/.cpp pair with embedded shaders
- **Consistent Patterns** - All operators follow the same structure (init/process/cleanup)
- **Fluent API** - Method chaining makes code readable and easy to generate
- **Comprehensive ROADMAP** - Detailed documentation helps LLMs understand the architecture
- **Hot Reload** - Instant feedback loop when iterating with AI assistance

The codebase is structured so an LLM can:
1. Understand the full architecture from ROADMAP.md
2. Read any operator as a complete, self-contained example
3. Generate new operators following established patterns
4. Modify chain.cpp files without needing deep context

## License

MIT

## Contributing

Contributions welcome! Please read the [ROADMAP.md](ROADMAP.md) for current development priorities.
