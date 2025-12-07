# Vivid

A creative coding framework for real-time graphics with hot-reloadable C++ chains.

## Features

- **Hot Reload** - Edit your C++ code and see changes instantly without restarting
- **WebGPU Backend** - Modern GPU API via wgpu-native (Metal on macOS, Vulkan/DX12 elsewhere)
- **Chain-Based Architecture** - Connect operators to build visual pipelines
- **Addon System** - Modular design with optional feature packages
- **State Preservation** - Feedback loops and animations survive hot reloads

## Quick Start

### Requirements

- CMake 3.20+
- C++17 compiler (Clang, GCC, or MSVC)
- macOS, Windows, or Linux

### Build

```bash
git clone https://github.com/yourusername/vivid.git
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
- `Ramp` - Gradient generator
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

### Compositing
- `Composite` - Blend multiple inputs
- `Switch` - Select between inputs
- `Output` - Final output to screen

## Project Structure

```
vivid/
├── core/           # Runtime engine (~600 lines)
├── addons/         # Optional feature packages
│   └── vivid-effects-2d/
├── examples/       # Demo projects
└── assets/         # Shared resources
```

## License

MIT

## Contributing

Contributions welcome! Please read the [ROADMAP.md](ROADMAP.md) for current development priorities.
